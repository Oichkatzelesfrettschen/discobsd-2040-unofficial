"""discobsd-sramdump: read a stuck board's kernel state out of its SRAM.

A forced reset into the boot ROM leaves the kernel's RAM intact (measured
on a bare Pico), so a board whose USB console has gone silent still holds
the state that explains the silence: the process table, each process's wait
channel, the console tty's state bits and the USB driver's counters. Two
subcommands split the work:

    discobsd-sramdump capture --usb-port PORT --force-reset [--out DIR] [--resume]
    discobsd-sramdump decode UNIX.ELF SRAM.BIN [--flash FLASH.BIN]

capture destroys the running session: it forces the board at PORT into the
boot ROM with `picotool reboot -u -f`, waits for product 0003 on that port,
and saves SRAM (0x20000000-0x20042000) and the kernel flash region
(128 KiB, or 160 KiB with --kernel-layout picocalc-sd) to sram.bin and
flash.bin. The port is mandatory
even with one board attached, every picotool call carries the --bus and
--address that the port's sysfs node holds at that moment (the address
changes at each re-enumeration), and the 2e8a devices on every other port
must keep their address and product for the whole run. Linux only: it reads
sysfs. DISCOBSD_SYSFS_USB replaces /sys/bus/usb/devices for tests, as in
discobsd-flash.

decode needs no hardware and no sysfs. It reads the variable and structure
layouts from the DWARF of the kernel ELF that was running (pyelftools), so
a wrong ELF decodes to plausible garbage; with --flash it first compares
the `version` string bytes in the saved flash with the ELF's own and stops
on a mismatch.
"""

from __future__ import annotations

import argparse
import bisect
import glob
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone

from . import __version__, ports

SRAM_BASE = 0x20000000
SRAM_END = 0x20042000
FLASH_BASE = 0x10000000
FLASH_END = 0x10020000
FLASH_LAYOUT_END = {"standard": FLASH_END, "picocalc-sd": 0x10028000}
SRAM_SIZE = SRAM_END - SRAM_BASE
FLASH_SIZE = FLASH_END - FLASH_BASE

KERNEL_PRODUCT = "000a"
BOOTROM_PRODUCT = "0003"
SRAM_FILE = "sram.bin"
FLASH_FILE = "flash.bin"
# The boot ROM enumerates a moment after the reset; 80 polls of a quarter
# second bound the wait, and one more second lets libusb see the interface.
BOOTROM_POLLS = 80
POLL_SECONDS = 0.25
SETTLE_SECONDS = 1.0
PICOTOOL_TIMEOUT = 120

