"""Run a linked RP2040 kernel's core 1 code on two emulated Cortex-M0+ cores.

The machine code comes from the kernel ELF unchanged. Two Unicorn instances
share one SRAM buffer. The core 0 instance also maps the kernel's flash and
executes the kernel's own core1_init(), core1_poll(), core1_park(),
watchdog_site() and NMI_Handler() when the scenario calls for them; the
core 1 instance maps no flash at all, so a single fetch or load from the
execute-in-place window ends the run. A register model stands in for the
SIO FIFOs, the boot ROM's side of the launch protocol, PSM.FRCE_OFF, the
timer, SYSCFG's NMI mask, the watchdog and each core's SysTick.

This is emulation: it establishes what the linked instructions do against
the modeled registers, and nothing about silicon timing or the real boot
ROM. Usage: emulate.py [--mutate NAME] unix.elf
"""

from __future__ import annotations

import argparse
import ctypes
import re
import struct
import sys
from collections import deque
from pathlib import Path

try:
    from elftools.elf.elffile import ELFFile  # type: ignore[import-not-found]
    from unicorn import (  # type: ignore[import-not-found]
        UC_ARCH_ARM,
        UC_HOOK_CODE,
        UC_MODE_MCLASS,
        UC_MODE_THUMB,
        UC_PROT_ALL,
        UC_PROT_EXEC,
        UC_PROT_READ,
        Uc,
        UcError,
    )
    from unicorn.arm_const import (  # type: ignore[import-not-found]
        UC_ARM_REG_LR,
        UC_ARM_REG_MSP,
        UC_ARM_REG_PC,
        UC_ARM_REG_PSP,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_SP,
        UC_CPU_ARM_CORTEX_M0,
    )
except ImportError as import_error:
    print("core 1 emulation requires Unicorn and pyelftools", file=sys.stderr)
    raise SystemExit(2) from import_error

FLASH_BASE = 0x10000000
FLASH_SIZE = 0x20000
SRAM_BASE = 0x20000000
SRAM_SIZE = 0x44000
APB_BASE = 0x40000000
APB_SIZE = 0x70000
SIO_BASE = 0xD0000000
PPB_BASE = 0xE000E000

SYSCFG = 0x40004000
RESETS = 0x4000C000
PSM = 0x40010000
TIMER = 0x40054000
WATCHDOG = 0x40058000



def header_interval(name: str) -> int:
    """The default of one interval in core1.h, the kernel's own source."""
    header = Path(__file__).resolve().parents[2] / "sys/arch/rp2040/rp2040/core1.h"
    match = re.search(rf"^#define\s+{name}\s+(\d+)U", header.read_text(), re.MULTILINE)
    if match is None:
        raise SystemExit(f"core 1 emulation: {name} not found in {header}")
    return int(match.group(1))


PERIOD_US = header_interval("C1_PERIOD_US")
STALL_US = header_interval("C1_STALL_US")
HANG_US = header_interval("C1_HANG_US")
STACK_PAINT = 0xC1C1C1C1
STACK_RESERVE = 64      # one exception frame and as much again
PROBE_BIT = 8
WDSEL_ALL_BUT_OSC = 0x1FFFC

# Where an emulated call returns to. Bit 2 of the return address selects the
# stack NMI_Handler reads its frame from, as bit 2 of EXC_RETURN does.
RETURN_MSP = FLASH_BASE + 0x00
RETURN_PSP = FLASH_BASE + 0x04


class Failure(Exception):
    """The emulated kernel did something the scenario does not allow."""


