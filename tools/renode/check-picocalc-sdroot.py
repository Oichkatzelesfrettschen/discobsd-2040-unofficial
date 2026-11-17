#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Boot the stamped PICOCALC kernel with its root on the SD card.

The loader programs the kernel's UF2, stamps vector entries 4 to 10 (magic
0xe98cc638, flash end 0x101fc000, the first 20 bytes of the UF2 path) and
lends the flash above the kernel to other applications. sdrootattach() in
sys/arch/rp2040/dev/sd.c then takes the root from the card's first MBR
partition of type b7. This driver tests that SD-root path on a stamped kernel
under Renode: the kernel image carries the stamp, the flash root region holds
a foreign byte pattern in place of a Dhara journal, and the SD card model
holds a card laid out as the owner's PicoCalc card is: a FAT32 entry, then
the b7 entry in slot 3, then a Linux entry in slot 2 at the card's end.

The image keeps the kernel's own second stage in flash bytes 0-255, where the
loader installs its own. The loader's second stage, the XIP exit and
re-entry through it, and flash_ssi_restore() after it are therefore outside
this gate: it is a stamped-kernel test, not a loader-state test.

Cases, each a fresh emulator:
  sdroot    a 40 MiB card: the stamped kernel prints the two root lines,
            the card line and root dev (5,3), reaches login on the panel,
            a keyboard login runs mount, which names /dev/sd0c on /, the
            flash root region still holds the foreign pattern, and UART0
            carries the tty0 login prompt; root_verdict() judges the root
            device, the mount source and the flash samples
  tmp       a 48 MiB card with an 8 MiB type-ba entry in slot 4: rc makes
            a file system on /dev/sd0t and `mount` shows it on /tmp, where
            mkfs's lost+found and a written file are visible; the sdroot
            case above shows no /tmp mount for a card without the entry
  nob7      the same card with its b7 entry retyped 83 stops with the
            panic line naming the missing partition

Renode's SD card model answers CMD58 without CCS at every capacity and
block size (measured with Renode 1.17.0+20260907gitf1dd1b4af: an 8 GiB
card, with and without blockSize Block512, reads "SD 2.0"), so the card
here is standard-capacity and byte-addressed. The high-capacity path a
32 GB card takes, CCS set and the sector number as the command argument,
is tests/picocalc/sdspi_test.c's sdhc scenario.

The unstamped kernel's flash root is check-picocalc.sh's case and is not
repeated here.

Before Renode starts, self_test() runs root_verdict() on the good
observations and on altered root device, mount source and flash samples,
and the gate stops with status 1 unless the good ones pass and every altered
one fails.

Usage: check-picocalc-sdroot.py RENODE KERNEL SDROOT_IMG OUTPUT_DIR
       check-picocalc-sdroot.py --self-test
KERNEL is a flat unix.bin or a kernel UF2 such as the board image
distrib/rp2040/unix-PICOCALC.uf2 that goes on the card.
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from renode_monitor import Renode, RenodeError, monitor_int  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    "check_picocalc", os.path.join(HERE, "check-picocalc.py")
)
if _spec is None or _spec.loader is None:
    raise ImportError("check-picocalc.py")
cp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cp)

LOADER_STAMP = struct.pack("<2I", 0xE98CC638, 0x101FC000) + b"/pico1-apps/discobsd"
STAMP_OFFSET = 0x110
FLASH_ROOT_BYTES = 1536 * 1024
FOREIGN = 0xA5
# Sampled words of the flash root region (0x10020000 up to fl1 at
# 0x101a0000): its first and last word and one in each 256 KiB.
FOREIGN_SAMPLES = [0x10020000 + i * 0x40000 for i in range(6)] + [0x1019FFFC]

SECTOR = 512
# Card layouts: total sectors and the start of the b7 entry. The FAT32
# entry runs from sector 2048 up to the root and the Linux entry, 2048
# sectors, follows it, as on the owner's card.
LAYOUTS = {
    "small": (81920, 8192, "sd0: SD 2.0, 40960 kbytes"),
    # The same card with an 8 MiB type-ba /tmp entry in slot 4 after the
    # Linux entry (PTYPE_TMP, sys/sys/disk.h).
    "tmp": (98304, 8192, "sd0: SD 2.0, 49152 kbytes"),
}
TMP_TYPE = 0xBA
TMP_SECTORS = 16384
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)

