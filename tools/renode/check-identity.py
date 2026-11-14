#!/usr/bin/env python3
"""Boot a stamped kernel and three altered copies under Renode and read the image line.

image_identify() in sys/arch/rp2040/rp2040/board.c sums the flash image at
boot and prints one console line. The line ends after the sum when the sum
equals the word tools/kernsum stamped, and carries the stamped word and
"differs" when it does not. This driver boots four kernels on the same
root flash image and requires the exact line for each:

stamped
    UNIX_BIN as built. The line names the file's length and sum and stops
    there.
stored word
    One bit of the stamped CRC32 word (vector entry 13) inverted. The sum
    reads that word as zero, so the computed sum is the file's and only the
    stored value moves.
image byte
    The first byte of the padding that follows the board address word at
    0x100001C0 set. The four bytes sit between the vector table and the
    first function, hold zero in the link, and no instruction fetch or data
    read reaches them, so the kernel runs unchanged while the sum over its
    bytes moves and the stamped word stays.
loader stamp
    The stamp the PicoCalc UF2 loader (pelrun/uf2loader) writes over flash
    bytes 0x110-0x12B after it flashes an application: the magic word
    0xe98cc638, the flash end 0x101fc000 and the first 20 bytes of the UF2
    path, written over vector entries 4 to 10. The sum reads that span as
    zero, so the line equals the stamped kernel's. The built kernel holds
    zero there, so this boot alone shows that image_identify() skips the
    span.

The expected sums are computed here from the bytes each boot loads, with the
second stage, the stamp span, the CRC word and the board descriptor read as
zero as tools/kernsum reads them. Each boot continues through the storage
probe, so an altered image that reports and then stops fails. The stamped
boot calibrates the altered ones and they calibrate it: a kernel that
always or never prints "differs" fails one side.

The claim is the emulated kernel's comparison over the flash model's bytes.
A board's result is a separate evidence class (sys/arch/rp2040/doc/TESTING.md).

Usage: check-identity.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer
or UNIX_BIN carries no usable identity words.
"""

import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from renode_monitor import Renode, RenodeError  # noqa: E402

CONFIG = "PICO_UART"
# The loader's stamp: magic, flash end, then 20 bytes of the UF2 path.
LOADER_STAMP = struct.pack("<2I", 0xE98CC638, 0x101FC000) + b"/pico1-apps/DiscoBSD"


def load_boot():
    """Import check-boot.py, whose name is not an identifier."""
    spec = importlib.util.spec_from_file_location("check_boot", os.path.join(HERE, "check-boot.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_cases(boot, image: bytes) -> list[tuple[str, bytes, str]]:
    """Return (name, kernel bytes, expected console line) for the four boots."""
    length, stored = struct.unpack_from("<2I", image, boot.IDENTITY_OFFSET)
    crc = boot.identity_sum(image)
    if length != len(image) or stored != crc:
        raise ValueError("identity words do not describe the file")

    flipped = bytearray(image)
    struct.pack_into("<I", flipped, boot.IDENTITY_OFFSET + 4, stored ^ 1)

    padding = boot.BOARD_WORD_OFFSET + 4
    if image[padding:padding + 4] != bytes(4):
        raise ValueError(f"the bytes after the board address word at {padding:#x} are not zero")
    altered = bytearray(image)
    altered[padding] = 0x01
    altered_crc = boot.identity_sum(bytes(altered))
    if altered_crc == crc:
        raise ValueError("the altered image sums to the stamped value")

    if len(LOADER_STAMP) != boot.STAMP_SIZE:
        raise ValueError("the loader stamp does not fill its span")
    if image[boot.STAMP_OFFSET:boot.STAMP_OFFSET + boot.STAMP_SIZE] != bytes(boot.STAMP_SIZE):
        raise ValueError("the loader's stamp span of the kernel is not zero")
    stamped = bytearray(image)
    stamped[boot.STAMP_OFFSET:boot.STAMP_OFFSET + boot.STAMP_SIZE] = LOADER_STAMP
    if boot.identity_sum(bytes(stamped)) != crc:
        raise ValueError("the loader stamp moves the sum")

    return [
        ("stamped", image, f"image: {length} bytes, crc32 {crc:x}\n"),
        ("stored word", bytes(flipped),
         f"image: {length} bytes, crc32 {crc:x}, stored {stored ^ 1:x} differs\n"),
        ("image byte", bytes(altered),
         f"image: {length} bytes, crc32 {altered_crc:x}, stored {stored:x} differs\n"),
        ("loader stamp", bytes(stamped), f"image: {length} bytes, crc32 {crc:x}\n"),
    ]


def boot_case(boot, renode_path: str, flash_bin: str, outdir: str,
              name: str, kernel: bytes, line: str) -> list[str]:
    """Boot kernel and return the failures of its image line and storage probe."""
    slug = name.replace(" ", "-")
    unix_bin = os.path.join(outdir, f"unix-{slug}.bin")
    with open(unix_bin, "wb") as handle:
        handle.write(kernel)
    transcript = os.path.join(outdir, f"uart0-{slug}.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    renode = Renode(renode_path, os.path.join(outdir, f"renode-{slug}.log"), boot.WALL_SECONDS)
    console = boot.Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        # board_report() prints the image line ahead of the banner.
        console.wait_for("image: ", f"{name}: image line", boot.PROBE_BUDGET_SECONDS)
        start = console.cursor - len("image: ")
        console.wait_lines([f"DiscoBSD 2.7 ({CONFIG})"] + boot.STORAGE_LINES,
                           f"{name}: banner and storage probe", boot.PROBE_BUDGET_SECONDS)
        text = console.text()
        printed = text[start:text.index("\n", start) + 1]
        print(f"check-identity: {name}: {printed.rstrip()}")
        if printed != line:
            console.fail(f"{name}: expected {line.rstrip()!r}")
    except boot.StageFailed:
        print(f"check-identity: transcript in {transcript}", file=sys.stderr)
    finally:
        renode.close()
    return console.failures


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    boot = load_boot()
    renode_path, unix_bin, flash_bin, outdir = argv
    try:
        with open(unix_bin, "rb") as handle:
            cases = build_cases(boot, handle.read())
    except (OSError, ValueError, struct.error) as error:
        print(f"check-identity: {unix_bin}: {error}", file=sys.stderr)
        return 2
    os.makedirs(outdir, exist_ok=True)
    failed = False
    for name, kernel, line in cases:
        try:
            failed = bool(boot_case(boot, renode_path, flash_bin, outdir,
                                    name, kernel, line)) or failed
        except RenodeError as error:
            print(f"check-identity: {error}", file=sys.stderr)
            return 2
    if failed:
        return 1
    print("check-identity: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