class Kernel:
    """Symbols, structure layouts and load segments of the kernel ELF."""

    def __init__(self, path: Path) -> None:
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            self.symbols: dict[str, tuple[int, int]] = {}
            for symbol in elf.get_section_by_name(".symtab").iter_symbols():
                if symbol.name and not symbol.name.startswith("$"):
                    self.symbols.setdefault(
                        symbol.name, (symbol["st_value"], symbol["st_size"])
                    )
            self.segments = [
                (segment["p_vaddr"], segment.data())
                for segment in elf.iter_segments()
                if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]
            ]
            self.members: dict[str, dict[str, int]] = {}
            if elf.has_dwarf_info():
                for unit in elf.get_dwarf_info().iter_CUs():
                    for entry in unit.iter_DIEs():
                        if entry.tag != "DW_TAG_structure_type":
                            continue
                        name = entry.attributes.get("DW_AT_name")
                        if name is None:
                            continue
                        fields = {
                            child.attributes["DW_AT_name"].value.decode():
                                child.attributes["DW_AT_data_member_location"].value
                            for child in entry.iter_children()
                            if child.tag == "DW_TAG_member"
                            and "DW_AT_data_member_location" in child.attributes
                            and "DW_AT_name" in child.attributes
                        }
                        if fields:
                            self.members.setdefault(name.value.decode(), fields)

    def address(self, name: str) -> int:
        if name not in self.symbols:
            raise Failure(f"kernel has no symbol {name}")
        return self.symbols[name][0]

    def offset(self, structure: str, member: str) -> int:
        try:
            return self.members[structure][member]
        except KeyError as error:
            raise Failure(f"no debug layout for {structure}.{member}") from error


