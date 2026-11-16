"""Check linked device switches against relocations in the configured conf.o."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def get_entry(table, name):
    entries = [item.entry for item in table.get_symbol_by_name(name) or []
               if item.entry.st_shndx != "SHN_UNDEF"]
    if len(entries) != 1 or entries[0].st_info.type != "STT_OBJECT":
        raise ValueError(f"expected one defined object {name}")
    return entries[0]


def check_data(data, object_data):
    elf = ELFFile(io.BytesIO(data))
    obj = ELFFile(io.BytesIO(object_data))
    for image, expected_type in ((elf, "ET_EXEC"), (obj, "ET_REL")):
        if (image.elfclass != 32 or not image.little_endian or
                image.header.e_machine != "EM_ARM" or image.header.e_type != expected_type):
            raise ValueError("expected linked ARM32 ELF and ARM32 relocatable object")
    linked_symbols = elf.get_section_by_name(".symtab")
    object_symbols = obj.get_section_by_name(".symtab")
    if linked_symbols is None or object_symbols is None:
        raise ValueError("symbol table is absent")
    for table_name, count_name, stride, data_field in (
            ("bdevsw", "nblkdev", 6, None), ("cdevsw", "nchrdev", 11, 6)):
        source = get_entry(object_symbols, table_name)
        linked = get_entry(linked_symbols, table_name)
        if source.st_size != linked.st_size or not source.st_size or source.st_size % (stride * 4):
            raise ValueError(f"{table_name} size differs")
        source_section = obj.get_section(source.st_shndx)
        source_bytes = source_section.data()
        source_start = source.st_value
        if source_start + source.st_size > len(source_bytes):
            raise ValueError(f"{table_name} object is truncated")
        linked_start = offset_for(elf, data, linked.st_value, linked.st_size)
        expected_count = source.st_size // (stride * 4) - 1
        source_count = get_entry(object_symbols, count_name)
        source_count_bytes = obj.get_section(source_count.st_shndx).data()
        declared = struct.unpack_from("<I", source_count_bytes, source_count.st_value)[0]
        linked_count = symbol(linked_symbols, count_name, "STT_OBJECT", 4)
        actual_count = struct.unpack_from("<I", data, offset_for(elf, data, linked_count, 4))[0]
        if source_count.st_size != 4 or declared != expected_count or actual_count != declared:
            raise ValueError(f"{count_name} count differs")
        relocations = {}
        for section in obj.iter_sections():
            if (not isinstance(section, RelocationSection) or
                    section.header.sh_info != source.st_shndx):
                continue
            if section.is_RELA():
                raise ValueError("RELA device-switch relocations are unsupported")
            symbols = obj.get_section(section.header.sh_link)
            for relocation in section.iter_relocations():
                offset = relocation.entry.r_offset
                if not source_start <= offset < source_start + source.st_size:
                    continue
                if offset in relocations or offset % 4 or relocation.entry.r_info_type != 2:
                    raise ValueError(f"invalid {table_name} relocation")
                relocations[offset] = symbols.get_symbol(relocation.entry.r_info_sym).name
        for index in range(source.st_size // 4):
            relative = index * 4
            source_word = struct.unpack_from("<I", source_bytes, source_start + relative)[0]
            value = struct.unpack_from("<I", data, linked_start + relative)[0]
            target = relocations.get(source_start + relative)
            field = index % stride
            pointer = table_name == "cdevsw" or field != 5
            if target is None:
                if pointer and source_word != 0:
                    raise ValueError(f"{table_name} unrelocated pointer field {index}")
                expected = source_word
            else:
                if not pointer or source_word != 0 or not target:
                    raise ValueError(f"{table_name} unsupported relocation field {index}")
                kind = "STT_OBJECT" if field == data_field else "STT_FUNC"
                expected = symbol(linked_symbols, target, kind)
            if value != expected:
                raise ValueError(f"{table_name} field {index} differs")
            if target is not None and field != data_field:
                if not value & 1:
                    raise ValueError(f"{table_name} field {index} lacks Thumb bit")
                offset_for(elf, data, value & ~1, 2, executable=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", required=True, type=Path)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    try:
        check_data(args.kernel.read_bytes(), args.object.read_bytes())
        print(f"PASS RP2040 device switches: {args.kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 device switches: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
