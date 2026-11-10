"""Check the linked RP2040 executable-format dispatch table's pointer values."""

import argparse
import io
import struct
import sys
from pathlib import Path

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

FORMATS = (("exec_hsaout_check", b"a.out.hs"), ("exec_aout_check", b"a.out"),
           ("exec_elf_check", b"elf"), ("exec_script_check", b"script"))


def symbol(table, name, kind, size=None):
    candidates = [item for item in table.get_symbol_by_name(name) or []
                  if item.entry.st_shndx != "SHN_UNDEF"]
    if len(candidates) != 1:
        raise ValueError(f"execsw: expected one defined {name} symbol")
    entry = candidates[0].entry
    if entry.st_info.type != kind or (size is not None and entry.st_size != size):
        raise ValueError(f"execsw: invalid {name} symbol type or size")
    return entry.st_value


def offset_for(elf, data, address, size, executable=False):
    matches = []
    for segment in elf.iter_segments():
        header = segment.header
        if header.p_type != "PT_LOAD" or (executable and not header.p_flags & 1):
            continue
        displacement = address - header.p_vaddr
        if 0 <= displacement and displacement + size <= header.p_filesz:
            offset = header.p_offset + displacement
            if offset + size > len(data):
                raise ValueError("execsw: truncated load segment")
            matches.append(offset)
    if len(matches) != 1:
        raise ValueError("execsw: address lacks one file-backed load segment")
    return matches[0]


def read_string(elf, data, address):
    result = bytearray()
    for index in range(32):
        offset = offset_for(elf, data, address + index, 1)
        value = data[offset]
        if value == 0:
            return bytes(result)
        result.append(value)
    raise ValueError("execsw: unterminated format name")


def check(path):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_EXEC"):
        raise ValueError("execsw: expected a linked little-endian ARM32 ELF")
    table = elf.get_section_by_name(".symtab")
    if table is None or table.header.sh_type != "SHT_SYMTAB":
        raise ValueError("execsw: symbol table is absent")
    address = symbol(table, "execsw", "STT_OBJECT", len(FORMATS) * 8)
    count_address = symbol(table, "nexecs", "STT_OBJECT", 4)
    if address & 3 or count_address & 3:
        raise ValueError("execsw: table or count is misaligned")
    count_offset = offset_for(elf, data, count_address, 4)
    if struct.unpack_from("<I", data, count_offset)[0] != len(FORMATS):
        raise ValueError("execsw: initialized format count differs")
    table_offset = offset_for(elf, data, address, len(FORMATS) * 8)
    targets = set()
    for index, (handler, name) in enumerate(FORMATS):
        pointer, name_pointer = struct.unpack_from("<II", data, table_offset + index * 8)
        expected = symbol(table, handler, "STT_FUNC")
        if pointer != expected or not pointer & 1:
            raise ValueError(f"execsw: {name.decode()} handler pointer differs from {handler}")
        offset_for(elf, data, pointer & ~1, 2, executable=True)
        if pointer in targets:
            raise ValueError("execsw: format handlers alias")
        targets.add(pointer)
        if read_string(elf, data, name_pointer) != name:
            raise ValueError(f"execsw: {handler} format name differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        for kernel in args.kernels:
            check(kernel)
            print(f"PASS execsw pointer values: {kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL execsw: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
