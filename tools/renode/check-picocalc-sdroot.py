#!/usr/bin/env python3
"""Boot the PICOCALC kernel as the PicoCalc UF2 loader leaves it, root on SD.

The loader programs the kernel's UF2, stamps vector entries 4 to 10 (magic
0xe98cc638, flash end 0x101fc000, the first 20 bytes of the UF2 path) and
lends the flash above the kernel to other applications. sdrootattach() in
sys/arch/rp2040/dev/sd.c then takes the root from the card's first MBR
partition of type b7. This driver models that end state under Renode: the
kernel image carries the stamp, the flash root region holds a foreign byte
pattern in place of a Dhara journal, and the SD card model holds a card laid
out as the owner's PicoCalc card is: a FAT32 entry, then the b7 entry in
slot 3, then a Linux entry in slot 2 at the card's end.

Cases, each a fresh emulator:
  sdroot    a 40 MiB card: the stamped kernel prints the two root lines,
            the card line and root dev (5,3), reaches login on the panel,
            a keyboard login runs mount, which names /dev/sd0c on /, the
            flash root region still holds the foreign pattern, and UART0
            carries the tty0 login prompt
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

Usage: check-picocalc-sdroot.py RENODE KERNEL SDROOT_IMG OUTPUT_DIR
KERNEL is a flat unix.bin or a kernel UF2 such as the board image
distrib/rp2040/unix-PICOCALC_PSRAM.uf2 that goes on the card.
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import importlib.util
import os
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
}
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)

# Virtual seconds for the card stage: SPI at 398 kHz through
# identification, then sector reads of the table and the superblock.
SD_BUDGET_SECONDS = 60.0

# Both PicoCalc kernels; the board name in parentheses differs.
BANNER = "DiscoBSD 2.7 (PICOCALC"
ROOT_LINES = ["root: started by the PicoCalc loader, root on sd0", "root: sd0c"]
NO_B7_LINE = "panic: root: no partition of type b7 on sd0"


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


def make_card(path, root_img, p3_type, layout):
    root = open(root_img, "rb").read()
    if len(root) % SECTOR:
        raise ValueError("root image is not whole sectors")
    sectors, p3_start = LAYOUTS[layout][:2]
    p3 = (p3_type, p3_start, len(root) // SECTOR)
    p2 = (0x83, p3_start + p3[2], 2048)
    p1 = (0x0C, 2048, p3_start - 2048)
    if p2[1] + p2[2] > sectors:
        raise ValueError("layout does not hold the root image")
    table = b"".join(mbr_entry(*p) for p in (p1, p2, p3)) + bytes(16)
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
        run.wait_for(["root dev  = (5,3)"], "root device sd0c", SD_BUDGET_SECONDS)
        run.wait_for(["login:"], "login prompt", cp.LOGIN_BUDGET_SECONDS)
        run.type_line("operator")
        run.wait_for_prompt("shell prompt", cp.KEY_BUDGET_SECONDS)
        run.type_line("mount")
        run.wait_for(["/dev/sd0c on /"], "mount names the SD root", cp.KEY_BUDGET_SECONDS)
        run.snapshot("mount")
        bad = [
            hex(a)
            for a in FOREIGN_SAMPLES
            if monitor_int(renode, f"sysbus ReadDoubleWord {a:#x}") != FOREIGN * 0x01010101
        ]
        run.expect(not bad, f"flash root region rewritten at {bad}")
        if not bad:
            print(
                f"check-picocalc-sdroot: flash root region intact at "
                f"{len(FOREIGN_SAMPLES)} sampled words"
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