# Virtual seconds for the card stage: SPI at 398 kHz through
# identification, then sector reads of the table and the superblock.
SD_BUDGET_SECONDS = 60.0

# Every PicoCalc kernel; the configuration name in parentheses differs.
BANNER = "DiscoBSD 2.7 (PICOCALC"
ROOT_LINES = ["root: started by the PicoCalc loader, root on sd0", "root: sd0c"]
NO_B7_LINE = "panic: root: no partition of type b7 on sd0"


ROOT_DEV = (5, 3)
ROOT_SOURCE = "/dev/sd0c"
ROOT_DEV_RE = re.compile(r"root dev\s*=\s*\((\d+),(\d+)\)")
ROOT_MOUNT_RE = re.compile(r"^(/dev/\S+) on /(?:\s|$)")


def parse_root_dev(lines):
    """The (major, minor) the kernel prints as `root dev = (M,m)`, or None."""
    for line in lines:
        found = ROOT_DEV_RE.search(line)
        if found:
            return int(found.group(1)), int(found.group(2))
    return None


def parse_mount_root(lines):
    """The device that `mount` shows on /, or None."""
    for line in lines:
        found = ROOT_MOUNT_RE.match(line.strip())
        if found:
            return found.group(1)
    return None


TMP_SOURCE = "/dev/sd0t"
TMP_MOUNT_RE = re.compile(r"^(/dev/\S+) on /tmp(?:\s|$)")


def parse_mount_tmp(lines):
    """The device that `mount` shows on /tmp, or None."""
    for line in lines:
        found = TMP_MOUNT_RE.match(line.strip())
        if found:
            return found.group(1)
    return None


def tmp_verdict(mount_source, want):
    """Failures in the /tmp observation; an empty list is a pass.

    want is TMP_SOURCE for a card with a type-ba entry and None for a card
    without one, where /tmp stays a directory of the root (no PSRAM answers
    under Renode, so the PSRAM region does not take it either).
    """
    if mount_source != want:
        return [f"/tmp mounted from {mount_source}, want {want}"]
    return []


def root_verdict(root_dev, mount_source, flash_words):
    """Failures in the root observations; an empty list is a pass.

    root_dev is the (major, minor) the kernel printed, mount_source the
    device `mount` names on /, and flash_words maps each sampled address of
    the flash root region to the word read there after the boot.
    """
    failures = []
    if root_dev != ROOT_DEV:
        failures.append(f"root dev {root_dev}, want {ROOT_DEV}")
    if mount_source != ROOT_SOURCE:
        failures.append(f"/ mounted from {mount_source}, want {ROOT_SOURCE}")
    if sorted(flash_words) != sorted(FOREIGN_SAMPLES):
        failures.append("flash root samples are not the sampled addresses")
    bad = [hex(a) for a, w in sorted(flash_words.items()) if w != FOREIGN * 0x01010101]
    if bad:
        failures.append(f"flash root region rewritten at {bad}")
    return failures


