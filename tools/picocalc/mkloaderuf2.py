"""Build the PicoCalc loader composite: a kernel UF2 followed by a root image.

usage: mkloaderuf2.py [-o OUTPUT] KERNEL.uf2 ROOT.img

The PicoCalc UF2 loader (pelrun/uf2loader) lists the .uf2 files of the
/pico1-apps directory on the SD card and programs the blocks of the selected
file that pass its block checks into flash (a block at XIP_BASE is dropped,
because the loader keeps its own boot2). It ignores each 512-byte chunk that
fails the checks. The output file is therefore both a UF2 the loader flashes
and a container for the root file system:

    offset 0            the kernel's N UF2 blocks (512 bytes each)
    N * 512 ... head    zero padding
    head                the root image, to the end of the file

    head = roundup(N * 512, 32768)

sys/arch/rp2040/dev/fat32tail.c finds the root image at boot from the FAT32
directory entry GIZMOBSD.UF2 (or its former name DISCOBSD.UF2, which the
locator still accepts until a card has booted the new name; two matching
files on the card fail closed). The head alignment is defined twice, as
FT32_HEAD_ALIGN in fat32tail.h and as HEAD_ALIGN here, and the gate
(tests/fat32tail/check.sh) compares the two. The output name defaults to
gizmobsd.uf2: the loader accepts a
selection only when the path ends in the lowercase text ".uf2", and an 8.3
name needs zero long-name directory entries.

The kernel UF2 must hold N blocks that the loader accepts: the UF2 magic
words, no not-main-flash flag, the RP2040 family, a 256-byte payload at a
256-byte-aligned address in flash below the loader's own 16 KiB, consecutive
block numbers 0 .. N-1, and nothing after block N-1. The builder also
requires the first block to lie in flash sector 0, so that the file is an
image of the flash from XIP_BASE; the loader itself accepts another first
block and then skips its boot2 handling. The root image must be a whole
GizmoBSD file system: the magic words of struct fs in sys/sys/fs.h, a length
equal to fs_fsize blocks of 1024 bytes, and the minimums tools/fsutil/fsutil.c
and create.c enforce (fs_fsize at least 10, fs_isize from 2 up to fs_fsize
exclusive). It must not hold a 512-byte-aligned block
that the loader would act on while it skips the tail: a block for RAM.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import tempfile
from pathlib import Path

UF2_BLOCK = 512
UF2_HEADER = struct.Struct("<8I")
UF2_START0 = 0x0A324655
UF2_START1 = 0x9E5D5157
UF2_END = 0x0AB16F30
UF2_FLAG_NOT_MAIN_FLASH = 0x00000001
UF2_FLAG_FAMILY = 0x00002000
RP2040_FAMILY = 0xE48BFF56
UF2_PAYLOAD = 256

XIP_BASE = 0x10000000
FLASH_SECTOR = 4096
# The loader's boot2 stage and application area end where its stage3 begins
# (0x101FC000, the 16 KiB at the top of the 2 MiB flash).
LOADER_FLASH_END = 0x101FC000
# pico-sdk SRAM_BASE and SRAM_END for the RP2040.
SRAM_BASE = 0x20000000
SRAM_END = 0x20042000

HEAD_ALIGN = 32768
DEFAULT_NAME = "gizmobsd.uf2"
# The former name, which the locator still accepts.
LEGACY_NAME = "discobsd.uf2"

# sys/sys/fs.h: FSMAGIC1 is the first word of block 0 of the root image and
# FSMAGIC2 the last word of struct fs, which is 1024 bytes long.
FS_MAGIC1 = b"FS<<"
FS_MAGIC2 = b">>FS"
FS_SUPER_BYTES = 1024
FS_MIN_BYTES = 2 * UF2_BLOCK
# fs_isize (blocks before the data area) and fs_fsize (the volume in 1024-byte
# blocks) are the second and third words of struct fs. tools/fsutil/fsutil.c
# refuses a volume of fewer than 10 blocks ("Need at least 10 blocks") and
# tools/fsutil/create.c gives every volume an i-list of at least 2 blocks.
FS_ISIZE_OFFSET = 4
FS_MIN_BLOCKS = 10
FS_MIN_ISIZE = 2

# FAT 8.3 short-name characters other than letters and digits.
SHORT_PUNCT = "!#$%&'()-@^_`{}~"


class BuildError(Exception):
    """An input or the output name fails a rule of the composite layout."""


def head_size(blocks: int) -> int:
    """Bytes from the file start to the root image."""
    return -(-(blocks * UF2_BLOCK) // HEAD_ALIGN) * HEAD_ALIGN


def validate_kernel(data: bytes) -> int:
    """Check every UF2 block the loader would program; return the count N."""
    if not data or len(data) % UF2_BLOCK:
        raise BuildError("kernel UF2 size is not a positive multiple of 512")
    blocks = len(data) // UF2_BLOCK
    for index in range(blocks):
        offset = index * UF2_BLOCK
        start0, start1, flags, target, payload, number, total, family = UF2_HEADER.unpack_from(
            data, offset
        )
        (end,) = struct.unpack_from("<I", data, offset + UF2_BLOCK - 4)
        where = f"kernel UF2 block {index}"
        if (start0, start1, end) != (UF2_START0, UF2_START1, UF2_END):
            raise BuildError(f"{where}: bad magic")
        if flags & UF2_FLAG_NOT_MAIN_FLASH:
            raise BuildError(f"{where}: not-main-flash flag set")
        if not flags & UF2_FLAG_FAMILY or family != RP2040_FAMILY:
            raise BuildError(f"{where}: family is not RP2040")
        if payload != UF2_PAYLOAD or target % UF2_PAYLOAD:
            raise BuildError(f"{where}: payload size or target alignment")
        if target < XIP_BASE:
            raise BuildError(f"{where}: target {target:#x} lies below flash")
        if target + UF2_PAYLOAD > LOADER_FLASH_END:
            raise BuildError(f"{where}: target {target:#x} reaches the loader's own flash")
        if number != index or total != blocks:
            raise BuildError(f"{where}: numbered {number} of {total}, expected {index} of {blocks}")
        if index == 0 and target >= XIP_BASE + FLASH_SECTOR:
            raise BuildError(f"{where}: the builder requires the first block in flash sector 0")
    return blocks


def validate_root(data: bytes) -> None:
    """The image starts a GizmoBSD file system and has whole sectors."""
    if len(data) % UF2_BLOCK or len(data) < FS_MIN_BYTES:
        raise BuildError("root image size is not a multiple of 512 of at least 1024 bytes")
    if data[:4] != FS_MAGIC1 or data[FS_SUPER_BYTES - 4 : FS_SUPER_BYTES] != FS_MAGIC2:
        raise BuildError("root image does not start with the GizmoBSD file-system magic words")
    isize, blocks = struct.unpack_from("<2I", data, FS_ISIZE_OFFSET)
    if blocks < FS_MIN_BLOCKS:
        raise BuildError(f"root image superblock claims {blocks} blocks, at least {FS_MIN_BLOCKS}")
    if not FS_MIN_ISIZE <= isize < blocks:
        raise BuildError(
            f"root image i-list size {isize} is outside {FS_MIN_ISIZE} .. {blocks - 1}"
        )
    if blocks * FS_SUPER_BYTES != len(data):
        raise BuildError(
            f"root image holds {len(data)} bytes, its superblock claims {blocks} blocks"
        )


def ram_block_hazard(data: bytes) -> int | None:
    """Offset of a 512-byte-aligned block that makes the loader launch RAM.

    While the loader skips the tail it applies the same block checks as to
    the kernel's blocks. A block that passes them and targets SRAM does not
    program flash: the loader starts the file as a RAM-only application.
    A block aimed at flash is harmless there, because its number can match
    neither the next expected block nor a block of this file.
    """
    for offset in range(0, len(data) - UF2_BLOCK + 1, UF2_BLOCK):
        start0, start1, flags, target, payload, number, total, family = UF2_HEADER.unpack_from(
            data, offset
        )
        (end,) = struct.unpack_from("<I", data, offset + UF2_BLOCK - 4)
        if (start0, start1, end) != (UF2_START0, UF2_START1, UF2_END):
            continue
        if flags & UF2_FLAG_NOT_MAIN_FLASH or target % UF2_PAYLOAD or payload != UF2_PAYLOAD:
            continue
        if total == 0 or number >= total:
            continue
        if flags & UF2_FLAG_FAMILY and family != RP2040_FAMILY:
            continue
        if SRAM_BASE <= target < SRAM_END:
            return offset
    return None


def check_output_name(path: Path) -> None:
    """A lowercase .uf2 name in 8.3 form, which needs no long-name entry."""
    name = path.name
    base, dot, extension = name.rpartition(".")
    if not dot or extension != "uf2":
        raise BuildError(f"{name}: the extension must be the lowercase text .uf2")
    allowed = re.compile("[A-Za-z0-9" + re.escape(SHORT_PUNCT) + "]{1,8}")
    if allowed.fullmatch(base) is None:
        raise BuildError(f"{name}: the name is not 8.3 (1 to 8 short-name characters)")
    if base != base.lower() and base != base.upper():
        raise BuildError(f"{name}: a mixed-case name needs a long-name entry")


def build(kernel: bytes, root: bytes) -> bytes:
    """The composite file's bytes."""
    blocks = validate_kernel(kernel)
    validate_root(root)
    hazard = ram_block_hazard(root)
    if hazard is not None:
        raise BuildError(
            f"root image offset {hazard:#x} holds a UF2 block for RAM, which the loader "
            "would start as an application while it skips the tail"
        )
    return kernel + bytes(head_size(blocks) - len(kernel)) + root


