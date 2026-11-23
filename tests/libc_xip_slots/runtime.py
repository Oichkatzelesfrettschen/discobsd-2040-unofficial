"""Execute canonical pointers and argument-preserving slots across process states."""

import struct

import unicorn
import unicorn.arm_const as arm
from elftools.elf.elffile import ELFFile
from symbols import functions, require

FLASH = 0x10020000
RAM = 0x20000000
BASES = (RAM + 0x1000, RAM + 0x3000)
REGS = tuple(getattr(arm, "UC_ARM_REG_R" + str(i)) for i in range(13))


def exercise(image, application):
    symbols = dict(image["symbols"])
    machine = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
    machine.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M0)
    machine.mem_map(FLASH, 0xC000)
    machine.mem_map(RAM, 0x30000)
    for address, data in image["sections"]:
        if address >= FLASH:
            machine.mem_write(address, data)
        else:
            for base in BASES:
                machine.mem_write(base + address, data)
    with application.open("rb") as stream:
        elf = ELFFile(stream)
        imports = functions(elf, ("api_sum", "api_alias", "api_xor"), image["slot_width"])
        for name, imported in imports.items():
            require(
                imported["st_value"] == image["symbols"][name],
                "application import address differs: " + name,
            )
        for symbol in elf.get_section_by_name(".symtab").iter_symbols():
            if symbol.name.startswith("app_"):
                symbols[symbol.name] = symbol["st_value"]
        for section in elf.iter_sections():
            if section["sh_flags"] & 2 and section["sh_type"] != "SHT_NOBITS":
                require(
                    RAM + 0x10000
                    <= section["sh_addr"]
                    < section["sh_addr"] + section["sh_size"]
                    <= RAM + 0x18000,
                    "application section containment differs",
                )
                machine.mem_write(section["sh_addr"], section.data())
    for base in BASES:
        for offset in image["relocations"]:
            value = struct.unpack("<I", machine.mem_read(base + offset, 4))[0]
            require(value < image["state_size"], "relocation value leaves fixture state")
            machine.mem_write(base + offset, struct.pack("<I", base + value))
    machine.mem_protect(FLASH, 0xC000, unicorn.UC_PROT_READ | unicorn.UC_PROT_EXEC)
    calls = 0

    def call(address, base, stack, arguments=()):
        nonlocal calls
        inactive = BASES[1] if base == BASES[0] else BASES[0]
        before = bytes(machine.mem_read(inactive, image["state_size"]))
        values = [0x12340000 + i * 0x101 for i in range(13)]
        values[9] = base + image["got_offset"]
        for index, value in enumerate(arguments[:4]):
            values[index] = value
        for index, value in enumerate(values):
            machine.reg_write(REGS[index], value)
        stack_bytes = struct.pack("<4I", *(list(arguments[4:]) + [0xAABBCCDD] * 4)[:4])
        machine.mem_write(stack, stack_bytes)
        stop = FLASH + 0xBFF0
        machine.reg_write(arm.UC_ARM_REG_SP, stack)
        machine.reg_write(arm.UC_ARM_REG_LR, stop | 1)
        machine.emu_start(address | 1, stop, count=2000)
        require(machine.reg_read(arm.UC_ARM_REG_PC) == stop, "call instruction budget expires")
        require(machine.reg_read(arm.UC_ARM_REG_SP) == stack, "call stack differs")
        require(
            all(machine.reg_read(REGS[index]) == values[index] for index in range(4, 12)),
            "call saved registers differ",
        )
        require(bytes(machine.mem_read(stack, 16)) == stack_bytes, "stack arguments differ")
        require(
            bytes(machine.mem_read(inactive, image["state_size"])) == before,
            "inactive process state differs",
        )
        calls += 1
        return machine.reg_read(arm.UC_ARM_REG_R0)

    corpus = (
        (1, 2, 3, 4, 5, 6),
        (0, 0, 0, 0, 0, 0),
        (0xFFFFFFFF, 0x80000000, 0xFFFFFFFD, 7, 0x90000001, 0x7FFFFFFF),
    )
    for stack in (RAM + 0x18000, RAM + 0x19008, RAM + 0x1AFF0):
        for base in BASES:
            for name in ("api_address", "api_alias_address", "peer_address", "app_address"):
                require(
                    call(symbols[name], base, stack) == (FLASH | 1),
                    "returned public pointer differs",
                )
            pointers = struct.unpack("<2I", machine.mem_read(base + symbols["saved"], 8))
            peer = struct.unpack("<I", machine.mem_read(base + symbols["peer_saved"], 4))[0]
            application_pointer = struct.unpack("<I", machine.mem_read(symbols["app_saved"], 4))[0]
            require(
                pointers == (FLASH | 1, FLASH | 1) and peer == application_pointer == (FLASH | 1),
                "initialized public pointer differs",
            )
            for arguments in corpus:
                for target, extra in (
                    (FLASH | 1, 0),
                    (symbols["api_alias"], 0),
                    (pointers[0], 0),
                    (pointers[1], 0),
                    (peer, 0),
                    (application_pointer, 0),
                    (symbols["api_nested"], 1),
                    (symbols["peer_call"], 2),
                    (symbols["app_call"], 2),
                ):
                    current = struct.unpack("<I", machine.mem_read(base + symbols["counter"], 4))[0]
                    expected = (
                        sum(a * b for a, b in zip(arguments, (1, 3, 5, 7, 11, 13)))
                        + current
                        + extra
                    ) & 0xFFFFFFFF
                    require(call(target, base, stack, arguments) == expected, "call result differs")
                    require(
                        struct.unpack("<I", machine.mem_read(base + symbols["counter"], 4))[0]
                        == (current + 1) & 0xFFFFFFFF,
                        "call state update differs",
                    )
                current = struct.unpack("<I", machine.mem_read(base + symbols["counter"], 4))[0]
                expected = (
                    sum(a * b for a, b in zip(arguments, (1, 3, 5, 7, 11, 13))) + current + 5
                ) & 0xFFFFFFFF
                require(
                    call(symbols["api_through"], base, stack, (symbols["app_call"], *arguments))
                    == expected,
                    "callback result differs",
                )
                require(
                    struct.unpack("<I", machine.mem_read(base + symbols["counter"], 4))[0]
                    == (current + 1) & 0xFFFFFFFF,
                    "callback state update differs",
                )
                expected = arguments[0] ^ arguments[1] ^ arguments[2] ^ arguments[3]
                require(
                    call(FLASH + image["slot_width"] + 1, base, stack, arguments) == expected,
                    "assembly call result differs",
                )
    return calls
