"""gizmobsd-sramdump: decode a saved SRAM image and capture one with picotool.

Decode runs against a real kernel ELF, because the tool reads its layouts
from DWARF and a hand-made ELF would restate the tool's own assumptions. The
ELF is GIZMOBSD_TEST_KERNEL_ELF when set, else the first
sys/arch/rp2040/compile/*/unix.elf in the tree that carries the USB console
symbols; with neither, the decode cases skip with the reason. The SRAM image
is synthesized: zeros, with known values written at the ELF's symbol
addresses. The synthetic layout and the decoder share the DWARF parse, so the
independent check of that parse is a real dump compared with `ps` on the
live board (recorded with the change that added the tool), not these tests.

Capture runs the module in a subprocess against a fake sysfs and a fake
picotool on PATH. The fake changes the sysfs the way a reset does (product
0003, advanced bus address) and refuses a call whose --bus and --address
match zero devices, so a stale or wrong-port address fails the call. No real
picotool, USB device or serial node is reachable. Each rule has a mutant: the
module is copied with the rule removed and the scenario must then fail.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from gizmobsd_host import sramdump

HOST = Path(__file__).resolve().parents[1]
REPO = HOST.parents[2]
SRC = HOST / "src"

# A PATH that resolves nothing; the interpreter is started by absolute path.
NO_PATH = "/nonexistent-path-for-sramdump-tests"

# The module runs with sleeps removed: the capture loops count polls, not time.
# "python -c" puts the working directory first on sys.path, ahead of
# PYTHONPATH; under pybuild that directory is the staged build tree, whose
# gizmobsd_host would shadow a mutant copy, so the driver drops that entry.
DRIVER = (
    "import sys, time; sys.path[:] = [p for p in sys.path if p not in ('', '.')]; "
    "time.sleep = lambda s: None; "
    "from gizmobsd_host import sramdump; sys.exit(sramdump.main(sys.argv[1:]))"
)


def run_tool(args, env_extra=None, src: Path = SRC, path: str | None = None, cwd=None):
    # No PATH entry may reach the host's own picotool: the default's one entry lacks it.
    env = {"PYTHONPATH": str(src), "PATH": path or NO_PATH}
    env.update(env_extra or {})
    return subprocess.run(
        [sys.executable, "-c", DRIVER, *args],
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
        cwd=cwd,
    )


# --- name tables against the headers ---------------------------------------


def header_defines(path: Path, start: str, end: str, pattern: str) -> dict[int, str]:
    text = path.read_text()
    block = text[text.index(start) : text.index(end, text.index(start))]
    return {
        int(m.group(2), 0): m.group(1)
        for m in re.finditer(r"^#define\s+(%s)\s+(0x[0-9a-fA-F]+|\d+)L?\b" % pattern, block, re.M)
    }


@pytest.mark.skipif(
    not (REPO / "sys/sys/proc.h").is_file(), reason="the package is built outside the repository"
)
def test_name_tables_match_the_headers():
    proc = REPO / "sys/sys/proc.h"
    tty = REPO / "sys/sys/tty.h"
    stat = header_defines(proc, "/* stat codes */", "/* flag codes */", r"S[A-Z]+")
    assert stat == sramdump.PROC_STAT
    flags = header_defines(proc, "/* flag codes */", "#define S_DATA", r"[A-Z_]+")
    assert flags == {bit: name for bit, name in sramdump.PROC_FLAGS}
    state = header_defines(tty, "/* internal state bits */", "#define TS_LOCAL", r"TS_[A-Z_]+")
    assert state == {bit: name for bit, name in sramdump.TTY_STATE}


def test_flag_names_name_set_bits_and_keep_unknown_bits():
    table = sramdump.PROC_FLAGS
    assert sramdump.flag_names(0x0081, table) == "SLOAD|P_SINTR"
    assert sramdump.flag_names(0, table) == "0"
    # Bit 0x0800 is unused in proc.h; it must stay visible, not vanish.
    assert sramdump.flag_names(0x0801, table) == "SLOAD|0x800"


# --- decode -----------------------------------------------------------------


def find_kernel_elf() -> Path | None:
    override = os.environ.get("GIZMOBSD_TEST_KERNEL_ELF")
    if override:
        return Path(override)
    for cand in sorted((REPO / "sys/arch/rp2040/compile").glob("*/unix.elf")):
        try:
            kern = sramdump.Kernel(str(cand))
        except sramdump.ToolError:
            continue
        have = "usbttys" in kern.syms and "usbd" in kern.syms
        kern.close()
        if have:
            return cand
    return None


@pytest.fixture(scope="session")
def kernel_path() -> Path:
    pytest.importorskip("elftools", reason="pyelftools is not installed")
    path = find_kernel_elf()
    if path is None:
        pytest.skip(
            "no kernel ELF with the USB console symbols: build a kernel under "
            "sys/arch/rp2040/compile or set GIZMOBSD_TEST_KERNEL_ELF"
        )
    return path


@pytest.fixture(scope="session")
def kern(kernel_path):
    k = sramdump.Kernel(str(kernel_path))
    yield k
    k.close()


class Synth:
    """A zeroed SRAM image with values written at the kernel's symbol addresses."""

    def __init__(self, kern):
        self.kern = kern
        self.data = bytearray(sramdump.SRAM_SIZE)

    def put(self, addr: int, size: int, value: int) -> None:
        off = addr - sramdump.SRAM_BASE
        self.data[off : off + size] = value.to_bytes(size, "little")

    def sym(self, name: str, value: int, size: int = 4) -> None:
        self.put(self.kern.syms[name][0], size, value)

    def hostname(self, name: str) -> None:
        addr, size = self.kern.syms["hostname"]
        off = addr - sramdump.SRAM_BASE
        self.data[off : off + size] = name.encode().ljust(size, b"\0")

    def field(self, var: str, member: str, value: int, index: int = 0) -> None:
        k = self.kern
        layout = k.members(k.element_type(var))
        off, size = layout[member]
        stride = k.strip(k.element_type(var)).attributes["DW_AT_byte_size"].value
        self.put(k.syms[var][0] + index * stride + off, size, value)

    def proc(self, slot: int, pid: int, stat: int, flag: int, wchan: int, pri: int) -> None:
        k = self.kern
        layout = k.members(k.structs["proc"])
        base = k.syms["proc"][0] + slot * k.struct_size("proc")
        for name, value in (
            ("p_pid", pid),
            ("p_stat", stat),
            ("p_flag", flag),
            ("P_wchan", wchan),
            ("P_pri", pri & 0xFF),
        ):
            off, size = layout[name]
            self.put(base + off, size, value)

    def write(self, path: Path) -> Path:
        path.write_bytes(bytes(self.data))
        return path


