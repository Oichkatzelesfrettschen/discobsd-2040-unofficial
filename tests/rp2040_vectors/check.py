"""Calibrate linked RP2040 exception-vector pointer verification.

The fixture also holds tools/kernsum to its contract on the linked kernel:
check mode accepts it, a copy with the identity words cleared stamps back to
the same bytes, a second stamp is refused, an ELF whose loadable segments
share file bytes is refused, and unix.hex beside the kernel decodes to the
bytes of unix.bin, the image the sum describes.

The loader-state cases hold the image identity to the writes of the PicoCalc
UF2 loader. A kernel whose bytes 0-255 are replaced and whose bytes
0x110-0x12B carry the loader's stamp still verifies, in the checker and in
kernsum; a change to any single byte beside those spans fails; and a checker
or a kernsum built without one of the two exclusions fails the loader-state
kernel, which calibrates the case.
"""

import argparse
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools/check_rp2040_vectors.py"
KERNSUM = ROOT / "tools/bin/rp2040/kernsum"
IDENTITY_WORDS = 0x10000130
sys.path.insert(0, str(ROOT / "tools"))
from check_rp2040_vectors import (  # noqa: E402
    BOARD_WORD,
    BOOT2_SIZE,
    CRC,
    IMAGE_ADDRESS,
    LENGTH,
    SLOTS,
    STAMP_END,
    STAMP_START,
    flash_image,
    identity_error,
)

# What the checker reports for a changed identity word (entries 12 and 13).
IDENTITY = {LENGTH: "length word is", CRC: "CRC word is"}

# What the loader writes over flash bytes 0x110-0x12B after it flashes an
# application: a magic word, the end of its flash area and the first 20
# bytes of the UF2 path, which is "/pico1-apps/DiscoBSD" for discobsd.uf2.
LOADER_STAMP = struct.pack("<2I", 0xE98CC638, 0x101FC000) + b"/pico1-apps/DiscoBSD"


def check_source(text):
    table = text.split("g_pfnVectors:", 1)[1].split(".size\tg_pfnVectors", 1)[0]
    names = re.findall(r"^\s*\.word\s+(\w+)", table, re.MULTILINE)
    # The source leaves the two words tools/kernsum stamps as zero.
    if tuple(None if name == "0" else name for name in names) != tuple(
            None if slot in IDENTITY else slot for slot in SLOTS):
        raise ValueError("source vector slots differ from the verifier contract")


def run(path, expected, diagnostic=None):
    result = subprocess.run([sys.executable, str(CHECKER), str(path)],
                            text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"vector checker exit {result.returncode}: {result.stderr}")
    if diagnostic is not None and diagnostic not in result.stderr:
        raise AssertionError(f"vector checker diagnostic differs: {result.stderr}")


def kernsum(arguments, expected, diagnostic=None, program=KERNSUM):
    result = subprocess.run([str(program), *map(str, arguments)],
                            text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"kernsum {arguments} exit {result.returncode}: {result.stderr}")
    if diagnostic is not None and diagnostic not in result.stderr:
        raise AssertionError(f"kernsum diagnostic differs: {result.stderr}")
    return result.stdout


def hex_image(path, base):
    """Return the bytes an Intel HEX file programs from address base up."""
    image, upper = bytearray(), 0
    for line in path.read_text(encoding="ascii").split():
        record = bytes.fromhex(line[1:])
        count, address, kind, data = record[0], record[1] << 8 | record[2], record[3], record[4:-1]
        if sum(record) & 0xFF or len(data) != count:
            raise AssertionError(f"{path}: bad record {line}")
        if kind == 4:
            upper = (data[0] << 8 | data[1]) << 16
        elif kind == 0:
            start = upper + address - base
            if start != len(image):
                raise AssertionError(f"{path}: gap or overlap at {upper + address:#x}")
            image.extend(data)
    return bytes(image)


def check_kernsum(kernel, original, elf, directory):
    """Hold tools/kernsum to its contract on the linked kernel."""
    kernsum(["-c", kernel], 0)
    load = next(segment.header for segment in elf.iter_segments()
                if segment.header.p_type == "PT_LOAD"
                and segment.header.p_paddr <= IDENTITY_WORDS
                < segment.header.p_paddr + segment.header.p_filesz)
    words = load.p_offset + IDENTITY_WORDS - load.p_paddr
    cleared = bytearray(original)
    cleared[words:words + 8] = bytes(8)
    copy = Path(directory) / "copy.elf"
    copy.write_bytes(cleared)
    kernsum(["-c", copy], 1, "length word 0")
    kernsum([copy], 0)
    if copy.read_bytes() != original:
        raise AssertionError("a restamped kernel differs from the linked one")
    kernsum([copy], 1, "identity words are not zero")
    # Point a later loadable segment at the file bytes of the vector segment.
    header = elf.header
    loads = [index for index, segment in enumerate(elf.iter_segments())
             if segment.header.p_type == "PT_LOAD" and segment.header.p_filesz]
    aliased = bytearray(cleared)
    entry = header.e_phoff + loads[-1] * header.e_phentsize
    struct.pack_into("<I", aliased, entry + 4, load.p_offset)
    copy.write_bytes(aliased)
    kernsum([copy], 2, "two segments share file bytes")
    if copy.read_bytes() != aliased:
        raise AssertionError("a refused kernel was written")
    binary = kernel.with_name("unix.bin").read_bytes()
    if hex_image(kernel.with_name("unix.hex"), 0x10000000) != binary:
        raise AssertionError("unix.hex does not decode to the bytes of unix.bin")