# sys/sys/proc.h, "stat codes" and "flag codes"; tests/test_sramdump.py
# compares both tables with the header.
SZOMB = 5
PROC_STAT = {
    1: "SSLEEP",
    2: "SWAIT",
    3: "SRUN",
    4: "SIDL",
    5: "SZOMB",
    6: "SSTOP",
}
PROC_FLAGS = [
    (0x0001, "SLOAD"),
    (0x0002, "SSYS"),
    (0x0004, "SLOCK"),
    (0x0008, "SSWAP"),
    (0x0010, "P_TRACED"),
    (0x0020, "P_WAITED"),
    (0x0080, "P_SINTR"),
    (0x0100, "SVFORK"),
    (0x0200, "SVFPRNT"),
    (0x0400, "SVFDONE"),
    (0x1000, "P_TIMEOUT"),
    (0x2000, "P_NOCLDSTOP"),
    (0x4000, "P_SELECT"),
]
# sys/sys/tty.h, "internal state bits".
TTY_STATE = [
    (0x000001, "TS_TIMEOUT"),
    (0x000002, "TS_WOPEN"),
    (0x000004, "TS_ISOPEN"),
    (0x000008, "TS_FLUSH"),
    (0x000010, "TS_CARR_ON"),
    (0x000020, "TS_BUSY"),
    (0x000040, "TS_ASLEEP"),
    (0x000080, "TS_XCLUDE"),
    (0x000100, "TS_TTSTOP"),
    (0x000200, "TS_HUPCLS"),
    (0x000400, "TS_TBLOCK"),
    (0x000800, "TS_RCOLL"),
    (0x001000, "TS_WCOLL"),
    (0x004000, "TS_ASYNC"),
    (0x040000, "TS_ERASE"),
    (0x080000, "TS_LNCH"),
    (0x100000, "TS_TYPEN"),
    (0x200000, "TS_CNTTB"),
]
TS_BUSY = 0x000020
# machdep.usb_* sysctl names and the kernel variables that back them
# (sys/arch/rp2040/rp2040/sysctl.c).
USB_COUNTERS = [
    ("usb_e15_deferred", "usb_e15_deferred"),
    ("usb_bulkin_arms", "usb_e15_bulkin_arms"),
    ("usb_service_reentered", "usb_service_reentered"),
    ("usb_tx_recovered", "usb_tx_recovered"),
]
USBD_FIELDS = (
    "configured",
    "dtr",
    "tx_busy",
    "tx_pending",
    "tx_head",
    "tx_tail",
    "rx_head",
    "rx_tail",
    "rx_polled",
)
REQUIRED_SYMBOLS = ("hostname", "time", "lbolt", "runin", "runout", "proc", "version")


class ToolError(Exception):
    """An operator-facing failure; main() prints it and exits 1."""


def flag_names(value: int, table) -> str:
    """Names of the set bits, with any bit the table lacks as one hex term."""
    names = []
    rest = value
    for bit, name in table:
        if value & bit:
            names.append(name)
            rest &= ~bit
    if rest:
        names.append("0x%x" % rest)
    return "|".join(names) if names else "0"


# --- decode -----------------------------------------------------------------


def _elftools():
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError as exc:
        raise ToolError(
            "pyelftools is missing: pip install pyelftools, apt install python3-pyelftools, "
            "or pacman -S python-pyelftools"
        ) from exc
    return ELFFile