def healthy(kern, tmp_path, tty_state=0x214, tx_busy=0) -> Synth:
    s = Synth(kern)
    s.hostname("testboard")
    s.sym("time", 1_700_000_000)
    s.sym("lbolt", 4242)
    s.sym("runout", 1, 1)
    s.field("usbttys", "t_state", tty_state)
    s.field("usbd", "configured", 1)
    s.field("usbd", "tx_busy", tx_busy)
    s.sym("usb_tx_recovered", 7)
    runout = kern.syms["runout"][0]
    tty = kern.syms["usbttys"][0]
    s.proc(0, 0, 1, 0x0003, runout, 0)
    s.proc(1, 10, 1, 0x0081, tty, 28)
    s.proc(3, 22, 3, 0x0001, 0, -5)
    return s


def flash_image(kern, mutate=None) -> bytes:
    """The flash a board running this ELF holds: the ELF's loadable sections at their addresses."""
    img = bytearray(b"\xff" * sramdump.FLASH_SIZE)
    for sec in kern.elf.iter_sections():
        addr = sec["sh_addr"]
        if sec["sh_type"] != "SHT_PROGBITS" or not sec["sh_flags"] & 2:
            continue
        if sramdump.FLASH_BASE <= addr < sramdump.FLASH_END:
            data = sec.data()[: sramdump.FLASH_END - addr]
            img[addr - sramdump.FLASH_BASE : addr - sramdump.FLASH_BASE + len(data)] = data
    if mutate:
        mutate(img)
    return bytes(img)


def decode(kernel_path, sram, flash=None, src: Path = SRC):
    args = ["decode", str(kernel_path), str(sram)]
    if flash is not None:
        args += ["--flash", str(flash)]
    return run_tool(args, src=src)


def test_decode_reports_state_names_and_wait_channels(kern, kernel_path, tmp_path):
    sram = healthy(kern, tmp_path).write(tmp_path / "sram.bin")
    r = decode(kernel_path, sram)
    assert r.returncode == 0, r.stderr
    out = r.stdout
    assert "hostname 'testboard'" in out
    assert "time.tv_sec 1700000000 lbolt 4242" in out
    assert "usbttys[0].t_state 0x214 (TS_ISOPEN|TS_CARR_ON|TS_HUPCLS)" in out
    assert "usbd.configured 1" in out
    assert "machdep.usb_tx_recovered 7" in out
    assert re.search(
        r"slot  0 pid   0 stat SSLEEP flag 0x0003 SLOAD\|SSYS "
        r"wchan 0x[0-9a-f]{8} runout\+0x0 pri 0",
        out,
    )
    assert re.search(
        r"slot  1 pid  10 stat SSLEEP flag 0x0081 SLOAD\|P_SINTR wchan 0x[0-9a-f]{8} "
        r"usbttys\+0x0 pri 28",
        out,
    )
    assert re.search(r"slot  3 pid  22 stat SRUN +flag 0x0001 SLOAD wchan 0x00000000 - pri -5", out)
    # A free slot (p_stat 0) is not listed.
    assert "slot  2" not in out
    assert "FLAG:" not in out
    assert "identity unchecked" in r.stderr


