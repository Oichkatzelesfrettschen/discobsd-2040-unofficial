"""Check linked terminal output callback literals in configured drivers."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from check_rp2040_ioconf import check_data as check_ioconf
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

DRIVERS = {
    "uartdriver": ("uart", "uartopen", "uartstart"),
    "uartusbdriver": ("usb", "usbopen", "usbstart"),
    "picocalcdriver": ("lcdcons", "lcdopen", "lcdstart"),
}


def object_function(symbols, name):
    entries = [item.entry for item in symbols.get_symbol_by_name(name) or []
               if item.entry.st_shndx != "SHN_UNDEF"]
    if len(entries) != 1 or entries[0].st_info.type != "STT_FUNC" or not entries[0].st_size:
        raise ValueError(f"expected one defined object function {name}")
    return entries[0]


def check_data(data, ioconf, objects):
    names = check_ioconf(data, ioconf)
    elf = ELFFile(io.BytesIO(data))
    linked_symbols = elf.get_section_by_name(".symtab")
    result = []
    for name in dict.fromkeys(names):
        if name not in DRIVERS:
            continue
        component, opener, callback = DRIVERS[name]
        if component not in objects:
            raise ValueError(f"missing configured object {component}")
        obj = ELFFile(io.BytesIO(objects[component]))
        if (obj.elfclass != 32 or not obj.little_endian or
                obj.header.e_machine != "EM_ARM" or obj.header.e_type != "ET_REL"):
            raise ValueError(f"invalid ARM32 object {component}")
        symbols = obj.get_section_by_name(".symtab")
        function = object_function(symbols, opener)
        object_callback = object_function(symbols, callback)
        if not object_callback.st_value & 1:
            raise ValueError(f"{callback} object symbol lacks Thumb bit")
        start = function.st_value & ~1
        references = []
        for section in obj.iter_sections():
            if (not isinstance(section, RelocationSection) or
                    section.header.sh_info != function.st_shndx):
                continue
            if section.is_RELA():
                raise ValueError("RELA terminal callback relocations are unsupported")
            relocation_symbols = obj.get_section(section.header.sh_link)
            for relocation in section.iter_relocations():
                offset = relocation.entry.r_offset
                target = relocation_symbols.get_symbol(relocation.entry.r_info_sym).name
                if (start <= offset and offset + 4 <= start + function.st_size and
                        target == callback):
                    if offset % 4 or relocation.entry.r_info_type != 2:
                        raise ValueError(f"invalid {opener} callback relocation")
                    references.append(offset)
        if len(references) != 1:
            raise ValueError(f"{opener} must contain exactly one {callback} relocation")
        object_bytes = obj.get_section(function.st_shndx).data()
        if struct.unpack_from("<I", object_bytes, references[0])[0] != 0:
            raise ValueError(f"{opener} callback relocation has an addend")
        linked_start = symbol(linked_symbols, opener, "STT_FUNC", function.st_size) & ~1
        callback_value = symbol(linked_symbols, callback, "STT_FUNC")
        literal = linked_start + references[0] - start
        linked_offset = offset_for(elf, data, literal, 4, executable=True)
        actual = struct.unpack_from("<I", data, linked_offset)[0]
        if actual != callback_value or not actual & 1:
            raise ValueError(f"{opener} callback literal differs from {callback}")
        offset_for(elf, data, actual & ~1, 2, executable=True)
        result.append((name, component, opener, callback, literal))
    if not result:
        raise ValueError("no configured terminal drivers")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--kernel", type=Path)
    args = parser.parse_args()
    try:
        data = (args.kernel or args.build_dir / "unix.elf").read_bytes()
        ioconf = (args.build_dir / "ioconf.o").read_bytes()
        names = check_ioconf(data, ioconf)
        objects = {DRIVERS[name][0]: (args.build_dir / (DRIVERS[name][0] + ".o")).read_bytes()
                   for name in names if name in DRIVERS}
        check_data(data, ioconf, objects)
        print(f"PASS RP2040 terminal output roots: {args.build_dir}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 terminal output roots: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