def file_offset(elf, address, size=1):
    """Return the file offset of the flash bytes at address, from one PT_LOAD."""
    for segment in elf.iter_segments():
        header = segment.header
        if (header.p_type == "PT_LOAD" and header.p_paddr <= address
                and address + size <= header.p_paddr + header.p_filesz):
            return header.p_offset + address - header.p_paddr
    raise AssertionError(f"no loadable segment holds {address:#x}")


def write_flash(data, elf, flash_offset, content):
    """Write content over the flash image bytes from flash_offset, in the ELF bytes data."""
    start = file_offset(elf, IMAGE_ADDRESS + flash_offset, len(content))
    data[start:start + len(content)] = content


def loader_state(original, elf):
    """Return the kernel as the loader leaves flash: its own second stage and its stamp."""
    patched = bytearray(original)
    start = file_offset(elf, IMAGE_ADDRESS, BOOT2_SIZE)
    for position in range(start, start + BOOT2_SIZE):
        patched[position] ^= 0xFF
    write_flash(patched, elf, STAMP_START, LOADER_STAMP)
    return patched


def flip_elf(data, elf, flash_offset):
    """Return a copy of the ELF bytes data with the flash byte at flash_offset inverted."""
    copy = bytearray(data)
    copy[file_offset(elf, IMAGE_ADDRESS + flash_offset)] ^= 0xFF
    return copy


def flip_image(image, offset):
    """Return a copy of a flash image with the byte at offset inverted."""
    copy = bytearray(image)
    copy[offset] ^= 0xFF
    return copy


def build_mutant_kernsum(directory, anchor):
    """Compile tools/kernsum with the statement anchor removed; return the program."""
    text = (ROOT / "tools/kernsum/kernsum.c").read_text(encoding="utf-8")
    if anchor not in text:
        raise AssertionError(f"kernsum mutation anchor is absent: {anchor}")
    source = Path(directory) / "kernsum-mutant.c"
    source.write_text(text.replace(anchor, "", 1), encoding="utf-8")
    program = Path(directory) / "kernsum-mutant"
    subprocess.run([os.environ.get("CC", "cc"), "-o", str(program), str(source)], check=True)
    return program


def check_loader_state(kernel, original, elf, text_start, directory):
    """Hold the image identity to the loader's writes."""
    image = flash_image(elf)
    loader = loader_state(original, elf)
    loader_image = flash_image(ELFFile(io.BytesIO(bytes(loader))))
    if len(loader_image) != len(image) or loader_image[:BOOT2_SIZE] == image[:BOOT2_SIZE]:
        raise AssertionError("the loader-state kernel keeps its second stage")
    # The stamp lands on the three words the identity once occupied.
    if struct.unpack_from("<3I", loader_image, 0x120) != (0x2F737070, 0x63736944, 0x4453426F):
        raise AssertionError("the loader stamp does not overlay vector entries 8 to 10")
    if identity_error(image) is not None:
        raise AssertionError("the control image fails its own check")

    # Known good: the loader-state image verifies, in the checker and in
    # kernsum. The build gate still requires a linked kernel to hold zero in
    # the stamp's vector entries, so it rejects the loader-state ELF there.
    if identity_error(loader_image) is not None:
        raise AssertionError(f"loader-state image rejected: {identity_error(loader_image)}")
    elf_path = Path(directory) / "loader-state.elf"
    elf_path.write_bytes(loader)
    run(elf_path, 1, "reserved vector 4 is nonzero")
    if kernsum(["-c", elf_path], 0) != kernsum(["-c", kernel], 0):
        raise AssertionError("kernsum reports another identity for the loader-state kernel")

    # Known bad: one covered byte changed. Each offset is a boundary of an
    # excluded span or a distinct covered region.
    covered = {
        "first vector byte": BOOT2_SIZE,
        "byte before the loader stamp": STAMP_START - 1,
        "byte after the loader stamp": STAMP_END,
        "byte after the length word": IDENTITY_WORDS - IMAGE_ADDRESS + 4 + 4,
        "board address word": BOARD_WORD,
        "padding after the board address word": BOARD_WORD + 4,
        "first function byte": text_start,
        "last byte": len(image) - 1,
    }
    for label, offset in covered.items():
        if identity_error(flip_image(loader_image, offset)) is None:
            raise AssertionError(f"checker accepts a changed {label}")
        if offset == BOARD_WORD:
            continue  # kernsum refuses a bad descriptor address; checked below
        elf_path.write_bytes(flip_elf(loader, elf, offset))
        kernsum(["-c", elf_path], 1, "CRC word")

    # Known good, boundaries: the first and last byte of each excluded span.
    excluded = {
        "first byte": 0,
        "last second-stage byte": BOOT2_SIZE - 1,
        "first stamp byte": STAMP_START,
        "last stamp byte": STAMP_END - 1,
    }
    for label, offset in excluded.items():
        if identity_error(flip_image(loader_image, offset)) is not None:
            raise AssertionError(f"checker rejects a changed {label}")
        elf_path.write_bytes(flip_elf(loader, elf, offset))
        kernsum(["-c", elf_path], 0)

    # Calibration: a comparison that forgets one exclusion fails the
    # loader-state image.
    for forget, anchor in (("boot2", "memset(image, 0, BOOT2_SIZE);"),
                           ("stamp", "memset(image + STAMP_OFFSET, 0, STAMP_SIZE);")):
        if identity_error(loader_image, forget=(forget,)) is None:
            raise AssertionError(f"a checker without the {forget} exclusion accepts the loader")
        elf_path.write_bytes(loader)
        program = build_mutant_kernsum(directory, anchor)
        kernsum(["-c", elf_path], 1, "CRC word", program=program)
    # A forgotten descriptor exclusion fails a rewritten descriptor.
    board = struct.unpack_from("<I", image, BOARD_WORD)[0] - IMAGE_ADDRESS
    rewritten = flip_image(image, board + 8)
    if identity_error(rewritten) is not None:
        raise AssertionError("checker rejects a rewritten board descriptor")
    if identity_error(rewritten, forget=("board",)) is None:
        raise AssertionError("a checker without the board exclusion accepts a rewritten descriptor")

    # A board address that does not address a descriptor is refused.
    elf_path.write_bytes(flip_elf(original, elf, BOARD_WORD))
    kernsum(["-c", elf_path], 2, "board descriptor")
    run(elf_path, 1, "board address word differs")


