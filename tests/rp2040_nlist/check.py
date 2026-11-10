"""Calibrate linked kernel name-to-address exports."""

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
sys.path.insert(0, str(ROOT / "tools"))
import check_rp2040_nlist as checker  # noqa: E402


def symbol_offset(elf, name):
    symbols = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    if symbols is None or len(symbols) != 1:
        raise AssertionError(f"missing or duplicate {name}")
    entry = symbols[0].entry
    section = elf.get_section(entry.st_shndx)
    return section.header.sh_offset + entry.st_value - section.header.sh_addr, entry.st_size


def reject(data, obj, diagnostic):
    try:
        checker.check_data(data, obj)
    except ValueError as error:
        if diagnostic in str(error):
            return
        raise AssertionError(f"unexpected diagnostic: {error}") from error
    raise AssertionError(f"accepted mutant: {diagnostic}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--object", required=True, type=Path)
    args = parser.parse_args()
    data, obj = args.kernel.read_bytes(), args.object.read_bytes()
    entries = checker.check_data(data, obj)
    source = (ROOT / "sys/arch/rp2040/rp2040/sysctl.c").read_text(encoding="utf-8")
    body = source.split("} nlist[] = {", 1)[1].split("\n};", 1)[0]
    pairs = re.findall(r'\{\s*"([^"]+)"\s*,\s*\(int\)&([A-Za-z0-9_]+)', body)
    if len(pairs) != len(set(pairs)) or any(pair not in pairs for pair in entries):
        raise AssertionError("object exports differ from source initializer")
    elf = ELFFile(io.BytesIO(data))
    table, size = symbol_offset(elf, "nlist")
    if size != (len(entries) + 1) * 8:
        raise AssertionError("unexpected table size")
    for index, (name, _) in enumerate(entries):
        name_ptr, address = struct.unpack_from("<II", data, table + index * 8)
        for relative, replacement, diagnostic in (
                (0, name_ptr ^ 4, f"nlist {index} name differs"),
                (4, address ^ 4, f"nlist {index} address differs")):
            mutant = bytearray(data)
            struct.pack_into("<I", mutant, table + index * 8 + relative, replacement)
            reject(mutant, obj, diagnostic)
    mutant = bytearray(data)
    struct.pack_into("<I", mutant, table + len(entries) * 8, 1)
    reject(mutant, obj, "nlist terminator differs")
    with tempfile.TemporaryDirectory(prefix="rp2040-nlist-") as directory:
        truncated = Path(directory) / "truncated.elf"
        truncated.write_bytes(data[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_nlist.py"),
                                 "--object", str(args.object), str(truncated)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 nlist: {len(entries)} names and linked address mutants")


if __name__ == "__main__":
    main()