class Kernel:
    """Symbols and DWARF layouts of one kernel ELF."""

    def __init__(self, path: str):
        elf_file = _elftools()
        try:
            self._file = open(path, "rb")
            self.elf = elf_file(self._file)
            symtab = self.elf.get_section_by_name(".symtab")
            dwarf = self.elf.get_dwarf_info() if self.elf.has_dwarf_info() else None
        except OSError as exc:
            raise ToolError("cannot read %s: %s" % (path, exc)) from exc
        except Exception as exc:  # pyelftools raises its own ELFError for a non-ELF file
            raise ToolError("%s is not a readable ELF file: %s" % (path, exc)) from exc
        if symtab is None or dwarf is None:
            raise ToolError("%s has no symbol table or no DWARF; build the kernel with -g" % path)
        self.syms: dict[str, tuple[int, int]] = {}
        by_addr: dict[int, tuple[int, int, str]] = {}
        for sym in symtab.iter_symbols():
            name = sym.name
            kind = sym["st_info"]["type"]
            addr = sym["st_value"]
            if not name or name.startswith("$") or addr == 0:
                continue
            if kind not in ("STT_OBJECT", "STT_FUNC", "STT_NOTYPE"):
                continue
            is_global = sym["st_info"]["bind"] != "STB_LOCAL"
            old = self.syms.get(name)
            if old is None or (is_global and old[2] is False):
                self.syms[name] = (addr, sym["st_size"], is_global)
            # One symbol per address: prefer a sized global symbol.
            rank = (sym["st_size"] > 0, is_global)
            kept = by_addr.get(addr)
            if kept is None or rank > kept[0]:
                by_addr[addr] = (rank, sym["st_size"], name)
        self.syms = {k: (v[0], v[1]) for k, v in self.syms.items()}
        self._addrs = sorted(by_addr)
        self._by_addr = by_addr
        self.vars: dict = {}
        self.structs: dict = {}
        for cu in dwarf.iter_CUs():
            for die in cu.get_top_DIE().iter_children():
                name = die.attributes.get("DW_AT_name")
                if not name:
                    continue
                name = name.value.decode()
                if die.tag == "DW_TAG_variable" and "DW_AT_type" in die.attributes:
                    # A declaration carries the same type as its definition.
                    self.vars.setdefault(name, die)
                elif die.tag == "DW_TAG_structure_type" and die.has_children:
                    self.structs.setdefault(name, die)

    def close(self) -> None:
        self._file.close()

    def require(self, names) -> None:
        missing = [n for n in names if n not in self.syms]
        if missing:
            raise ToolError(
                "the ELF has no symbol %s; it is not a DiscoBSD RP2040 kernel built with symbols"
                % ", ".join(missing)
            )

    def nearest(self, addr: int) -> str:
        """The symbol at or below addr as name+offset, or '?' below every symbol."""
        i = bisect.bisect_right(self._addrs, addr) - 1
        if i < 0:
            return "?"
        start = self._addrs[i]
        _, size, name = self._by_addr[start]
        text = "%s+0x%x" % (name, addr - start)
        if size and addr >= start + size:
            text += " (past its size 0x%x)" % size
        return text

    def elf_bytes(self, addr: int, size: int) -> bytes | None:
        """The loadable bytes the ELF places at addr, or None if no section holds them."""
        for sec in self.elf.iter_sections():
            if sec["sh_type"] != "SHT_PROGBITS" or not sec["sh_flags"] & 2:  # SHF_ALLOC
                continue
            start = sec["sh_addr"]
            if start <= addr and addr + size <= start + sec["sh_size"]:
                return sec.data()[addr - start : addr - start + size]
        return None

    @staticmethod
    def strip(die):
        while die.tag in ("DW_TAG_typedef", "DW_TAG_volatile_type", "DW_TAG_const_type"):
            die = die.get_DIE_from_attribute("DW_AT_type")
        return die

    def var_type(self, name):
        return self.strip(self.vars[name].get_DIE_from_attribute("DW_AT_type"))

    def element_type(self, name):
        """The variable's type, or its element type when it is an array."""
        die = self.var_type(name)
        if die.tag == "DW_TAG_array_type":
            die = self.strip(die.get_DIE_from_attribute("DW_AT_type"))
        return die

    def members(self, struct_die, base=0, out=None):
        """Flatten members to {name: (offset, size)}, descending into nested aggregates."""
        out = {} if out is None else out
        for child in self.strip(struct_die).iter_children():
            if child.tag != "DW_TAG_member":
                continue
            loc = child.attributes.get("DW_AT_data_member_location")
            off = base + (loc.value if loc else 0)
            mtype = self.strip(child.get_DIE_from_attribute("DW_AT_type"))
            if mtype.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                self.members(mtype, off, out)
            if "DW_AT_name" in child.attributes:
                size = mtype.attributes.get("DW_AT_byte_size")
                out.setdefault(
                    child.attributes["DW_AT_name"].value.decode(),
                    (off, size.value if size else 4),
                )
        return out

    def struct_size(self, name: str) -> int:
        return self.strip(self.structs[name]).attributes["DW_AT_byte_size"].value


class Image:
    """A saved memory range; every read is bounds-checked against its length."""

    def __init__(self, data: bytes, base: int, label: str):
        self.data = data
        self.base = base
        self.label = label

    def bytes(self, addr: int, n: int) -> bytes:
        off = addr - self.base
        if off < 0 or off + n > len(self.data):
            raise ToolError("address 0x%x lies outside the %s image" % (addr, self.label))
        return self.data[off : off + n]

    def uint(self, addr: int, size: int) -> int:
        return int.from_bytes(self.bytes(addr, size), "little")


