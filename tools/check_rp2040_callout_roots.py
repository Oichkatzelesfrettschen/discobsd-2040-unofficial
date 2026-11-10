"""Check linked literals for configured RP2040 callout callbacks."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from check_rp2040_ioconf import check_data as check_ioconf
from check_rp2040_tty_roots import object_function
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

CORE = (("kern_synch", "schedcpu", "schedcpu"),
        ("kern_synch", "tsleep", "endtsleep"),
        ("tty", "ttycheckoutq", "wakeup"))
PICO_CALC = (("picokbd", "kbd_poll", "kbd_poll"),
             ("picokbd", "picokbd_poll_start", "kbd_poll"),
             ("picokbd", "picokbd_reg", "kbdreq_timeout"),
             ("lcdcons", "lcdrender", "render_resume"),
             ("picoaudio", "lcd_bell", "bell_stop"))


def configured_roots(names):
    return CORE + (PICO_CALC if "picocalcdriver" in names else ())


def check_data(data, ioconf, objects):
    names = check_ioconf(data, ioconf)
    roots = configured_roots(names)
    elf = ELFFile(io.BytesIO(data))
    linked_symbols = elf.get_section_by_name(".symtab")
    result = []
    for component, owner, callback in roots:
        if component not in objects:
            raise ValueError(f"missing configured object {component}")
        obj = ELFFile(io.BytesIO(objects[component]))
        if (obj.elfclass != 32 or not obj.little_endian or
                obj.header.e_machine != "EM_ARM" or obj.header.e_type != "ET_REL"):
            raise ValueError(f"invalid ARM32 object {component}")
        symbols = obj.get_section_by_name(".symtab")
        function = object_function(symbols, owner)
        start = function.st_value & ~1
        references = []
        for section in obj.iter_sections():
            if (not isinstance(section, RelocationSection) or
                    section.header.sh_info != function.st_shndx):
                continue
            if section.is_RELA():
                raise ValueError("RELA callout relocations are unsupported")
            relocation_symbols = obj.get_section(section.header.sh_link)
            for relocation in section.iter_relocations():
                offset = relocation.entry.r_offset
                target = relocation_symbols.get_symbol(relocation.entry.r_info_sym).name
                if (start <= offset and offset + 4 <= start + function.st_size and
                        target == callback):
                    if offset % 4 or relocation.entry.r_info_type != 2:
                        raise ValueError(f"invalid {owner} callout relocation")
                    references.append(offset)
        if len(references) != 1:
            raise ValueError(f"{owner} must contain exactly one {callback} relocation")
        raw = obj.get_section(function.st_shndx).data()
        if struct.unpack_from("<I", raw, references[0])[0] != 0:
            raise ValueError(f"{owner} callout relocation has an addend")
        linked_start = symbol(linked_symbols, owner, "STT_FUNC", function.st_size) & ~1
        callback_value = symbol(linked_symbols, callback, "STT_FUNC")
        literal = linked_start + references[0] - start
        literal_offset = offset_for(elf, data, literal, 4, executable=True)
        actual = struct.unpack_from("<I", data, literal_offset)[0]
        if actual != callback_value or not actual & 1:
            raise ValueError(f"{owner} callout literal differs from {callback}")
        offset_for(elf, data, actual & ~1, 2, executable=True)
        result.append((component, owner, callback, literal))
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
        components = {root[0] for root in configured_roots(names)}
        objects = {name: (args.build_dir / (name + ".o")).read_bytes() for name in components}
        check_data(data, ioconf, objects)
        print(f"PASS RP2040 callout roots: {args.build_dir}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 callout roots: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
