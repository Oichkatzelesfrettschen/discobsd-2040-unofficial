"""Check configured kernel name exports against linked symbol addresses."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_devsw import get_entry
from check_rp2040_execsw import offset_for, read_string, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def check_data(data, object_data):
    elf = ELFFile(io.BytesIO(data))
    obj = ELFFile(io.BytesIO(object_data))
    for image, kind in ((elf, "ET_EXEC"), (obj, "ET_REL")):
        if (image.elfclass != 32 or not image.little_endian or
                image.header.e_machine != "EM_ARM" or image.header.e_type != kind):
            raise ValueError("expected linked ARM32 ELF and ARM32 relocatable object")
    linked_symbols = elf.get_section_by_name(".symtab")
    object_symbols = obj.get_section_by_name(".symtab")
    if linked_symbols is None or object_symbols is None:
        raise ValueError("symbol table is absent")
    original = get_entry(object_symbols, "nlist")
    linked = get_entry(linked_symbols, "nlist")
    if (original.st_size != linked.st_size or original.st_size < 16 or
            original.st_size % 8 or original.st_value % 4 or linked.st_value % 4):
        raise ValueError("nlist size or alignment differs")
    source = obj.get_section(original.st_shndx).data()
    if original.st_value + original.st_size > len(source):
        raise ValueError("nlist object is truncated")
    linked_start = offset_for(elf, data, linked.st_value, linked.st_size)
    relocations = {}
    for section in obj.iter_sections():
        if (not isinstance(section, RelocationSection) or
                section.header.sh_info != original.st_shndx):
            continue
        if section.is_RELA():
            raise ValueError("RELA nlist relocations are unsupported")
        symbols = obj.get_section(section.header.sh_link)
        for relocation in section.iter_relocations():
            relative = relocation.entry.r_offset - original.st_value
            if not 0 <= relative < original.st_size:
                continue
            if (relative % 4 or relative in relocations or relocation.entry.r_info_type != 2 or
                    relative >= original.st_size - 8):
                raise ValueError("invalid nlist relocation")
            relocations[relative] = symbols.get_symbol(relocation.entry.r_info_sym)
    entries = []
    for index in range(original.st_size // 8 - 1):
        relative = index * 8
        name_symbol, target = relocations.get(relative), relocations.get(relative + 4)
        if name_symbol is None or target is None or name_symbol.entry.st_info.type != "STT_SECTION":
            raise ValueError(f"nlist {index} relocation differs")
        name_section = obj.get_section(name_symbol.entry.st_shndx)
        name_data = name_section.data()
        addend, target_addend = struct.unpack_from("<II", source, original.st_value + relative)
        start = name_symbol.entry.st_value + addend
        end = name_data.find(b"\0", start)
        if start >= len(name_data) or end < 0 or end == start or target_addend or not target.name:
            raise ValueError(f"nlist {index} object value differs")
        try:
            name = name_data[start:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError(f"nlist {index} name is invalid") from error
        if name != "_" + target.name or (name, target.name) in entries:
            raise ValueError(f"nlist {index} object name differs")
        name_pointer, address = struct.unpack_from("<II", data, linked_start + relative)
        if read_string(elf, data, name_pointer) != name.encode("ascii"):
            raise ValueError(f"nlist {index} name differs")
        kind = "STT_NOTYPE" if target.name == "u" else "STT_OBJECT"
        if address != symbol(linked_symbols, target.name, kind):
            raise ValueError(f"nlist {index} address differs")
        entries.append((name, target.name))
    if (struct.unpack_from("<II", source, original.st_value + original.st_size - 8) != (0, 0) or
            struct.unpack_from("<II", data, linked_start + linked.st_size - 8) != (0, 0)):
        raise ValueError("nlist terminator differs")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", required=True, type=Path)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    try:
        entries = check_data(args.kernel.read_bytes(), args.object.read_bytes())
        print(f"PASS RP2040 nlist: {len(entries)} exports: {args.kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 nlist: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