class Machine:
    def __init__(self, kernel: Kernel, mutate: str | None) -> None:
        self.kernel = kernel
        self.mutate = mutate
        self.now = 1000000          # microsecond timer
        self.to_core = (deque(), deque())   # FIFO words waiting for each core
        self.sticky = [0, 0]
        self.frce_off = 0
        self.nmi_mask = 0
        self.intf = 0
        self.resets = 0xFFFFFFFF    # RESETS.RESET: everything held
        self.scratch = [0] * 8
        self.wdsel = 0
        self.triggered = False
        self.rom_step = None        # boot ROM launch state, None while running
        self.rom_words: list[int] = []
        self.launches: list[tuple[int, int, int]] = []
        self.parks = 0
        self.systick = [dict(csr=0, rvr=0, pend=0, next=0) for _ in range(2)]
        self.scr = [0x4, 0x4]       # SLEEPDEEP, as the boot ROM leaves core 1
        self.vtor = [0, 0]
        self.callout = False        # timeout(core1_poll) is pending
        self.wfi = [a for a in ()]
        self.core1_alive = True     # scenario switch: core 1 gets cycles
        self.core0_answers = True   # scenario switch: core 0 takes the NMI
        self.nmi_frame = (0x1000ABCC, 0x1000DEF1)
        self.nmi_stack = "msp"
        self.nmis = 0
        self.wakes = 0
        self.log: list[str] = []

        self.sram = (ctypes.c_ubyte * SRAM_SIZE)()
        for base, data in kernel.segments:
            if SRAM_BASE <= base < SRAM_BASE + SRAM_SIZE:
                ctypes.memmove(
                    ctypes.addressof(self.sram) + base - SRAM_BASE, data, len(data)
                )
        self.cpu = [self.make_cpu(0), None]
        flash = bytearray(FLASH_SIZE)
        for base, data in kernel.segments:
            if FLASH_BASE <= base < FLASH_BASE + FLASH_SIZE:
                flash[base - FLASH_BASE : base - FLASH_BASE + len(data)] = data
        self.cpu[0].mem_map(FLASH_BASE, FLASH_SIZE, UC_PROT_READ | UC_PROT_EXEC)
        self.cpu[0].mem_write(FLASH_BASE, bytes(flash))
        self.stubs = {
            kernel.address("timeout") & ~1: self.stub_timeout,
            kernel.address("untimeout") & ~1: self.stub_untimeout,
        }
        for stub_address in self.stubs:
            self.cpu[0].hook_add(
                UC_HOOK_CODE, self.hook_core0, begin=stub_address, end=stub_address
            )

        main, size = kernel.symbols["core1_main"]
        body = self.read(main & ~1, size)
        self.wfi = [
            (main & ~1) + offset
            for offset in range(0, size - 1, 2)
            if body[offset : offset + 2] == b"\x30\xbf"
        ]
        if len(self.wfi) != 1:
            raise Failure(f"core1_main holds {len(self.wfi)} WFI encodings, expected 1")
        self.at_wfi = False

    # Memory and registers.

    def make_cpu(self, core: int) -> Uc:
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
        cpu.mem_map_ptr(SRAM_BASE, SRAM_SIZE, UC_PROT_ALL, self.sram)
        cpu.mmio_map(APB_BASE, APB_SIZE, self.apb_read, core, self.apb_write, core)
        cpu.mmio_map(SIO_BASE, 0x1000, self.sio_read, core, self.sio_write, core)
        cpu.mmio_map(PPB_BASE, 0x1000, self.ppb_read, core, self.ppb_write, core)
        return cpu

    def read(self, address: int, size: int) -> bytes:
        return bytes(self.sram[address - SRAM_BASE : address - SRAM_BASE + size])

    def word(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def poke(self, address: int, value: int) -> None:
        struct.pack_into("<I", self.sram, address - SRAM_BASE, value & 0xFFFFFFFF)

    def field(self, symbol: str, *path: tuple[str, str]) -> int:
        address = self.kernel.address(symbol)
        for structure, member in path:
            address += self.kernel.offset(structure, member)
        return self.word(address)

    # The register model.

    def apb_read(self, _cpu: Uc, offset: int, _size: int, core: int) -> int:
        address = APB_BASE + (offset & ~0x3000)
        if address == TIMER + 0x28:
            self.now += 10          # a polling loop sees time pass
            return self.now & 0xFFFFFFFF
        if address == TIMER + 0x3C:
            return self.intf
        if address == SYSCFG:
            return self.nmi_mask
        if address == RESETS:
            return self.resets
        if address == RESETS + 0x8:
            return ~self.resets & 0xFFFFFFFF
        if address == PSM + 0x4:
            return self.frce_off
        if address == PSM + 0x8:
            return self.wdsel
        if WATCHDOG + 0xC <= address < WATCHDOG + 0x2C:
            return self.scratch[(address - WATCHDOG - 0xC) // 4]
        if address == WATCHDOG:
            return 0
        raise Failure(f"core {core} read unmodeled register {address:#x}")

    def apb_write(self, _cpu: Uc, offset: int, _size: int, value: int, core: int) -> None:
        address = APB_BASE + (offset & ~0x3000)
        alias = (offset >> 12) & 3

        def merged(old: int) -> int:
            return (value, old ^ value, old | value, old & ~value)[alias] & 0xFFFFFFFF

        if address == TIMER + 0x3C:
            self.intf = merged(self.intf)
        elif address == SYSCFG:
            if core != 0:
                raise Failure("core 1 wrote SYSCFG.PROC0_NMI_MASK")
            if self.resets & (1 << 18):
                raise Failure("SYSCFG written while held in reset")
            self.nmi_mask = merged(self.nmi_mask)
        elif address == RESETS:
            self.resets = merged(self.resets)
        elif address == PSM + 0x4:
            before = self.frce_off
            self.frce_off = merged(self.frce_off)
            self.psm_changed(before)
        elif address == PSM + 0x8:
            self.wdsel = merged(self.wdsel)
        elif WATCHDOG + 0xC <= address < WATCHDOG + 0x2C:
            self.scratch[(address - WATCHDOG - 0xC) // 4] = merged(0)
        elif address == WATCHDOG:
            if merged(0) & 0x80000000:
                if core != 1:
                    raise Failure("core 0 triggered the watchdog")
                self.triggered = True
                self.cpu[1].emu_stop()
        else:
            raise Failure(f"core {core} wrote unmodeled register {address:#x}")

    def psm_changed(self, before: int) -> None:
        bit = 1 << 16
        if self.frce_off & bit and not before & bit:
            self.cpu[1] = None          # core 1 is in reset
            self.rom_step = None
            self.at_wfi = False
        elif before & bit and not self.frce_off & bit:
            # Core 1 reaches the boot ROM's wait loop: it empties its own
            # FIFO and pushes a zero (datasheet 2.8.2).
            self.parks += 1
            self.to_core[1].clear()
            self.rom_step = 0
            self.rom_words = []
            if self.mutate != "rom-silent":
                self.push(1, 0)

    def push(self, sender: int, value: int) -> None:
        queue = self.to_core[1 - sender]
        if len(queue) >= 8:
            self.sticky[sender] |= 0x4
            return
        queue.append(value)
        if sender == 0 and self.rom_step is not None:
            self.rom_receive()

    def rom_receive(self) -> None:
        """The boot ROM's side of the launch: echo, restart on a wrong word."""
        expected = (0, 0, 1)
        value = self.to_core[1].popleft()
        if self.rom_step < 3 and value != expected[self.rom_step]:
            self.rom_step = 0
            self.rom_words = []
        else:
            self.rom_words.append(value)
            self.rom_step += 1
        self.push(1, value ^ (1 if self.mutate == "rom-wrong-echo" else 0))
        if self.rom_step == 6:
            vtor, stack, entry = self.rom_words[3:6]
            self.rom_step = None
            self.launches.append((vtor, stack, entry))
            if self.mutate == "rom-never-starts":
                return
            cpu = self.make_cpu(1)
            cpu.hook_add(
                UC_HOOK_CODE, self.hook_core1, begin=self.wfi[0], end=self.wfi[0]
            )
            cpu.reg_write(UC_ARM_REG_SP, stack)
            cpu.reg_write(UC_ARM_REG_PC, entry)
            self.vtor[1] = vtor
            self.systick[1] = dict(csr=0, rvr=0, pend=0, next=0)
            self.scr[1] = 0x4
            self.cpu[1] = cpu
            base, size = self.kernel.symbols["core1_stack"]
            for address in range(base, base + size, 4):
                self.poke(address, STACK_PAINT)
            self.core1_pc = entry

    def sio_read(self, _cpu: Uc, offset: int, _size: int, core: int) -> int:
        if offset == 0x50:
            status = self.sticky[core]
            if self.to_core[core]:
                status |= 0x1
            if len(self.to_core[1 - core]) < 8:
                status |= 0x2
            return status
        if offset == 0x58:
            if not self.to_core[core]:
                self.sticky[core] |= 0x8
                return 0
            return self.to_core[core].popleft()
        raise Failure(f"core {core} read SIO offset {offset:#x}")

    def sio_write(self, _cpu: Uc, offset: int, _size: int, value: int, core: int) -> None:
        if offset == 0x50:
            self.sticky[core] &= ~(value & 0xC)
        elif offset == 0x54:
            self.push(core, value)
        else:
            raise Failure(f"core {core} wrote SIO offset {offset:#x}")

    def ppb_read(self, _cpu: Uc, offset: int, _size: int, core: int) -> int:
        tick = self.systick[core]
        if offset == 0x10:
            return tick["csr"]
        if offset == 0x14:
            return tick["rvr"]
        if offset == 0xD04:
            return tick["pend"] << 26
        if offset == 0xD08:
            return self.vtor[core]
        if offset == 0xD10:
            return self.scr[core]
        raise Failure(f"core {core} read PPB offset {offset:#x}")

    def ppb_write(self, _cpu: Uc, offset: int, _size: int, value: int, core: int) -> None:
        tick = self.systick[core]
        if offset == 0x10:
            tick["csr"] = value & 7
            tick["next"] = self.now + (tick["rvr"] + 1) // 125
        elif offset == 0x14:
            tick["rvr"] = value & 0xFFFFFF
        elif offset == 0x18:
            tick["pend"] = 0
        elif offset == 0xD04:
            if value & (1 << 25):
                tick["pend"] = 0
        elif offset == 0xD10:
            self.scr[core] = value
        else:
            raise Failure(f"core {core} wrote PPB offset {offset:#x}")

    # Core 0: the kernel's own functions, called as the kernel would.

    def hook_core0(self, cpu: Uc, address: int, _size: int, _data: object) -> None:
        stub = self.stubs.get(address)
        if stub is not None:
            stub(cpu)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def stub_timeout(self, cpu: Uc) -> None:
        if (cpu.reg_read(UC_ARM_REG_R0) & ~1) != (self.kernel.address("core1_poll") & ~1):
            raise Failure("timeout() scheduled something other than core1_poll")
        if self.callout:
            raise Failure("core1_poll scheduled twice")
        self.callout = True

    def stub_untimeout(self, _cpu: Uc) -> None:
        self.callout = False

    def call(self, name: str, *arguments: int, ret: int = RETURN_MSP) -> None:
        cpu = self.cpu[0]
        cpu.reg_write(UC_ARM_REG_SP, self.kernel.address("_estack") - 64)
        cpu.reg_write(UC_ARM_REG_LR, ret | 1)
        for register, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1), arguments):
            cpu.reg_write(register, value)
        try:
            cpu.emu_start(self.kernel.address(name) | 1, ret, count=2000000)
        except UcError as error:
            raise Failure(
                f"core 0 in {name}: {error} at pc "
                f"{cpu.reg_read(UC_ARM_REG_PC):#x}"
            ) from error
        if cpu.reg_read(UC_ARM_REG_PC) != ret:
            raise Failure(f"core 0 did not return from {name}")

    def serve_nmi(self) -> None:
        """Core 0 takes the NMI when the forced interrupt is routed to it."""
        if not (self.intf & self.nmi_mask & PROBE_BIT) or not self.core0_answers:
            return
        pc, lr = self.nmi_frame
        frame = self.kernel.address("_estack") - 0x400
        for index, value in enumerate((0, 1, 2, 3, 12, lr, pc, 0x01000000)):
            self.poke(frame + 4 * index, value)
        cpu = self.cpu[0]
        if self.nmi_stack == "psp":
            cpu.reg_write(UC_ARM_REG_PSP, frame)
            cpu.reg_write(UC_ARM_REG_MSP, self.kernel.address("_estack") - 0x200)
            self.call_raw("NMI_Handler", RETURN_PSP)
        else:
            cpu.reg_write(UC_ARM_REG_PSP, 0)
            cpu.reg_write(UC_ARM_REG_MSP, frame)
            self.call_raw("NMI_Handler", RETURN_MSP)
        self.nmis += 1
        if self.intf & PROBE_BIT:
            raise Failure("NMI_Handler left the forced interrupt asserted")

    def call_raw(self, name: str, ret: int) -> None:
        """Enter a handler without touching the stack pointers."""
        cpu = self.cpu[0]
        cpu.reg_write(UC_ARM_REG_LR, ret | 1)
        try:
            cpu.emu_start(self.kernel.address(name) | 1, ret, count=10000)
        except UcError as error:
            raise Failure(f"core 0 in {name}: {error}") from error

    # Core 1: free-running between its WFI instructions.

    def hook_core1(self, cpu: Uc, address: int, _size: int, _data: object) -> None:
        if address == self.wfi[0]:
            self.at_wfi = True
            cpu.emu_stop()

    def run_core1(self) -> None:
        """Let core 1 run until it sleeps, if its wake condition holds."""
        cpu = self.cpu[1]
        if cpu is None or not self.core1_alive or self.triggered:
            return
        tick = self.systick[1]
        if tick["csr"] & 1 and self.now >= tick["next"]:
            tick["pend"] = 1
            tick["next"] += (tick["rvr"] + 1) // 125
        if self.at_wfi:
            if not (tick["pend"] and tick["csr"] & 2):
                return
            self.at_wfi = False
            self.wakes += 1
            self.core1_pc = self.wfi[0] + 2
        try:
            cpu.emu_start(self.core1_pc | 1, 0xFFFFFFFE, count=200000)
        except UcError as error:
            raise Failure(
                f"core 1: {error} at pc {cpu.reg_read(UC_ARM_REG_PC):#x}"
            ) from error
        self.core1_pc = cpu.reg_read(UC_ARM_REG_PC)
        if self.cpu[1] is cpu and not self.at_wfi and not self.triggered:
            raise Failure(f"core 1 never slept; pc {self.core1_pc:#x}")

    # The scenario clock.

    def advance(self, milliseconds: int, beating: bool = True) -> None:
        """Model time: core 0 ticks at 1 kHz while beating, polls at 1 Hz."""
        stamp = self.kernel.address("clkcatch") + self.kernel.offset(
            "clock_catchup", "stamp_us"
        )
        for _ in range(milliseconds):
            if self.triggered:
                return
            self.now += 1000
            if beating:
                self.poke(stamp, self.now)
            self.elapsed_ms += 1
            if beating and self.callout and self.elapsed_ms % 1000 == 0:
                self.callout = False
                self.call("core1_poll", 0)
            self.run_core1()
            self.serve_nmi()
        self.note_stack()

    def note_stack(self) -> None:
        """Record the least stack core 1 has left untouched since a launch."""
        if self.cpu[1] is None:
            return
        base, size = self.kernel.symbols["core1_stack"]
        free = 0
        while free < size and self.word(base + free) == STACK_PAINT:
            free += 4
        Machine.stack_free = min(Machine.stack_free, free)

    stack_free = 1 << 30

    elapsed_ms = 0


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise Failure(message)


def boot(kernel: Kernel, mutate: str | None = None) -> Machine:
    """core1_init() as clkstart() calls it, then the first second."""
    machine = Machine(kernel, mutate)
    machine.frce_off = 1 << 16      # Reset_Handler holds core 1
    machine.call("core1_init")
    expect(not machine.resets & (1 << 18), "core1_init left SYSCFG in reset")
    expect(machine.callout, "core1_init scheduled no poll")
    expect(machine.cpu[1] is None, "core 1 ran before the first poll")
    return machine


def sup(machine: Machine, member: str) -> int:
    return machine.field("core1_sup", ("core1_sup", member))


def obs(machine: Machine, member: str) -> int:
    return machine.field(
        "core1", ("core1_shared", "c1_obs"), ("core1_obs", member)
    )


def scenario_launch_and_ping(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(1000)
    expect(len(machine.launches) == 1, "the first poll did not launch core 1")
    vtor, stack, entry = machine.launches[0]
    expect(vtor == kernel.address("core1"), "VTOR is not the shared block")
    expect(vtor % 256 == 0, "the vector table is not 256-byte aligned")
    stack_base, stack_size = kernel.symbols["core1_stack"]
    expect(stack == stack_base + stack_size, "the stack pointer is not the stack top")
    expect(entry == kernel.address("core1_main") | 1, "the entry is not core1_main")
    fault = kernel.address("core1_fault") | 1
    for index in range(2, 16):
        expect(machine.word(vtor + 4 * index) == fault,
               f"vector {index} does not name core1_fault")
    expect(sup(machine, "cs_state") == 1, "the supervisor is not running")
    expect(machine.nmi_mask == PROBE_BIT, "the NMI route is not set")
    expect(not machine.to_core[0] and not machine.to_core[1],
           "the launch left a word in a FIFO")
    machine.advance(30000)
    expect(sup(machine, "cs_seq") == 30, "30 pings were not sent")
    expect(sup(machine, "cs_pong") in (29, 30), "the pings were not answered")
    expect(sup(machine, "cs_restarts") == 0, "a healthy core 1 was restarted")
    expect(machine.scr[1] == 0, "core 1 left SLEEPDEEP set")
    expect(2990 <= machine.wakes <= 3010,
           f"core 1 woke {machine.wakes} times in 30 s, expected one per 10 ms")
    expect(machine.nmis == 0 and obs(machine, "co_stalls") == 0,
           "a ticking clock was reported")
    expect(machine.sticky == [0, 0], "a FIFO error was raised")
    return "launch through the boot ROM protocol, 30 pings answered"


def scenario_transient_stall(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    for stack, ret_bit in (("msp", 0), ("psp", 1)):
        machine.nmi_stack = stack
        machine.nmi_frame = (0x10004000 + 0x100 * ret_bit, 0x10005001)
        before = machine.nmis
        machine.advance(300, beating=False)
        expect(machine.nmis == before + 1,
               f"a 300 ms stall raised {machine.nmis - before} probes ({stack})")
        probe = kernel.address("core1_probe")
        expect(machine.word(probe + 4) == machine.word(probe),
               "the probe went unanswered")
        expect(machine.word(probe + 8) == machine.nmi_frame[0],
               f"the stacked PC was not recorded from the {stack} frame")
        expect(machine.word(probe + 12) == machine.nmi_frame[1],
               "the stacked LR was not recorded")
        machine.advance(2000)
    expect(obs(machine, "co_stalls") == 2, "two stalls were not counted")
    gap = obs(machine, "co_gap_us")
    expect(280000 <= gap <= 305000, f"longest stall recorded as {gap} us")
    expect(not machine.triggered, "a transient stall reset the chip")
    expect(sup(machine, "cs_restarts") == 0, "core 1 was restarted")
    return "two 300 ms stalls probed once each, PC and LR from MSP and PSP frames"


def scenario_declared_erase(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    for _ in range(20):
        machine.call("watchdog_site", 1, 0x1A0000)
        expect(machine.nmi_mask == 0, "the NMI route stayed up inside the section")
        expect(machine.scratch[1] == 1, "the section was not declared")
        machine.advance(400, beating=False)
        # The erase path advances the tick record before it clears the name.
        machine.poke(kernel.address("clkcatch") + kernel.offset(
            "clock_catchup", "stamp_us"), machine.now)
        machine.call("watchdog_site", 0, 0)
        expect(machine.nmi_mask == PROBE_BIT, "the NMI route was not restored")
        machine.advance(37)
    expect(obs(machine, "co_stalls") == 0 and machine.nmis == 0,
           "a declared 400 ms erase was reported as a stall")
    expect(machine.intf == 0, "a probe was raised inside a declared section")
    expect(not machine.triggered, "a declared erase reset the chip")
    return "twenty declared 400 ms sections, no probe and no stall"


def scenario_hang(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    machine.nmi_frame = (0x1000F44C, 0x1000F4A1)
    start = machine.now
    machine.advance(6000, beating=False)
    expect(machine.triggered, "a 6 s hang did not reset the chip")
    took = machine.now - start
    expect(HANG_US <= took <= HANG_US + 3 * PERIOD_US,
           f"the reset came {took} us into the hang")
    expect(machine.nmis == 2, f"{machine.nmis} probes were answered, expected 2")
    expect(machine.scratch[1] == 3, "SCRATCH1 does not name the stall site")
    expect(machine.scratch[2] == machine.nmi_frame[0], "SCRATCH2 is not the PC")
    expect(machine.scratch[3] == machine.nmi_frame[1], "SCRATCH3 is not the LR")
    expect(machine.wdsel == WDSEL_ALL_BUT_OSC, "WDSEL does not reset the chip")
    return "hang reset after 4 s with the answered PC and LR in the scratch registers"


def scenario_hang_unanswered(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    machine.advance(200, beating=False)     # an answered probe leaves a PC
    machine.advance(1000)
    machine.core0_answers = False
    machine.advance(6000, beating=False)
    expect(machine.triggered, "the hang did not reset the chip")
    expect(machine.scratch[1] == 3, "SCRATCH1 does not name the stall site")
    expect(machine.scratch[2] == 0 and machine.scratch[3] == 0,
           "an unanswered probe was recorded with a stale PC")
    return "unanswered probe recorded as zeros, not as the previous stall's PC"


def scenario_hang_in_declared_section(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    machine.call("watchdog_site", 2, 0x1B2000)
    machine.advance(6000, beating=False)
    expect(machine.triggered, "a section that never returned did not reset")
    expect(machine.scratch[1] == 2 and machine.scratch[2] == 0x1B2000,
           "the declared section's own record was overwritten")
    expect(machine.nmis == 0, "an NMI was delivered with execute-in-place down")
    return "a declared section that never returns resets with its own record"


def scenario_core1_dies(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(5000)
    machine.core1_alive = False
    machine.advance(3000)
    expect(sup(machine, "cs_restarts") == 1, "a silent core 1 was not restarted")
    expect(machine.cpu[1] is None or len(machine.launches) == 2,
           "core 1 was not reset")
    machine.core1_alive = True
    machine.advance(6000)
    expect(len(machine.launches) == 2, "core 1 was not launched again")
    expect(sup(machine, "cs_state") == 1 and sup(machine, "cs_epoch") == 2,
           "the supervisor did not return to running")
    expect(sup(machine, "cs_pong") >= sup(machine, "cs_seq") - 1,
           "the relaunched core 1 does not answer")
    # Core 1 stays dead: the budget runs out and it is left parked.
    machine.core1_alive = False
    machine.advance(40000)
    expect(sup(machine, "cs_state") == 3, "the restart budget did not end")
    expect(not machine.callout, "the supervisor still polls a parked core 1")
    expect(machine.nmi_mask == 0, "the NMI route outlived core 1")
    expect(machine.cpu[1] is None, "core 1 was left running")
    return "silent core 1 restarted with backoff, then parked for good"


def scenario_park(kernel: Kernel) -> str:
    machine = boot(kernel)
    machine.advance(3000)
    machine.intf = PROBE_BIT
    machine.core0_answers = False
    machine.call("core1_park")
    expect(machine.cpu[1] is None, "core1_park left core 1 running")
    expect(machine.frce_off == 0, "core1_park left core 1 held in reset")
    expect(machine.nmi_mask == 0, "core1_park left the NMI route up")
    expect(machine.intf == 0, "core1_park left a forced interrupt asserted")
    expect(machine.word(kernel.address("core1_nmi_mask")) == 0,
           "core1_nmi_mask still names the route")
    machine.call("watchdog_site", 1, 0)
    machine.call("watchdog_site", 0, 0)
    expect(machine.nmi_mask == 0, "watchdog_site restored a withdrawn route")
    return "park from a running core 1: reset, route withdrawn, interrupt cleared"


def scenario_rom_never_answers(kernel: Kernel) -> str:
    machine = boot(kernel, "rom-silent")
    start = machine.now
    machine.advance(1000)
    expect(not machine.launches, "a silent boot ROM was reported as launched")
    expect(sup(machine, "cs_state") == 2, "a failed launch is not waiting")
    expect(machine.now - start < 1000000 + 400000, "the launch wait is unbounded")
    machine.advance(60000)
    expect(sup(machine, "cs_state") == 3, "failed launches never ended")
    # Each attempt parks core 1 before the handshake and again when it fails.
    expect(machine.parks == 8, f"{machine.parks} parks, expected 8 for 4 attempts")
    expect(machine.nmi_mask == 0, "the NMI route is up with no core 1")
    return "a boot ROM that never answers costs four bounded attempts"


def scenario_rom_wrong_echo(kernel: Kernel) -> str:
    machine = boot(kernel, "rom-wrong-echo")
    machine.advance(1000)
    expect(sup(machine, "cs_state") == 2, "a wrong echo was accepted as a launch")
    expect(machine.nmi_mask == 0, "the NMI route is up after a failed launch")
    return "a wrong echo restarts the sequence until the budget ends"


def scenario_rom_never_starts(kernel: Kernel) -> str:
    machine = boot(kernel, "rom-never-starts")
    machine.advance(5000)
    expect(len(machine.launches) >= 1, "no launch was attempted")
    expect(sup(machine, "cs_restarts") >= 1,
           "a core 1 that never ran was not detected by the missing pongs")
    return "a completed handshake with no running core 1 is caught by the ping"


SCENARIOS = (
    scenario_launch_and_ping,
    scenario_transient_stall,
    scenario_declared_erase,
    scenario_hang,
    scenario_hang_unanswered,
    scenario_hang_in_declared_section,
    scenario_core1_dies,
    scenario_park,
    scenario_rom_never_answers,
    scenario_rom_wrong_echo,
    scenario_rom_never_starts,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf", type=Path)
    arguments = parser.parse_args()
    try:
        kernel = Kernel(arguments.elf)
        for scenario in SCENARIOS:
            Machine.elapsed_ms = 0
            print(f"PASS core1-emulation {scenario.__name__[9:]}: {scenario(kernel)}")
        size = kernel.symbols["core1_stack"][1]
        if Machine.stack_free < STACK_RESERVE:
            raise Failure(
                f"core 1 left {Machine.stack_free} of {size} stack bytes "
                f"untouched, fewer than {STACK_RESERVE}"
            )
        print(
            f"PASS core1-emulation stack: core 1 used at most "
            f"{size - Machine.stack_free} of {size} bytes"
        )
    except Failure as failure:
        print(f"FAIL core1-emulation {arguments.elf}: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