def write_atomically(path: Path, data: bytes) -> None:
    """Write beside the destination and rename, so no partial file is named."""
    directory = path.parent if str(path.parent) else Path(".")
    fd, temporary = tempfile.mkstemp(dir=directory, prefix=".mkloaderuf2.")
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-o", "--output", type=Path, default=Path(DEFAULT_NAME))
    parser.add_argument("kernel", type=Path, help="the kernel's UF2 file")
    parser.add_argument("root", type=Path, help="the root file-system image")
    args = parser.parse_args(argv)
    try:
        check_output_name(args.output)
        composite = build(args.kernel.read_bytes(), args.root.read_bytes())
        write_atomically(args.output, composite)
    except (BuildError, OSError) as error:
        print(f"mkloaderuf2: {error}", file=sys.stderr)
        return 1
    lowered = args.output.name.lower()
    if lowered == LEGACY_NAME:
        print(
            f"mkloaderuf2: {args.output.name} is the former name; the kernel looks for "
            f"{DEFAULT_NAME} and still accepts it, but two matching files on the card "
            "fail closed, so keep one",
            file=sys.stderr,
        )
    elif lowered != DEFAULT_NAME:
        print(
            f"mkloaderuf2: the kernel looks for the file name {DEFAULT_NAME}; "
            f"{args.output.name} is not found at boot",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
