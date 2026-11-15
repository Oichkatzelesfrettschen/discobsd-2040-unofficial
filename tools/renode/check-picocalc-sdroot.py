#!/usr/bin/env python3
"""Boot the PICOCALC kernel as the PicoCalc UF2 loader leaves it, root on SD.

The loader programs the kernel's UF2, stamps vector entries 4 to 10 (magic
0xe98cc638, flash end 0x101fc000, the first 20 bytes of the UF2 path) and
lends the flash above the kernel to other applications. sdrootattach() in
sys/arch/rp2040/dev/sd.c then takes the root from the card's first MBR
partition of type b7. This driver models that end state under Renode: the
kernel image carries the stamp, the flash root region holds a foreign byte
pattern in place of a Dhara journal, and the SD card model holds a card laid
out as the owner's PicoCalc card is (a FAT32 entry, a Linux entry, then the
b7 entry in slot 3), scaled down.

Cases, each a fresh emulator:
  sdroot    the stamped kernel prints the two root lines, reaches login on
            the panel, a keyboard login runs mount, which names /dev/sd0c
            on /, and the flash root region still holds the foreign pattern
  nob7      the same kernel and a card whose b7 entry is retyped 83 stops
            with the panic line naming the missing partition

The unstamped kernel's flash root is check-picocalc.sh's case and is not
repeated here.

Usage: check-picocalc-sdroot.py RENODE UNIX_BIN SDROOT_IMG OUTPUT_DIR
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
P1 = (0x0C, 2048, 4096)  # FAT32 LBA, left empty
P2 = (0x83, 6144, 2048)  # Linux
P3_START = 8192  # b7, the root image

# Virtual seconds for the card stage: SPI at 398 kHz through
# identification, then sector reads of the table and the superblock.
SD_BUDGET_SECONDS = 60.0

ROOT_LINES = ["root: started by the PicoCalc loader, root on sd0", "root: sd0c"]
NO_B7_LINE = "panic: root: no partition of type b7 on sd0"


def stamped_kernel(unix_bin, path):
    image = bytearray(open(unix_bin, "rb").read())
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


def make_card(path, root_img, p3_type):
    root = open(root_img, "rb").read()
    if len(root) % SECTOR:
        raise ValueError("root image is not whole sectors")
    p3 = (p3_type, P3_START, len(root) // SECTOR)
    total = P3_START * SECTOR + len(root)
    total = (total + 0xFFFFF) & ~0xFFFFF  # whole MiB
    table = b"".join(mbr_entry(*p) for p in (P1, P2, p3)) + bytes(16)
    with open(path, "wb") as stream:
        stream.truncate(total)
        stream.seek(446)
        stream.write(table + b"\x55\xaa")
        stream.seek(P3_START * SECTOR)
        stream.write(root)
    return total


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


def case_sdroot(renode_path, kernel, flash, root_img, outdir):
    os.makedirs(outdir, exist_ok=True)
    card = os.path.join(outdir, "card.img")
    capacity = make_card(card, root_img, 0xB7)
    renode, run = boot(renode_path, kernel, flash, card, capacity, outdir)
    try:
        # The banner scrolls off the 32-row panel before the card answers,
        # so the two waits run one after the other.
        run.wait_for(cp.BANNER_LINES[:1], "banner", cp.BOOT_BUDGET_SECONDS)
        run.wait_for(ROOT_LINES, "root lines", SD_BUDGET_SECONDS)
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
    except cp.StageFailed:
        pass
    finally:
        renode.close()
    return run.failures


def case_nob7(renode_path, kernel, flash, root_img, outdir):
    os.makedirs(outdir, exist_ok=True)
    card = os.path.join(outdir, "card.img")
    capacity = make_card(card, root_img, 0x83)
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
        for name, case in (("sdroot", case_sdroot), ("nob7", case_nob7)):
            print(f"check-picocalc-sdroot: case {name}", flush=True)
            got = case(renode_path, kernel, flash, root_img, os.path.join(outdir, name))
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