def test_decode_reads_no_live_fields_of_a_zombie(kern, kernel_path, tmp_path):
    s = healthy(kern, tmp_path)
    # Slot 4 is a zombie whose union bytes would decode as a wait channel
    # inside usbttys and a priority of 28 if they were read as live fields.
    s.proc(4, 31, 5, 0x0000, kern.syms["usbttys"][0], 28)
    r = decode(kernel_path, s.write(tmp_path / "z.bin"))
    assert r.returncode == 0, r.stderr
    line = [x for x in r.stdout.splitlines() if "pid  31" in x]
    assert len(line) == 1
    assert "stat SZOMB" in line[0]
    assert "unavailable (zombie)" in line[0]
    assert "usbttys" not in line[0]
    assert "pri 28" not in line[0]


def test_decode_flags_tty_busy_with_idle_driver(kern, kernel_path, tmp_path):
    flagged = healthy(kern, tmp_path, tty_state=0x234, tx_busy=0).write(tmp_path / "a.bin")
    r = decode(kernel_path, flagged)
    assert r.returncode == 0, r.stderr
    assert "TS_BUSY" in r.stdout.split("usbd.configured")[0]
    assert "FLAG: usbttys[0].t_state has TS_BUSY while usbd.tx_busy is 0" in r.stdout
    # Neither half alone is the combination.
    busy = healthy(kern, tmp_path, tty_state=0x234, tx_busy=1).write(tmp_path / "b.bin")
    idle = healthy(kern, tmp_path, tty_state=0x214, tx_busy=0).write(tmp_path / "c.bin")
    for p in (busy, idle):
        assert "FLAG:" not in decode(kernel_path, p).stdout