def load_image(path: str, base: int, size: int, label: str) -> Image:
    """Read a capture file; a length other than the captured range is an error."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as exc:
        raise ToolError("cannot read %s: %s" % (path, exc)) from exc
    if len(data) != size:
        raise ToolError(
            "%s holds %d bytes; a complete %s capture is %d (0x%x) bytes, so the file is "
            "truncated or is not a capture of the range 0x%x-0x%x"
            % (path, len(data), label, size, size, base, base + size)
        )
    return Image(data, base, label)


def check_identity(kern: Kernel, flash: Image) -> None:
    """Fail unless the saved flash holds the ELF's `version` string bytes."""
    addr, size = kern.syms["version"]
    if size == 0:
        raise ToolError("the ELF's version symbol has no size; identity cannot be checked")
    if not (FLASH_BASE <= addr and addr + size <= flash.base + len(flash.data)):
        raise ToolError("the ELF's version string at 0x%x is outside the saved flash range" % addr)
    want = kern.elf_bytes(addr, size)
    if want is None:
        raise ToolError("no loadable ELF section holds the version string at 0x%x" % addr)
    have = flash.bytes(addr, size)
    if have != want:
        raise ToolError(
            "the ELF is not the kernel that ran: the flash holds %r at the version symbol, "
            "the ELF holds %r" % (_show(have), _show(want))
        )


def _show(raw: bytes) -> bytes:
    """The bytes up to the first NUL, for an error message."""
    return raw.split(b"\0")[0][:96]


def _text(raw: bytes) -> str:
    return raw.split(b"\0")[0].decode("ascii", errors="replace").strip()