def self_test():
    """Calibrate root_verdict() and the parsers; returns failure messages."""
    good_words = {a: FOREIGN * 0x01010101 for a in FOREIGN_SAMPLES}
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append(f"{label}: {got!r}, want {want!r}")

    expect("good observations", root_verdict(ROOT_DEV, ROOT_SOURCE, good_words), [])
    altered = {
        "minor of sd0b": ((5, 2), ROOT_SOURCE, good_words),
        "minor of sd0d": ((5, 4), ROOT_SOURCE, good_words),
        "major of fl0": ((4, 3), ROOT_SOURCE, good_words),
        "root dev unread": (None, ROOT_SOURCE, good_words),
        "mount source sd0b": (ROOT_DEV, "/dev/sd0b", good_words),
        "mount source fl0": (ROOT_DEV, "/dev/fl0a", good_words),
        "mount unread": (ROOT_DEV, None, good_words),
    }
    for label, (dev, source, words) in altered.items():
        if not root_verdict(dev, source, words):
            failures.append(f"{label}: accepted")
    for address in FOREIGN_SAMPLES:
        words = dict(good_words)
        words[address] ^= 0x00000100
        if not root_verdict(ROOT_DEV, ROOT_SOURCE, words):
            failures.append(f"flash word at {address:#x} changed: accepted")
        words = dict(good_words)
        del words[address]
        if not root_verdict(ROOT_DEV, ROOT_SOURCE, words):
            failures.append(f"flash word at {address:#x} missing: accepted")
    expect("parse root dev", parse_root_dev(["x", "root dev  = (5,3)"]), (5, 3))
    expect("parse root dev none", parse_root_dev(["root dev  = (5,x)"]), None)
    expect("parse mount root", parse_mount_root(["$ mount", "/dev/sd0c on / (rw)"]), "/dev/sd0c")
    expect("parse mount other mount point", parse_mount_root(["/dev/sd0c on /home"]), None)
    expect("parse mount echoed command", parse_mount_root(["$ /dev/sd0c on /"]), None)
    expect("parse tmp mount", parse_mount_tmp(["/dev/sd0t on /tmp (rw)"]), "/dev/sd0t")
    expect("parse tmp mount other point", parse_mount_tmp(["/dev/sd0t on /tmpx"]), None)
    expect("parse tmp mount echoed command", parse_mount_tmp(["$ /dev/sd0t on /tmp"]), None)
    expect("tmp on the card", tmp_verdict("/dev/sd0t", TMP_SOURCE), [])
    expect("no tmp entry, none mounted", tmp_verdict(None, None), [])
    for label, got, want in (
        ("tmp on sd0b", "/dev/sd0b", TMP_SOURCE),
        ("tmp on the PSRAM", "/dev/psram1", TMP_SOURCE),
        ("tmp unread", None, TMP_SOURCE),
        ("tmp mounted without an entry", "/dev/sd0t", None),
    ):
        if not tmp_verdict(got, want):
            failures.append(f"{label}: accepted")
    return failures


def flat_kernel(path):
    """The flash bytes from 0x10000000 of a unix.bin or a contiguous UF2."""
    data = open(path, "rb").read()
    if not path.endswith(".uf2"):
        return bytearray(data)
    image = bytearray()
    for off in range(0, len(data), 512):
        head = struct.unpack_from("<8I", data, off)
        end = struct.unpack_from("<I", data, off + 508)[0]
        if (head[0], head[1], end) != UF2_MAGIC or head[4] != 256:
            raise ValueError(f"UF2 block at {off:#x}")
        if head[3] != 0x10000000 + len(image):
            raise ValueError(f"UF2 block at {off:#x} is not contiguous")
        image += data[off + 32 : off + 32 + 256]
    return image


def stamped_kernel(kernel, path):
    image = flat_kernel(kernel)
    if len(LOADER_STAMP) != 28 or len(image) < STAMP_OFFSET + 28:
        raise ValueError("stamp or kernel size")
    image[STAMP_OFFSET : STAMP_OFFSET + 28] = LOADER_STAMP
    with open(path, "wb") as stream:
        stream.write(image)


def foreign_flash(path):
    with open(path, "wb") as stream:
        stream.write(bytes([FOREIGN]) * FLASH_ROOT_BYTES)


def mbr_entry(kind, start, count):
    return struct.pack("<B3sB3sII", 0, b"\0\0\0", kind, b"\0\0\0", start, count)


