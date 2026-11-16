"""Calibrate linked device-switch fields against the configured object."""

import argparse
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import check_rp2040_devsw as checker  # noqa: E402


def symbol_offset(elf, name):
    entries = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    if entries is None or len(entries) != 1:
        raise AssertionError(f"missing or duplicate {name}")
    entry = entries[0].entry
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
    checker.check_data(data, obj)
    elf = ELFFile(io.BytesIO(data))
    changed = 0
    for name in ("bdevsw", "cdevsw"):
        offset, size = symbol_offset(elf, name)
        for index in range(size // 4):
            modified = bytearray(data)
            old = struct.unpack_from("<I", modified, offset + index * 4)[0]
            struct.pack_into("<I", modified, offset + index * 4, old ^ 4)
            reject(modified, obj, f"{name} field {index} differs")
            changed += 1
    for name in ("nblkdev", "nchrdev"):
        offset, _ = symbol_offset(elf, name)
        modified = bytearray(data)
        struct.pack_into("<I", modified, offset, 0)
        reject(modified, obj, f"{name} count differs")
    block_offset, _ = symbol_offset(elf, "bdevsw")
    modified = bytearray(data)
    swap_open = struct.unpack_from("<I", data, block_offset + 4 * 6 * 4)[0]
    struct.pack_into("<I", modified, block_offset, swap_open)
    reject(modified, obj, "bdevsw field 0 differs")
    object_elf = ELFFile(io.BytesIO(obj))
    object_table = object_elf.get_section_by_name(".symtab")
    cdev_section = object_table.get_symbol_by_name("cdevsw")[0].entry.st_shndx
    relocation_section = next(section for section in object_elf.iter_sections()
                              if isinstance(section, RelocationSection) and
                              section.header.sh_info == cdev_section)
    object_symbols = object_elf.get_section(relocation_section.header.sh_link)
    close_index = next(index for index, entry in enumerate(object_symbols.iter_symbols())
                       if entry.name == "cnclose")
    for index, relocation in enumerate(relocation_section.iter_relocations()):
        name = object_symbols.get_symbol(relocation.entry.r_info_sym).name
        if name == "cnopen":
            mutated_object = bytearray(obj)
            info_offset = relocation_section.header.sh_offset + index * 8 + 4
            struct.pack_into("<I", mutated_object, info_offset, (close_index << 8) | 2)
            reject(data, mutated_object, "cdevsw field 0 differs")
            break
    else:
        raise AssertionError("cnopen object relocation is absent")
    with tempfile.TemporaryDirectory(prefix="rp2040-devsw-") as directory:
        mutant = Path(directory) / "truncated.elf"
        mutant.write_bytes(data[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_devsw.py"),
                                 "--object", str(args.object), str(mutant)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 devsw: {changed} changed fields and count mutants rejected")


if __name__ == "__main__":
    main()