def decode_report(kern: Kernel, sram: Image) -> list[str]:
    """The decoded kernel state as output lines."""
    out: list[str] = []
    addr, size = kern.syms["hostname"]
    out.append("hostname %r" % _text(sram.bytes(addr, size)))
    out.append(
        "time.tv_sec %d lbolt %d"
        % (sram.uint(kern.syms["time"][0], 4), sram.uint(kern.syms["lbolt"][0], 4))
    )
    out.append(
        "runin %d runout %d"
        % (sram.uint(kern.syms["runin"][0], 1), sram.uint(kern.syms["runout"][0], 1))
    )
    tty_state = None
    tx_busy = None
    if "usbttys" in kern.syms and "usbttys" in kern.vars:
        tty = kern.members(kern.element_type("usbttys"))
        off, sz = tty["t_state"]
        tty_state = sram.uint(kern.syms["usbttys"][0] + off, sz)
        out.append("usbttys[0].t_state 0x%x (%s)" % (tty_state, flag_names(tty_state, TTY_STATE)))
    if "usbd" in kern.syms and "usbd" in kern.vars:
        usbd = kern.members(kern.var_type("usbd"))
        ubase = kern.syms["usbd"][0]
        for field in USBD_FIELDS:
            if field in usbd:
                off, sz = usbd[field]
                value = sram.uint(ubase + off, sz)
                out.append("usbd.%s %d" % (field, value))
                if field == "tx_busy":
                    tx_busy = value
    for sysctl, symbol in USB_COUNTERS:
        if symbol in kern.syms:
            out.append("machdep.%s %d" % (sysctl, sram.uint(kern.syms[symbol][0], 4)))
    if tty_state is not None and tty_state & TS_BUSY and tx_busy == 0:
        out.append(
            "FLAG: usbttys[0].t_state has TS_BUSY while usbd.tx_busy is 0: the tty counts "
            "output in flight that the USB driver does not hold (the issue 569 hypothesis state)"
        )
    proc = kern.members(kern.structs["proc"])
    pbase, psize = kern.syms["proc"]
    one = kern.struct_size("proc")
    out.append("proc table: %d slots of %d bytes" % (psize // one, one))
    for i in range(psize // one):
        at = pbase + i * one

        def get(name, at=at):
            off, sz = proc[name]
            return sram.uint(at + off, sz)

        stat = get("p_stat")
        if stat == 0:
            continue
        flag = get("p_flag")
        head = "  slot %2d pid %3d stat %-6s flag 0x%04x %s" % (
            i,
            get("p_pid"),
            PROC_STAT.get(stat, str(stat)),
            flag,
            flag_names(flag, PROC_FLAGS),
        )
        if stat == SZOMB:
            # exit() overwrites the live half of p_un with the exit status and
            # resource usage (sys/kern/kern_exit.c), so a zombie has no
            # priority or wait channel to read.
            out.append(head + " wchan and pri unavailable (zombie)")
            continue
        wchan = get("P_wchan")
        pri = get("P_pri") & 0xFF
        pri = pri - 256 if pri > 127 else pri
        out.append(
            head + " wchan 0x%08x %s pri %d" % (wchan, kern.nearest(wchan) if wchan else "-", pri)
        )
    return out


def run_decode(args) -> int:
    sram = load_image(args.sram, SRAM_BASE, SRAM_SIZE, "SRAM")
    kern = Kernel(args.elf)
    try:
        kern.require(REQUIRED_SYMBOLS)
        if args.flash:
            end = kern.syms.get("__kernel_flash_end", (FLASH_END, 0))[0]
            if end not in FLASH_LAYOUT_END.values():
                raise ToolError("unsupported ELF kernel flash end %#x" % end)
            flash = load_image(args.flash, FLASH_BASE, end - FLASH_BASE, "flash")
            check_identity(kern, flash)
            print("identity: the saved flash holds this ELF's version string", file=sys.stderr)
        else:
            print(
                "warning: kernel identity unchecked (no --flash); a wrong ELF decodes to "
                "plausible garbage",
                file=sys.stderr,
            )
        for line in decode_report(kern, sram):
            print(line)
    finally:
        kern.close()
    return 0


# --- capture ----------------------------------------------------------------


@dataclass(frozen=True)
class Device:
    port: str
    product: str
    bus: str
    address: str
    name: str

    def describe(self) -> str:
        return "%s 2e8a:%s bus %s address %s %s" % (
            self.port,
            self.product,
            self.bus,
            self.address,
            self.name,
        )


def read_devices(sysfs: str) -> dict[str, Device]:
    """Every 2e8a device in the USB sysfs tree, by port."""
    try:
        names = sorted(os.listdir(sysfs))
    except OSError:
        return {}
    found = {}
    for n in names:
        if ports._sysattr(sysfs, n, "idVendor") != "2e8a":
            continue
        found[n] = Device(
            n,
            ports._sysattr(sysfs, n, "idProduct"),
            ports._sysattr(sysfs, n, "busnum"),
            ports._sysattr(sysfs, n, "devnum"),
            ports._sysattr(sysfs, n, "product"),
        )
    return found


class Capture:
    def __init__(
        self, port: str, sysfs: str, out_dir: str, resume: bool, log=None, flash_end=FLASH_END
    ):
        self.port = port
        self.sysfs = sysfs
        self.out_dir = out_dir
        self.flash_end = flash_end
        self.resume = resume
        self.log = log or (lambda text: print(text, file=sys.stderr))
        self.others_before = self.others()

    def others(self) -> dict[str, Device]:
        return {p: d for p, d in read_devices(self.sysfs).items() if p != self.port}

    def check_others(self, stage: str) -> None:
        """The 2e8a devices on the other ports keep their address and product."""
        now = self.others()
        if now != self.others_before:
            raise ToolError(
                "a 2e8a device on another USB port changed %s\n  before: %s\n  now:    %s"
                % (stage, _listing(self.others_before), _listing(now))
            )

    def target(self) -> Device:
        """The named port's device as the sysfs node holds it right now."""
        dev = read_devices(self.sysfs).get(self.port)
        if dev is None:
            raise ToolError("USB port %s holds no 2e8a device right now" % self.port)
        if not (dev.bus.isdigit() and dev.address.isdigit()):
            raise ToolError("USB port %s has no readable bus and address" % self.port)
        return dev

    def picotool(self, *argv: str) -> None:
        """picotool addressed to the named port, with the other ports checked around it."""
        self.check_others("before a picotool call")
        dev = self.target()
        cmd = ["picotool", *argv, "--bus", dev.bus, "--address", dev.address]
        self.log("discobsd-sramdump: " + " ".join(cmd))
        try:
            done = subprocess.run(
                cmd, capture_output=True, text=True, timeout=PICOTOOL_TIMEOUT, check=False
            )
        except subprocess.TimeoutExpired as exc:
            raise ToolError(
                "picotool %s did not finish in %d s" % (argv[0], PICOTOOL_TIMEOUT)
            ) from exc
        except OSError as exc:
            raise ToolError("cannot run picotool: %s" % exc) from exc
        self.check_others("during picotool %s" % argv[0])
        if done.returncode != 0:
            detail = (done.stderr or done.stdout).strip()
            raise ToolError("picotool %s failed (exit %d): %s" % (argv[0], done.returncode, detail))

    def wait_bootrom(self) -> None:
        for _ in range(BOOTROM_POLLS):
            self.check_others("while waiting for the boot ROM")
            dev = read_devices(self.sysfs).get(self.port)
            if dev is not None and dev.product == BOOTROM_PRODUCT:
                # The product id proves the enumeration; the pause lets libusb see it.
                time.sleep(SETTLE_SECONDS)
                return
            time.sleep(POLL_SECONDS)
        raise ToolError(
            "port %s did not show product %s after the reset. Keep the board powered and do "
            "not replug it or press BOOTSEL: its RAM still holds the evidence and a power "
            "cycle destroys it. Check the USB attachment (cable, hub, dmesg) and rerun. "
            "Replugging is the last resort and loses the SRAM." % (self.port, BOOTROM_PRODUCT)
        )

    def volume_sources(self) -> list[str]:
        """Block devices behind the named port's sysfs node, whole disk then partition 1.

        The boot ROM's RPI-RP2 volume shares its label with the other board's, so only
        devices under this port's own node are candidates.
        """
        devfs = os.environ.get(ports.ENV_DEVFS) or ports.DEFAULT_DEVFS
        pattern = os.path.join(self.sysfs, self.port, "*", "host*", "target*", "*", "block", "*")
        found = []
        for node in sorted(glob.glob(pattern)):
            name = os.path.basename(node)
            found += [os.path.join(devfs, name), os.path.join(devfs, name + "1")]
        return found

    def unmount_volume(self) -> None:
        """Unmount the named port's mounted boot ROM volume before the board leaves the boot ROM.

        A volume left mounted when the device reboots stays as a stale mount. A failed
        unmount stops the run before the reboot.
        """
        if shutil.which("findmnt") is None:
            self.log(
                "discobsd-sramdump: findmnt is missing; a mounted RPI-RP2 volume is not checked"
            )
            return
        for dev in self.volume_sources():
            found = _run(["findmnt", "-rn", "-S", dev, "-o", "TARGET"])
            lines = found.stdout.splitlines()
            if found.returncode != 0 or not lines:
                continue
            self.check_others("before unmounting the boot ROM volume")
            if shutil.which("udisksctl"):
                cmd = ["udisksctl", "unmount", "-b", dev]
            else:
                cmd = ["umount", lines[0]]
            self.log("discobsd-sramdump: " + " ".join(cmd))
            done = _run(cmd)
            if done.returncode != 0:
                raise ToolError(
                    "unmounting %s failed (exit %d): %s; the board was not restarted"
                    % (dev, done.returncode, (done.stderr or done.stdout).strip())
                )

    def save(self, start: int, end: int, name: str, size: int) -> str:
        path = os.path.join(self.out_dir, name)
        self.picotool("save", "-r", "0x%08x" % start, "0x%08x" % end, path)
        if not os.path.exists(path):
            raise ToolError("picotool wrote no %s" % name)
        got = os.path.getsize(path)
        if got != size:
            os.replace(path, path + ".incomplete")
            raise ToolError(
                "%s holds %d bytes, expected %d; kept as %s.incomplete" % (name, got, size, name)
            )
        return path

    def run(self) -> None:
        dev = self.target()
        if dev.product == KERNEL_PRODUCT:
            self.picotool("reboot", "-u", "-f")
            self.wait_bootrom()
        else:
            self.log(
                "discobsd-sramdump: port %s is already in the boot ROM; no reset issued" % self.port
            )
        sram = self.save(SRAM_BASE, SRAM_END, SRAM_FILE, SRAM_SIZE)
        flash = self.save(FLASH_BASE, self.flash_end, FLASH_FILE, self.flash_end - FLASH_BASE)
        if self.resume:
            self.unmount_volume()
            self.picotool("reboot")
            self.log("discobsd-sramdump: the board restarted from flash; its RAM state is gone")
        else:
            now = self.target()
            self.log(
                "discobsd-sramdump: the board stays in the boot ROM (bus %s address %s); "
                "`picotool reboot --bus %s --address %s` restarts the kernel and discards "
                "its RAM state" % (now.bus, now.address, now.bus, now.address)
            )
        self.check_others("at the end of the run")
        self.log("discobsd-sramdump: the other 2e8a devices kept their address and product")
        self.log("discobsd-sramdump: saved %s and %s" % (sram, flash))
        self.log(
            "discobsd-sramdump: next: discobsd-sramdump decode UNIX.ELF %s --flash %s"
            % (sram, flash)
        )


def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(
            cmd, capture_output=True, text=True, timeout=PICOTOOL_TIMEOUT, check=False
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ToolError("cannot run %s: %s" % (cmd[0], exc)) from exc


def _listing(devices: dict[str, Device]) -> str:
    return "; ".join(d.describe() for d in devices.values()) or "none"


def plan_text(
    dev: Device, others: dict[str, Device], out_dir: str, resume: bool, flash_end=FLASH_END
) -> list[str]:
    lines = [
        "discobsd-sramdump will capture the board at USB port %s (%s)" % (dev.port, dev.describe())
    ]
    if dev.product == KERNEL_PRODUCT:
        lines.append(
            "  1. picotool reboot -u -f --bus %s --address %s: forces the running kernel at "
            "port %s into the boot ROM, which ends its session and its console"
            % (dev.bus, dev.address, dev.port)
        )
        lines.append("  2. wait for product %s on port %s" % (BOOTROM_PRODUCT, dev.port))
    else:
        lines.append("  1. port %s already shows the boot ROM; no reset is issued" % dev.port)
    lines.append(
        "  3. picotool save -r 0x%08x 0x%08x %s"
        % (SRAM_BASE, SRAM_END, os.path.join(out_dir, SRAM_FILE))
    )
    lines.append(
        "  4. picotool save -r 0x%08x 0x%08x %s"
        % (FLASH_BASE, flash_end, os.path.join(out_dir, FLASH_FILE))
    )
    lines.append(
        "  5. %s"
        % (
            "picotool reboot: restart the kernel from flash"
            if resume
            else "leave the board in the boot ROM"
        )
    )
    lines.append("  other 2e8a devices that must not change: %s" % _listing(others))
    return lines


def run_capture(args) -> int:
    port = args.usb_port
    if port is None:
        port = os.environ.get(ports.ENV_USB_PORT) or None
    elif not port:
        raise ToolError("--usb-port needs a port such as 1-3")
    if port is None:
        print(
            "discobsd-sramdump: capture destroys the running session of one board, so it "
            "always needs the board named: --usb-port PORT or %s" % ports.ENV_USB_PORT,
            file=sys.stderr,
        )
        for dev in read_devices(_sysfs()).values():
            print("  " + dev.describe(), file=sys.stderr)
        return 2
    if not ports.USB_PORT_RE.fullmatch(port):
        raise ToolError("%s is not a USB port path such as 1-3 or 1-2.4" % port)
    sysfs = _sysfs()
    if not os.path.isdir(sysfs):
        raise ToolError("capture reads Linux sysfs; this host has none")
    devices = read_devices(sysfs)
    dev = devices.get(port)
    if dev is None:
        raise ToolError("USB port %s holds no 2e8a device" % port)
    if dev.product not in (KERNEL_PRODUCT, BOOTROM_PRODUCT):
        raise ToolError(
            "USB port %s holds 2e8a:%s, neither the kernel (%s) nor the boot ROM (%s)"
            % (port, dev.product, KERNEL_PRODUCT, BOOTROM_PRODUCT)
        )
    out_dir = args.out or datetime.now(timezone.utc).strftime("sramdump-%Y%m%dT%H%M%SZ")
    others = {p: d for p, d in devices.items() if p != port}
    flash_end = FLASH_LAYOUT_END[args.kernel_layout]
    for line in plan_text(dev, others, out_dir, args.resume, flash_end):
        print(line, file=sys.stderr)
    if not args.force_reset:
        print(
            "discobsd-sramdump: refusing without --force-reset; nothing was sent to the board",
            file=sys.stderr,
        )
        return 2
    if shutil.which("picotool") is None:
        raise ToolError("picotool is not installed (apt, pacman or brew install picotool)")
    for name in (SRAM_FILE, FLASH_FILE):
        if os.path.exists(os.path.join(out_dir, name)):
            raise ToolError(
                "%s already exists in %s; name a fresh --out directory" % (name, out_dir)
            )
    os.makedirs(out_dir, exist_ok=True)
    Capture(port, sysfs, out_dir, args.resume, flash_end=flash_end).run()
    return 0


def _sysfs() -> str:
    return os.environ.get(ports.ENV_SYSFS) or ports.DEFAULT_SYSFS


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="discobsd-sramdump",
        description="Capture a stuck board's SRAM and decode its kernel state.",
    )
    parser.add_argument("--version", action="version", version="discobsd-sramdump " + __version__)
    sub = parser.add_subparsers(dest="command", required=True)
    cap = sub.add_parser(
        "capture",
        help="force the board into the boot ROM and save its SRAM and kernel flash (Linux)",
        description="Force the board at USB port PORT into the boot ROM and save its SRAM and "
        "kernel flash. This ends the running session.",
    )
    cap.add_argument(
        "--usb-port",
        metavar="PORT",
        help="USB port of the board, such as 1-3 (default $%s); required even with one board"
        % ports.ENV_USB_PORT,
    )
    cap.add_argument(
        "--force-reset",
        action="store_true",
        help="acknowledge that the capture resets the board and ends its session",
    )
    cap.add_argument("--kernel-layout", choices=tuple(FLASH_LAYOUT_END), default="standard",
                     help="kernel flash reservation to capture (default: standard)")
    cap.add_argument("--out", metavar="DIR", help="output directory (default sramdump-<UTC time>)")
    cap.add_argument(
        "--resume",
        action="store_true",
        help="restart the kernel with picotool reboot afterwards (default: stay in the boot ROM)",
    )
    dec = sub.add_parser(
        "decode",
        help="decode a saved SRAM image against the kernel ELF that ran (no hardware)",
        description="Decode kernel state from a saved SRAM image.",
    )
    dec.add_argument("elf", metavar="ELF", help="the kernel's unix.elf, built with -g")
    dec.add_argument("sram", metavar="SRAM.BIN", help="the saved 0x20000000-0x20042000 image")
    dec.add_argument(
        "--flash",
        metavar="FLASH.BIN",
        help="the saved kernel flash image, sized for the supplied ELF",
    )
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "capture":
            return run_capture(args)
        return run_decode(args)
    except ToolError as exc:
        print("discobsd-sramdump: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