def make_card(path, root_img, p3_type, layout, tmp_entry=False):
    root = open(root_img, "rb").read()
    if len(root) % SECTOR:
        raise ValueError("root image is not whole sectors")
    sectors, p3_start = LAYOUTS[layout][:2]
    p3 = (p3_type, p3_start, len(root) // SECTOR)
    p2 = (0x83, p3_start + p3[2], 2048)
    p1 = (0x0C, 2048, p3_start - 2048)
    if p2[1] + p2[2] > sectors:
        raise ValueError("layout does not hold the root image")
    p4 = (TMP_TYPE, p2[1] + p2[2], TMP_SECTORS) if tmp_entry else None
    if p4 is not None and p4[1] + p4[2] > sectors:
        raise ValueError("layout does not hold the tmp entry")
    table = b"".join(mbr_entry(*p) for p in (p1, p2, p3)) + (
        mbr_entry(*p4) if p4 is not None else bytes(16)
    )
    with open(path, "wb") as stream:
        stream.truncate(sectors * SECTOR)
        stream.seek(446)
        stream.write(table + b"\x55\xaa")
        stream.seek(p3_start * SECTOR)
        stream.write(root)
    return sectors * SECTOR


def boot(renode_path, kernel, flash, card, capacity, outdir):
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), cp.WALL_SECONDS)
    renode.command(f"$unix_bin=@{kernel}")
    renode.command(f"$flash_bin=@{flash}")
    renode.command(f"include @{os.path.join(HERE, 'picocalc-machine.resc')}")
    renode.command(
        'machine LoadPlatformDescriptionFromString "sd: SD.SDCard @ spi0 '
        f'{{ imageFile: \\"{card}\\"; capacity: {capacity}; '
        'persistent: true; spiMode: true }"'
    )
    renode.command(f"sysbus.uart0 CreateFileBackend @{os.path.join(outdir, 'uart0.txt')} true")
    return renode, cp.Run(renode, outdir)


def case_root(renode_path, kernel, flash, root_img, outdir, layout):
    os.makedirs(outdir, exist_ok=True)
    card = os.path.join(outdir, "card.img")
    capacity = make_card(card, root_img, 0xB7, layout)
    renode, run = boot(renode_path, kernel, flash, card, capacity, outdir)
    try:
        # The banner scrolls off the 32-row panel before the card answers,
        # so the waits run one after the other.
        run.wait_for([BANNER], "banner", cp.BOOT_BUDGET_SECONDS)
        run.wait_for(ROOT_LINES + [LAYOUTS[layout][2]], "root lines", SD_BUDGET_SECONDS)
        run.snapshot("root")
        run.wait_until(
            lambda: parse_root_dev(run.lines) is not None,
            "root device line",
            "a root dev line",
            SD_BUDGET_SECONDS,
        )
        root_dev = parse_root_dev(run.lines)
        run.wait_for(["login:"], "login prompt", cp.LOGIN_BUDGET_SECONDS)
        run.type_line("operator")
        run.wait_for_prompt("shell prompt", cp.KEY_BUDGET_SECONDS)
        run.type_line("mount")
        run.wait_until(
            lambda: parse_mount_root(run.lines) is not None,
            "mount root line",
            "a mount line for /",
            cp.KEY_BUDGET_SECONDS,
        )
        mount_source = parse_mount_root(run.lines)
        run.snapshot("mount")
        for message in tmp_verdict(parse_mount_tmp(run.lines), None):
            run.fail(message)
        flash_words = {
            a: monitor_int(renode, f"sysbus ReadDoubleWord {a:#x}") for a in FOREIGN_SAMPLES
        }
        bad = root_verdict(root_dev, mount_source, flash_words)
        for message in bad:
            run.fail(message)
        if not bad:
            print(
                f"check-picocalc-sdroot: root dev {root_dev}, / from {mount_source}, "
                f"flash root region intact at {len(FOREIGN_SAMPLES)} sampled words"
            )
        # The SD root's /etc/ttys runs a getty on tty0, UART0, which the
        # PicoCalc's USB-C port carries.
        with open(os.path.join(outdir, "uart0.txt"), "rb") as stream:
            uart = stream.read()
        run.expect(b"login:" in uart, "tty0: no login prompt on UART0")
        if b"login:" in uart:
            print("check-picocalc-sdroot: login prompt on tty0 (UART0)")
    except cp.StageFailed:
        pass
    finally:
        renode.close()
    return run.failures