def symbol_value(elf, name):
    return elf.get_section_by_name(".symtab").get_symbol_by_name(name)[0].entry.st_value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    args = parser.parse_args()
    source = (ROOT / "sys/arch/rp2040/rp2040/locore0.S").read_text(encoding="utf-8")
    check_source(source)
    mutant_source = source.replace(".word\tUSBCTRL_IRQ_Handler", ".word\tXIP_IRQ_Handler", 1)
    if mutant_source == source:
        raise AssertionError("source mutation anchor is absent")
    try:
        check_source(mutant_source)
    except ValueError:
        pass
    else:
        raise AssertionError("wrong source handler was accepted")
    original = args.kernel.read_bytes()
    elf = ELFFile(io.BytesIO(original))
    symbol = elf.get_section_by_name(".symtab").get_symbol_by_name("g_pfnVectors")[0]
    section = elf.get_section(symbol.entry.st_shndx)
    offset = section.header.sh_offset + symbol.entry.st_value - section.header.sh_addr
    words = struct.unpack_from("<48I", original, offset)
    targets = {value for value in words[1:] if value}
    if len(targets) < 2:
        raise AssertionError("fixture needs two distinct linked interrupt targets")
    run(args.kernel, 0)
    with tempfile.TemporaryDirectory(prefix="rp2040-vector-fixture-") as directory:
        mutant = Path(directory) / "mutant.elf"
        for index, value in enumerate(words):
            if index == 0:
                replacement = value ^ 4
            else:
                replacement = next(target for target in targets if target != value)
            modified = bytearray(original)
            struct.pack_into("<I", modified, offset + index * 4, replacement)
            mutant.write_bytes(modified)
            if SLOTS[index] is None:
                diagnostic = f"reserved vector {index} is nonzero"
            elif SLOTS[index] in IDENTITY:
                diagnostic = IDENTITY[SLOTS[index]]
            else:
                diagnostic = f"vector {index} differs from {SLOTS[index]}"
            run(mutant, 1, diagnostic)
        # The Thumb bit belongs to handler entries; the identity words and
        # the descriptor address are data.
        for index, value in enumerate(words[1:], 1):
            if value & 1 and isinstance(SLOTS[index], str) and SLOTS[index].endswith("_Handler"):
                modified = bytearray(original)
                struct.pack_into("<I", modified, offset + index * 4, value & ~1)
                mutant.write_bytes(modified)
                run(mutant, 1, f"vector {index} differs from {SLOTS[index]}")
        mutant.write_bytes(original[:80])
        run(mutant, 1)
        check_kernsum(args.kernel, original, elf, directory)
        text_start = symbol_value(elf, "Reset_Handler") & ~1
        check_loader_state(args.kernel, original, elf, text_start - IMAGE_ADDRESS, directory)
    print("PASS RP2040 vectors: linked control, all slots, identity words, Thumb bits, kernsum, "
          "loader state")


if __name__ == "__main__":
    main()