def test_decode_rejects_truncated_images(kern, kernel_path, tmp_path):
    whole = healthy(kern, tmp_path).write(tmp_path / "sram.bin").read_bytes()
    # Half an image fails the length rule and a read; sixteen missing bytes
    # past every symbol the tool reads fail the length rule alone.
    for name, cut in (("half", len(whole) // 2), ("tail", len(whole) - 16)):
        p = tmp_path / f"{name}.bin"
        p.write_bytes(whole[:cut])
        r = decode(kernel_path, p)
        assert r.returncode == 1, name
        assert "truncated" in r.stderr and r.stdout == ""
    p = tmp_path / "long.bin"
    p.write_bytes(whole + b"\0")
    assert decode(kernel_path, p).returncode == 1


def test_decode_identity(kern, kernel_path, tmp_path):
    sram = healthy(kern, tmp_path).write(tmp_path / "sram.bin")
    good = tmp_path / "flash.bin"
    good.write_bytes(flash_image(kern))
    r = decode(kernel_path, sram, good)
    assert r.returncode == 0, r.stderr
    assert "identity" in r.stderr and "unchecked" not in r.stderr
    assert "hostname 'testboard'" in r.stdout

    addr, _ = kern.syms["version"]

    def flip(img):
        img[addr - sramdump.FLASH_BASE + 12] ^= 0x01

    bad = tmp_path / "bad.bin"
    bad.write_bytes(flash_image(kern, flip))
    r = decode(kernel_path, sram, bad)
    assert r.returncode == 1
    assert "not the kernel that ran" in r.stderr
    assert r.stdout == ""
    # An erased flash is a wrong image too.
    blank = tmp_path / "blank.bin"
    blank.write_bytes(b"\xff" * sramdump.FLASH_SIZE)
    assert decode(kernel_path, sram, blank).returncode == 1
    # A short flash image is rejected before the comparison.
    short = tmp_path / "short.bin"
    short.write_bytes(good.read_bytes()[:-1])
    r = decode(kernel_path, sram, short)
    assert r.returncode == 1 and "truncated" in r.stderr


def other_kernel_elf(kern, kernel_path) -> Path | None:
    """A second ELF in the tree whose version string differs from kern's."""
    addr, size = kern.syms["version"]
    mine = kern.elf_bytes(addr, size)
    pinned = os.environ.get("GIZMOBSD_TEST_OTHER_KERNEL_ELF")
    candidates = (
        [Path(pinned)] if pinned else sorted((REPO / "sys/arch/rp2040/compile").glob("*/unix.elf"))
    )
    for cand in candidates:
        if cand.resolve() == kernel_path.resolve():
            continue
        try:
            other = sramdump.Kernel(str(cand))
        except sramdump.ToolError:
            continue
        a, s = other.syms.get("version", (0, 0))
        differs = s and other.elf_bytes(a, s) != mine
        other.close()
        if differs:
            return cand
    return None


def test_decode_refuses_a_different_kernel_elf(kern, kernel_path, tmp_path):
    other = other_kernel_elf(kern, kernel_path)
    if other is None:
        pytest.skip("the tree holds no second kernel ELF with a different version string")
    sram = healthy(kern, tmp_path).write(tmp_path / "sram.bin")
    flash = tmp_path / "flash.bin"
    flash.write_bytes(flash_image(kern))
    r = decode(other, sram, flash)
    assert r.returncode == 1 and "not the kernel that ran" in r.stderr and r.stdout == ""


def test_decode_without_a_kernel_symbol_set_fails_cleanly(tmp_path):
    pytest.importorskip("elftools", reason="pyelftools is not installed")
    junk = tmp_path / "junk.elf"
    junk.write_bytes(b"not an elf")
    sram = tmp_path / "sram.bin"
    sram.write_bytes(bytes(sramdump.SRAM_SIZE))
    r = decode(junk, sram)
    assert r.returncode == 1 and "not a readable ELF" in r.stderr


def test_decode_names_the_missing_dependency(tmp_path, monkeypatch, capsys):
    # With pyelftools hidden, the tool says how to install it and exits 1.
    monkeypatch.setitem(sys.modules, "elftools", None)
    monkeypatch.setitem(sys.modules, "elftools.elf", None)
    monkeypatch.setitem(sys.modules, "elftools.elf.elffile", None)
    sram = tmp_path / "sram.bin"
    sram.write_bytes(bytes(sramdump.SRAM_SIZE))
    assert sramdump.main(["decode", str(tmp_path / "x.elf"), str(sram)]) == 1
    assert "pip install pyelftools" in capsys.readouterr().err


# --- capture ----------------------------------------------------------------

FAKE_PICOTOOL = """#!{python}
import os, sys
root = os.environ["GIZMOBSD_SYSFS_USB"]
args = sys.argv[1:]
bus = addr = None
for i, a in enumerate(args):
    if a == "--bus":
        bus = args[i + 1]
    if a == "--address":
        addr = args[i + 1]


def attr(d, name):
    with open(os.path.join(root, d, name)) as f:
        return f.read().strip()


def put(d, name, value):
    with open(os.path.join(root, d, name), "w") as f:
        f.write(str(value) + "\\n")


dev = None
for d in sorted(os.listdir(root)):
    if not os.path.exists(os.path.join(root, d, "idVendor")):
        continue
    if attr(d, "idVendor") != "2e8a":
        continue
    if bus is None or (attr(d, "busnum") == bus and attr(d, "devnum") == addr):
        dev = d
        break
with open(os.environ["PT_LOG"], "a") as log:
    log.write(" ".join(args) + " => " + (dev or "none") + "\\n")
if dev is None or bus is None:
    sys.exit(1)
cmd = args[0]
if cmd == "reboot" and "-u" in args:
    if not os.environ.get("PT_NO_BOOTSEL"):
        put(dev, "idProduct", "0003")
        put(dev, "devnum", int(attr(dev, "devnum")) + 1)
    if os.environ.get("PT_BUMP_OTHER") == "reset":
        for d in os.listdir(root):
            if d != dev and os.path.exists(os.path.join(root, d, "idVendor")):
                put(d, "devnum", 99)
elif cmd == "reboot":
    if attr(dev, "idProduct") != "0003":
        sys.exit(1)
    put(dev, "idProduct", "000a")
    put(dev, "devnum", int(attr(dev, "devnum")) + 1)
elif cmd == "save":
    if attr(dev, "idProduct") != "0003":
        sys.exit(1)
    start, end, path = int(args[2], 0), int(args[3], 0), args[4]
    size = end - start
    if os.environ.get("PT_SHORT") == os.path.basename(path):
        size -= 1
    with open(path, "wb") as f:
        f.write(b"\\xa5" * size)
"""


STUB_FINDMNT = """#!/bin/sh
# The boot ROM volume of the board behind sdz and of the other board behind sdy.
[ -n "${FAKE_MOUNT:-}" ] || exit 1
case "$3" in */sdz1 | */sdy1) echo /mnt/boot ;; *) exit 1 ;; esac
"""


def make_world(tmp: Path, devices=None, with_picotool=True, volumes=None, udisks=True):
    """Fake sysfs with {port: (product, bus, address)}, and a bin dir with the fake picotool."""
    devices = devices or {"1-3": ("000a", 1, 7), "1-4": ("000a", 2, 9)}
    sysfs = tmp / "sys"
    bindir = tmp / "bin"
    sysfs.mkdir()
    bindir.mkdir()
    for port, (product, bus, addr) in devices.items():
        node = sysfs / port
        node.mkdir()
        (node / "idVendor").write_text("2e8a\n")
        (node / "idProduct").write_text(product + "\n")
        (node / "busnum").write_text(f"{bus}\n")
        (node / "devnum").write_text(f"{addr}\n")
        (node / "product").write_text(f"board {port}\n")
    # An unrelated device must not count as one of ours.
    (sysfs / "1-1").mkdir()
    (sysfs / "1-1" / "idVendor").write_text("046d\n")
    # volumes: {port: disk name}, a block node under the port's USB interface.
    for port, disk in (volumes or {}).items():
        (sysfs / port / f"{port}:1.0" / "host0" / "target0:0:0" / "0:0:0:0" / "block" / disk).mkdir(
            parents=True
        )
    stubs = {"findmnt": STUB_FINDMNT, "umount": '#!/bin/sh\necho "umount $*" >> "$PT_LOG"\n'}
    if udisks:
        stubs["udisksctl"] = '#!/bin/sh\necho "udisksctl $*" >> "$PT_LOG"\n[ -z "${UD_FAIL:-}" ]\n'
    for name, text in stubs.items():
        (bindir / name).write_text(text)
        (bindir / name).chmod(0o755)
    if with_picotool:
        tool = bindir / "picotool"
        tool.write_text(FAKE_PICOTOOL.format(python=sys.executable))
        tool.chmod(0o755)
    return sysfs, bindir


def capture(
    tmp,
    args,
    devices=None,
    env=None,
    src: Path = SRC,
    with_picotool=True,
    volumes=None,
    udisks=True,
):
    sysfs, bindir = make_world(tmp, devices, with_picotool, volumes, udisks)
    out = tmp / "out"
    full_env = {
        "GIZMOBSD_SYSFS_USB": str(sysfs),
        "GIZMOBSD_DEVFS": str(tmp / "dev"),
        "PT_LOG": str(tmp / "picotool.log"),
    }
    full_env.update(env or {})
    argv = ["capture", *args]
    if "--out" not in argv:
        argv += ["--out", str(out)]
    r = run_tool(argv, full_env, src=src, path=str(bindir))
    return r, sysfs, out


def picotool_log(tmp: Path) -> list[str]:
    p = tmp / "picotool.log"
    return p.read_text().splitlines() if p.exists() else []


def scenario_capture_saves_both_regions(src: Path, tmp: Path) -> None:
    r, sysfs, out = capture(tmp, ["--usb-port", "1-3", "--force-reset"], src=src)
    assert r.returncode == 0, r.stderr
    # The save calls carry the address the reset left, not the one before it.
    log = picotool_log(tmp)
    assert log[0] == "reboot -u -f --bus 1 --address 7 => 1-3"
    assert re.match(r"save -r 0x20000000 0x20042000 /(proc/self|dev)/fd/", log[1])
    assert log[1].endswith("/sram.bin --bus 1 --address 8 => 1-3")
    assert re.match(r"save -r 0x10000000 0x10020000 /(proc/self|dev)/fd/", log[2])
    assert log[2].endswith("/flash.bin --bus 1 --address 8 => 1-3")
    assert (out / "sram.bin").stat().st_size == 0x42000
    assert (out / "flash.bin").stat().st_size == 0x20000
    assert out.stat().st_mode & 0o777 == 0o700
    assert (out / "sram.bin").stat().st_mode & 0o777 == 0o600
    assert (out / "flash.bin").stat().st_mode & 0o777 == 0o600
    assert (sysfs / "1-3" / "idProduct").read_text().strip() == "0003"
    assert (sysfs / "1-4" / "devnum").read_text().strip() == "9"
    assert "stays in the boot ROM" in r.stderr and "kept their address" in r.stderr


def scenario_capture_resume(src: Path, tmp: Path) -> None:
    r, sysfs, _ = capture(tmp, ["--usb-port", "1-3", "--force-reset", "--resume"], src=src)
    assert r.returncode == 0, r.stderr
    assert picotool_log(tmp)[-1] == "reboot --bus 1 --address 8 => 1-3"
    assert (sysfs / "1-3" / "idProduct").read_text().strip() == "000a"


def scenario_capture_resume_unmounts_the_named_volume(src: Path, tmp: Path) -> None:
    volumes = {"1-3": "sdz", "1-4": "sdy"}
    r, sysfs, _ = capture(
        tmp,
        ["--usb-port", "1-3", "--force-reset", "--resume"],
        env={"FAKE_MOUNT": "1"},
        volumes=volumes,
        src=src,
    )
    assert r.returncode == 0, r.stderr
    log = picotool_log(tmp)
    # Only the named port's volume, after both saves and before the reboot.
    assert log[-2:] == [f"udisksctl unmount -b {tmp}/dev/sdz1", "reboot --bus 1 --address 8 => 1-3"]
    assert sum(line.startswith("udisksctl") for line in log) == 1
    # A failed unmount stops the run before the board leaves the boot ROM.
    (tmp / "f").mkdir()
    r, sysfs, _ = capture(
        tmp / "f",
        ["--usb-port", "1-3", "--force-reset", "--resume"],
        env={"FAKE_MOUNT": "1", "UD_FAIL": "1"},
        volumes=volumes,
        src=src,
    )
    assert r.returncode == 1 and "unmounting" in r.stderr
    assert not any(line.startswith("reboot --bus") for line in picotool_log(tmp / "f"))
    assert (sysfs / "1-3" / "idProduct").read_text().strip() == "0003"
    # Without udisksctl the mount point goes to umount.
    (tmp / "u").mkdir()
    r, _, _ = capture(
        tmp / "u",
        ["--usb-port", "1-3", "--force-reset", "--resume"],
        env={"FAKE_MOUNT": "1"},
        volumes=volumes,
        udisks=False,
        src=src,
    )
    assert r.returncode == 0, r.stderr
    assert picotool_log(tmp / "u")[-2] == "umount /mnt/boot"
    # Nothing mounted: no unmount call at all.
    (tmp / "n").mkdir()
    r, _, _ = capture(
        tmp / "n", ["--usb-port", "1-3", "--force-reset", "--resume"], volumes=volumes, src=src
    )
    assert r.returncode == 0, r.stderr
    assert not any(line.split()[0].endswith("mount") for line in picotool_log(tmp / "n"))


def scenario_capture_needs_the_port(src: Path, tmp: Path) -> None:
    # One board attached: still refused, and nothing reaches picotool.
    devices = {"1-3": ("000a", 1, 7)}
    r, _, out = capture(tmp, ["--force-reset"], devices=devices, src=src)
    assert r.returncode == 2, r.stderr
    assert picotool_log(tmp) == [] and not out.exists()
    assert "--usb-port" in r.stderr
    # An empty environment variable counts as unset; an empty operand is an error.
    (tmp / "b").mkdir()
    r, _, _ = capture(
        tmp / "b", ["--force-reset"], devices=devices, env={"GIZMOBSD_USB_PORT": ""}, src=src
    )
    assert r.returncode == 2 and picotool_log(tmp / "b") == []
    (tmp / "c").mkdir()
    r, _, _ = capture(tmp / "c", ["--usb-port", "", "--force-reset"], devices=devices, src=src)
    assert r.returncode == 1 and picotool_log(tmp / "c") == []
    # The variable names the board when it is set.
    (tmp / "d").mkdir()
    r, _, _ = capture(tmp / "d", ["--force-reset"], env={"GIZMOBSD_USB_PORT": "1-4"}, src=src)
    assert r.returncode == 0, r.stderr
    assert picotool_log(tmp / "d")[0] == "reboot -u -f --bus 2 --address 9 => 1-4"


def scenario_capture_needs_force_reset(src: Path, tmp: Path) -> None:
    r, _, out = capture(tmp, ["--usb-port", "1-3"], src=src)
    assert r.returncode == 2, r.stderr
    assert picotool_log(tmp) == [] and not out.exists()
    # The refusal says exactly what would have happened, to which board.
    assert "USB port 1-3" in r.stderr
    assert "picotool reboot -u -f --bus 1 --address 7" in r.stderr
    assert "0x20000000 0x20042000" in r.stderr and "0x10000000 0x10020000" in r.stderr
    assert "refusing without --force-reset" in r.stderr


def scenario_capture_stops_when_another_board_moves(src: Path, tmp: Path) -> None:
    r, _, out = capture(
        tmp,
        ["--usb-port", "1-3", "--force-reset"],
        env={"PT_BUMP_OTHER": "reset"},
        src=src,
    )
    assert r.returncode == 1, r.stderr
    # The reset is the only call; no save follows a changed neighbor.
    assert picotool_log(tmp) == ["reboot -u -f --bus 1 --address 7 => 1-3"]
    assert "another USB port changed" in r.stderr and "1-4" in r.stderr
    assert not (out / "sram.bin").exists()


def scenario_capture_waits_for_the_boot_rom(src: Path, tmp: Path) -> None:
    r, _, _ = capture(
        tmp, ["--usb-port", "1-3", "--force-reset"], env={"PT_NO_BOOTSEL": "1"}, src=src
    )
    assert r.returncode == 1
    assert picotool_log(tmp) == ["reboot -u -f --bus 1 --address 7 => 1-3"]
    assert "did not show product 0003" in r.stderr
    # Replugging power-cycles the board and destroys the SRAM the run exists to keep.
    assert "do not replug it" in r.stderr and "loses the SRAM" in r.stderr
    assert "hold BOOTSEL" not in r.stderr


def scenario_capture_from_the_boot_rom(src: Path, tmp: Path) -> None:
    devices = {"1-3": ("0003", 1, 7), "1-4": ("000a", 2, 9)}
    r, _, out = capture(tmp, ["--usb-port", "1-3", "--force-reset"], devices=devices, src=src)
    assert r.returncode == 0, r.stderr
    assert [line.split()[0] for line in picotool_log(tmp)] == ["save", "save"]
    assert "no reset issued" in r.stderr and (out / "sram.bin").exists()


def scenario_capture_rejects_a_short_save(src: Path, tmp: Path) -> None:
    r, _, out = capture(
        tmp,
        ["--usb-port", "1-3", "--force-reset"],
        env={"PT_SHORT": "sram.bin"},
        src=src,
    )
    assert r.returncode == 1
    assert len(picotool_log(tmp)) == 2
    assert (out / "sram.bin.incomplete").exists() and not (out / "sram.bin").exists()


def scenario_capture_rejects_bad_ports(src: Path, tmp: Path) -> None:
    for i, port in enumerate(("1-9", "1-1", "../1-3", "usb1")):
        d = tmp / str(i)
        d.mkdir()
        r, _, _ = capture(d, ["--usb-port", port, "--force-reset"], src=src)
        assert r.returncode == 1, port
        assert picotool_log(d) == [], port
    # A device that is neither the kernel nor the boot ROM is left alone.
    (tmp / "p").mkdir()
    r, _, _ = capture(
        tmp / "p",
        ["--usb-port", "1-3", "--force-reset"],
        devices={"1-3": ("0009", 1, 7)},
        src=src,
    )
    assert r.returncode == 1 and picotool_log(tmp / "p") == []


def scenario_capture_keeps_earlier_files(src: Path, tmp: Path) -> None:
    out = tmp / "out"
    out.mkdir()
    (out / "sram.bin").write_bytes(b"earlier")
    r, _, _ = capture(tmp, ["--usb-port", "1-3", "--force-reset", "--out", str(out)], src=src)
    assert r.returncode == 1 and picotool_log(tmp) == []
    assert (out / "sram.bin").read_bytes() == b"earlier"


def scenario_capture_needs_picotool(src: Path, tmp: Path) -> None:
    r, _, _ = capture(tmp, ["--usb-port", "1-3", "--force-reset"], src=src, with_picotool=False)
    assert r.returncode == 1 and "picotool is not installed" in r.stderr


def scenario_decode_rejects_a_tail_cut(src: Path, tmp: Path) -> None:
    # Sixteen bytes missing at the end, past every symbol decode reads.
    p = tmp / "sram.bin"
    p.write_bytes(bytes(sramdump.SRAM_SIZE - 16))
    r = run_tool(["decode", "/nonexistent.elf", str(p)], src=src)
    assert r.returncode == 1 and "truncated" in r.stderr


def scenario_decode_checks_identity(src: Path, tmp: Path) -> None:
    kernel_path = find_kernel_elf()
    k = sramdump.Kernel(str(kernel_path))
    try:
        sram = healthy(k, tmp).write(tmp / "sram.bin")
        addr, _ = k.syms["version"]

        def flip(img):
            img[addr - sramdump.FLASH_BASE + 12] ^= 0x01

        bad = tmp / "bad.bin"
        bad.write_bytes(flash_image(k, flip))
    finally:
        k.close()
    r = decode(kernel_path, sram, bad, src=src)
    assert r.returncode == 1 and "not the kernel that ran" in r.stderr and r.stdout == ""


CAPTURE_SCENARIOS = {
    "saves": scenario_capture_saves_both_regions,
    "resume": scenario_capture_resume,
    "unmount": scenario_capture_resume_unmounts_the_named_volume,
    "port": scenario_capture_needs_the_port,
    "force": scenario_capture_needs_force_reset,
    "neighbor": scenario_capture_stops_when_another_board_moves,
    "wait": scenario_capture_waits_for_the_boot_rom,
    "bootrom": scenario_capture_from_the_boot_rom,
    "short": scenario_capture_rejects_a_short_save,
    "ports": scenario_capture_rejects_bad_ports,
    "overwrite": scenario_capture_keeps_earlier_files,
    "picotool": scenario_capture_needs_picotool,
}

capture_only = pytest.mark.skipif(sys.platform == "win32", reason="shebang stub and POSIX paths")


@capture_only
@pytest.mark.parametrize("name", sorted(CAPTURE_SCENARIOS))
def test_capture_scenario(name, tmp_path):
    CAPTURE_SCENARIOS[name](SRC, tmp_path)


# Each mutant removes one rule from a copy of the module; its scenario must
# then fail. The anchor must occur in the module, or the mutant tests nothing.
MUTANTS = {
    "address from the other port": (
        "        dev = read_devices(self.sysfs).get(self.port)\n        if dev is None:\n"
        '            raise ToolError("USB port %s holds no 2e8a device right now"',
        "        dev = read_devices(self.sysfs).get(sorted(self.others())[0])\n"
        "        if dev is None:\n"
        '            raise ToolError("USB port %s holds no 2e8a device right now"',
        scenario_capture_saves_both_regions,
    ),
    "address resolved once": (
        "        dev = self.target()\n        cmd = ",
        '        dev = self.__dict__.setdefault("_dev", self.target())\n        cmd = ',
        scenario_capture_saves_both_regions,
    ),
    "volume left mounted": (
        "            self.unmount_volume()\n",
        "            pass\n",
        scenario_capture_resume_unmounts_the_named_volume,
    ),
    "every port's volume": (
        'os.path.join(self.sysfs, self.port, "*", "host*"',
        'os.path.join(self.sysfs, "*", "*", "host*"',
        scenario_capture_resume_unmounts_the_named_volume,
    ),
    "failed unmount ignored": (
        "            if done.returncode != 0:\n                raise ToolError(\n"
        '                    "unmounting',
        '            if False:\n                raise ToolError(\n                    "unmounting',
        scenario_capture_resume_unmounts_the_named_volume,
    ),
    "replug advice": (
        "not replug it or press BOOTSEL",
        "hold BOOTSEL through a replug",
        scenario_capture_waits_for_the_boot_rom,
    ),
    "other boards unchecked": (
        "        if now != self.others_before:",
        "        if False:",
        scenario_capture_stops_when_another_board_moves,
    ),
    "force-reset not required": (
        "    if not args.force_reset:",
        "    if False:",
        scenario_capture_needs_force_reset,
    ),
    "unnamed port accepted": (
        "    if port is None:\n        print(",
        "    if port is None and False:\n        print(",
        scenario_capture_needs_the_port,
    ),
    "short save accepted": (
        "        if got != size:",
        "        if False:",
        scenario_capture_rejects_a_short_save,
    ),
    "truncated image accepted": (
        "    if len(data) != size:",
        "    if False:",
        scenario_decode_rejects_a_tail_cut,
    ),
    "identity check skipped": (
        "            check_identity(kern, flash)\n",
        "            pass\n",
        scenario_decode_checks_identity,
    ),
}


@capture_only
def test_expanded_sd_capture(tmp_path):
    result, _, output = capture(
        tmp_path, ["--usb-port", "1-3", "--force-reset", "--kernel-layout", "picocalc-sd"]
    )
    assert result.returncode == 0, result.stderr
    assert "save -r 0x10000000 0x10028000" in "\n".join(picotool_log(tmp_path))
    assert (output / "flash.bin").stat().st_size == 160 * 1024


@capture_only
@pytest.mark.parametrize("name", sorted(MUTANTS))
def test_mutant_is_caught(name, tmp_path):
    anchor, replacement, scenario = MUTANTS[name]
    if scenario is scenario_decode_checks_identity:
        pytest.importorskip("elftools", reason="pyelftools is not installed")
        if find_kernel_elf() is None:
            pytest.skip("no kernel ELF for the decode mutant")
    pkg = tmp_path / "mutant" / "gizmobsd_host"
    shutil.copytree(SRC / "gizmobsd_host", pkg, ignore=shutil.ignore_patterns("__pycache__"))
    target = pkg / "sramdump.py"
    text = target.read_text()
    assert anchor in text, f"mutant anchor missing from sramdump.py: {anchor!r}"
    target.write_text(text.replace(anchor, replacement, 1))
    work = tmp_path / "run"
    work.mkdir()
    with pytest.raises(AssertionError):
        scenario(tmp_path / "mutant", work)
    # The unmutated module passes the same scenario, so the failure is the mutant's.
    good = tmp_path / "good"
    good.mkdir()
    scenario(SRC, good)
