"""Calibrate linked executable-format pointer and count verification."""

import argparse
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools/check_rp2040_execsw.py"


def run(image, expected):
    result = subprocess.run([sys.executable, str(CHECKER), str(image)],
                            capture_output=True, text=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"{image}: exit {result.returncode}: {result.stderr}")
    return result


def symbol_offset(elf, name):
    symbols = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    assert symbols and len(symbols) == 1
    symbol = symbols[0]
    section = elf.get_section(symbol.entry.st_shndx)
    return section.header.sh_offset + symbol.entry.st_value - section.header.sh_addr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    args = parser.parse_args()
    original = args.kernel.read_bytes()
    elf = ELFFile(io.BytesIO(original))
    table_offset = symbol_offset(elf, "execsw")
    count_offset = symbol_offset(elf, "nexecs")
    entries = [struct.unpack_from("<II", original, table_offset + index * 8)
               for index in range(4)]
    assert len({pointer for pointer, _ in entries}) == 4
    assert len({name for _, name in entries}) == 4
    assert all(pointer & 1 for pointer, _ in entries)
    run(args.kernel, 0)
    with tempfile.TemporaryDirectory(prefix="rp2040-execsw-fixture-") as directory:
        mutant = Path(directory) / "mutant.elf"
        mutations = []
        for index, (pointer, _) in enumerate(entries):
            alternate, alternate_name = entries[(index + 1) % len(entries)]
            offset = table_offset + index * 8
            mutations.extend((
                (f"wrong-handler-{index}", offset, alternate, "handler pointer differs"),
                (f"null-handler-{index}", offset, 0, "handler pointer differs"),
                (f"non-thumb-handler-{index}", offset, pointer & ~1, "handler pointer differs"),
                (f"wrong-name-{index}", offset + 4, alternate_name, "format name differs"),
            ))
        mutations.append(("short-count", count_offset, 3, "initialized format count differs"))
        for name, offset, value, diagnostic in mutations:
            modified = bytearray(original)
            struct.pack_into("<I", modified, offset, value)
            mutant.write_bytes(modified)
            result = run(mutant, 1)
            assert diagnostic in result.stderr, f"{name}: unexpected diagnostic {result.stderr}"
        mutant.write_bytes(original[:80])
        run(mutant, 1)
    print("PASS RP2040 execsw: linked control and 18 corrupt-image mutants")


if __name__ == "__main__":
    main()
