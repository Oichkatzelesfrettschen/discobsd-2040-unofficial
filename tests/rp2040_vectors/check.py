"""Calibrate linked RP2040 exception-vector pointer verification.

The fixture also holds tools/kernsum to its contract on the linked kernel:
check mode accepts it, a copy with the identity words cleared stamps back to
the same bytes, a second stamp is refused, an ELF whose loadable segments
share file bytes is refused, and unix.hex beside the kernel decodes to the
bytes of unix.bin, the image the sum describes.
"""

import argparse
import io
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
IDENTITY_WORDS = 0x10000120
sys.path.insert(0, str(ROOT / "tools"))
from check_rp2040_vectors import CRC, LENGTH, SLOTS  # noqa: E402

# What the checker reports for a changed identity word (entries 8 and 9).
IDENTITY = {LENGTH: "length word is", CRC: "CRC word is"}


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


def kernsum(arguments, expected, diagnostic=None):
    result = subprocess.run([str(KERNSUM), *map(str, arguments)],
                            text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"kernsum {arguments} exit {result.returncode}: {result.stderr}")
    if diagnostic is not None and diagnostic not in result.stderr:
        raise AssertionError(f"kernsum diagnostic differs: {result.stderr}")


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
    print("PASS RP2040 vectors: linked control, all slots, identity words, Thumb bits, kernsum")


if __name__ == "__main__":
    main()