def case_tmp(renode_path, kernel, flash, root_img, outdir):
    """The card holds a type-ba entry in slot 4: /etc/rc.rp2040 makes a file
    system on /dev/sd0t and mounts it at /tmp, which then holds files.
    sdswapattach() finds no PSRAM to widen here (the Renode model has none),
    so the swap size on the PSRAM stays unobserved."""
    os.makedirs(outdir, exist_ok=True)
    card = os.path.join(outdir, "card.img")
    capacity = make_card(card, root_img, 0xB7, "tmp", tmp_entry=True)
    renode, run = boot(renode_path, kernel, flash, card, capacity, outdir)
    try:
        run.wait_for([BANNER], "banner", cp.BOOT_BUDGET_SECONDS)
        run.wait_for(ROOT_LINES + [LAYOUTS["tmp"][2]], "root lines", SD_BUDGET_SECONDS)
        run.wait_for(["login:"], "login prompt", cp.LOGIN_BUDGET_SECONDS)
        run.type_line("operator")
        run.wait_for_prompt("shell prompt", cp.KEY_BUDGET_SECONDS)
        run.type_line("mount")
        run.wait_until(
            lambda: parse_mount_root(run.lines) is not None,
            "mount root line",
            "a mount line for /",
            cp.KEY_BUDGET_SECONDS,
        )
        run.snapshot("mount")
        tmp_source = parse_mount_tmp(run.lines)
        bad = tmp_verdict(tmp_source, TMP_SOURCE)
        for message in bad:
            run.fail(message)
        if not bad:
            print(f"check-picocalc-sdroot: /tmp from {tmp_source}")
        # The new file system is usable: mkfs's lost+found is there and a
        # file written through the mount reads back.
        run.type_line("ls /tmp")
        run.wait_until(
            lambda: any(line.strip() == "lost+found" for line in run.lines),
            "lost+found in /tmp",
            "a lost+found line",
            cp.KEY_BUDGET_SECONDS,
        )
        run.type_line("echo tmpok > /tmp/probe")
        run.wait_for_prompt("prompt after the write", cp.KEY_BUDGET_SECONDS)
        run.type_line("cat /tmp/probe")
        run.wait_until(
            lambda: any(line.strip() == "tmpok" for line in run.lines),
            "the file read back",
            "a tmpok line",
            cp.KEY_BUDGET_SECONDS,
        )
        run.snapshot("tmp")
        print("check-picocalc-sdroot: /tmp holds mkfs's lost+found and a written file")
    except cp.StageFailed:
        pass
    finally:
        renode.close()
    return run.failures


def case_nob7(renode_path, kernel, flash, root_img, outdir):
    os.makedirs(outdir, exist_ok=True)
    card = os.path.join(outdir, "card.img")
    capacity = make_card(card, root_img, 0x83, "small")
    renode, run = boot(renode_path, kernel, flash, card, capacity, outdir)
    try:
        run.wait_for(
            [ROOT_LINES[0], NO_B7_LINE], "panic without a b7 entry", cp.BOOT_BUDGET_SECONDS
        )
        run.snapshot("panic")
        run.expect(not run.has("login:"), "login reached without a root partition")
    except cp.StageFailed:
        pass
    finally:
        renode.close()
    return run.failures


def main(argv):
    if argv == ["--self-test"] or len(argv) == 4:
        failed = self_test()
        for message in failed:
            print(f"check-picocalc-sdroot: self-test FAIL {message}", file=sys.stderr)
        if failed:
            return 1
        print("check-picocalc-sdroot: self-test pass")
        if argv == ["--self-test"]:
            return 0
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    # Renode resolves a relative @path against its own directory, not the
    # caller's, so every path it receives is absolute.
    renode_path = argv[0]
    unix_bin, root_img, outdir = (os.path.abspath(p) for p in argv[1:])
    os.makedirs(outdir, exist_ok=True)
    kernel = os.path.join(outdir, "unix-stamped.bin")
    flash = os.path.join(outdir, "flash-foreign.bin")
    stamped_kernel(unix_bin, kernel)
    foreign_flash(flash)
    failures = []
    try:
        cases = (
            ("sdroot", lambda d: case_root(renode_path, kernel, flash, root_img, d, "small")),
            ("tmp", lambda d: case_tmp(renode_path, kernel, flash, root_img, d)),
            ("nob7", lambda d: case_nob7(renode_path, kernel, flash, root_img, d)),
        )
        for name, case in cases:
            print(f"check-picocalc-sdroot: case {name}", flush=True)
            got = case(os.path.join(outdir, name))
            failures += [f"{name}: {f}" for f in got]
    except RenodeError as error:
        print(f"check-picocalc-sdroot: {error}", file=sys.stderr)
        return 2
    for failure in failures:
        print(f"check-picocalc-sdroot: FAIL {failure}", file=sys.stderr)
    if failures:
        return 1
    print("check-picocalc-sdroot: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
