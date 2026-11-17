"""Check linked RP2040 generated devices and their typed probe descriptors."""

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

DRIVERS = {
    "uartdriver": (b"uart", "uartprobe"),
    "uartusbdriver": (b"uartusb", "usbprobe"),
    "picocalcdriver": (b"picocalc", "lcdprobe"),
    "sddriver": (b"sd", "sdprobe"),
    "psramdriver": (b"psram", "psramprobe"),
}
DEVICE_SIZE = 24
SHF_WRITE = 1
# Functions a Config "service" line may name; main() calls each entry of
# conf_service_init in table order after the devices attach.
SERVICES = ("sdrootattach", "sdswapattach")
SERVICE_SIZE = 4


def zero_object(elf, symbols, name, size):
    entry = get_entry(symbols, name)
    section = elf.get_section(entry.st_shndx)
    if (entry.st_size != size or section.header.sh_type != "SHT_NOBITS" or
            not section.header.sh_flags & 2 or not section.header.sh_flags & 1 or
            entry.st_value < section.header.sh_addr or
            entry.st_value + size > section.header.sh_addr + section.header.sh_size):
        raise ValueError(f"{name} is not zero-initialized")
    matches = [segment for segment in elf.iter_segments() if segment.header.p_type == "PT_LOAD" and
               segment.header.p_flags & 2 and
               segment.header.p_vaddr <= entry.st_value and
               entry.st_value + size <= segment.header.p_vaddr + segment.header.p_memsz and
               entry.st_value >= segment.header.p_vaddr + segment.header.p_filesz]
    if len(matches) != 1:
        raise ValueError(f"{name} is not in a unique zero-fill load segment")
    start = symbol(symbols, "_sbss", "STT_NOTYPE")
    end = symbol(symbols, "_ebss", "STT_NOTYPE")
    if not (start <= entry.st_value and entry.st_value + size <= end):
        raise ValueError(f"{name} is outside the startup bss range")


def read_only(elf, entry, name):
    section = elf.get_section(entry.st_shndx)
    if section.header.sh_type != "SHT_PROGBITS" or section.header.sh_flags & SHF_WRITE:
        raise ValueError(f"{name} is writable")
    matches = [segment for segment in elf.iter_segments() if segment.header.p_type == "PT_LOAD" and
               not segment.header.p_flags & 2 and
               segment.header.p_vaddr <= entry.st_value and
               entry.st_value + entry.st_size <= segment.header.p_vaddr + segment.header.p_filesz]
    if len(matches) != 1:
        raise ValueError(f"{name} is not in a unique read-only load segment")


def object_relocations(obj, original, limit, stride, table):
    """Map each ABS32 relocation inside a table's entries to its symbol name."""
    relocations = {}
    for section in obj.iter_sections():
        if (not isinstance(section, RelocationSection) or
                section.header.sh_info != original.st_shndx):
            continue
        if section.is_RELA():
            raise ValueError("RELA configuration relocations are unsupported")
        symbols = obj.get_section(section.header.sh_link)
        for relocation in section.iter_relocations():
            offset = relocation.entry.r_offset - original.st_value
            if not 0 <= offset < original.st_size:
                continue
            if (offset % stride or offset >= limit or
                    offset in relocations or relocation.entry.r_info_type != 2):
                raise ValueError(f"unsupported {table} relocation")
            relocations[offset] = symbols.get_symbol(relocation.entry.r_info_sym).name
    return relocations


def check_services(elf, data, obj, linked_symbols, object_symbols):
    """Check conf_service_init: a read-only table of allowed Thumb function
    pointers in generated order and a zero terminator, the terminator alone
    for a kernel whose Config names no service. Returns the names."""
    original = get_entry(object_symbols, "conf_service_init")
    linked = get_entry(linked_symbols, "conf_service_init")
    if (original.st_size != linked.st_size or original.st_size < SERVICE_SIZE or
            original.st_size % SERVICE_SIZE):
        raise ValueError("conf_service_init size differs")
    section = obj.get_section(original.st_shndx)
    if section.header.sh_type != "SHT_PROGBITS" or section.header.sh_flags & SHF_WRITE:
        raise ValueError("conf_service_init object is not read-only data")
    source = section.data()
    if original.st_value + original.st_size > len(source):
        raise ValueError("conf_service_init object is truncated")
    read_only(elf, linked, "conf_service_init")
    entries = original.st_size // SERVICE_SIZE - 1
    relocations = object_relocations(obj, original, entries * SERVICE_SIZE,
                                     SERVICE_SIZE, "service")
    table_offset = offset_for(elf, data, linked.st_value, linked.st_size)
    names = []
    for index in range(entries + 1):
        relative = index * SERVICE_SIZE
        raw = struct.unpack_from("<I", source, original.st_value + relative)[0]
        value = struct.unpack_from("<I", data, table_offset + relative)[0]
        if index < entries:
            target = relocations.get(relative)
            if target not in SERVICES or raw != 0:
                raise ValueError(f"unknown service function at entry {index}")
            expected = symbol(linked_symbols, target, "STT_FUNC")
            if not expected & 1:
                raise ValueError(f"{target} is not a Thumb function")
            offset_for(elf, data, expected & ~1, 2, executable=True)
            names.append(target)
        else:
            if raw != 0:
                raise ValueError("nonzero service terminator")
            expected = 0
        if value != expected:
            raise ValueError(f"conf_service_init entry {index} differs")
    return names


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
    original = get_entry(object_symbols, "conf_ctlr_init")
    if (original.st_size != 24 or
            obj.get_section(original.st_shndx).header.sh_type != "SHT_NOBITS"):
        raise ValueError("conf_ctlr_init object is not an empty table")
    zero_object(elf, linked_symbols, "conf_ctlr_init", 24)
    check_services(elf, data, obj, linked_symbols, object_symbols)
    original = get_entry(object_symbols, "conf_device_init")
    linked = get_entry(linked_symbols, "conf_device_init")
    if (original.st_size != linked.st_size or original.st_size < DEVICE_SIZE * 2 or
            original.st_size % DEVICE_SIZE):
        raise ValueError("conf_device_init size differs")
    source = obj.get_section(original.st_shndx).data()
    if original.st_value + original.st_size > len(source):
        raise ValueError("conf_device_init object is truncated")
    read_only(elf, linked, "conf_device_init")
    table_offset = offset_for(elf, data, linked.st_value, linked.st_size)
    entries = original.st_size // DEVICE_SIZE - 1
    relocations = object_relocations(obj, original, entries * DEVICE_SIZE,
                                     DEVICE_SIZE, "device configuration")
    names = []
    for index in range(original.st_size // 4):
        relative = index * 4
        raw = struct.unpack_from("<I", source, original.st_value + relative)[0]
        value = struct.unpack_from("<I", data, table_offset + relative)[0]
        target = relocations.get(relative)
        if relative % DEVICE_SIZE == 0 and relative // DEVICE_SIZE < entries:
            if target not in DRIVERS or raw != 0:
                raise ValueError(f"unknown device driver at field {index}")
            expected = symbol(linked_symbols, target, "STT_OBJECT", 8)
            names.append(target)
        else:
            if target is not None or (relative >= entries * DEVICE_SIZE and raw != 0):
                raise ValueError("unexpected relocation or nonzero device terminator")
            expected = raw
        if value != expected:
            raise ValueError(f"conf_device_init field {index} differs")
    for name in set(names):
        driver = symbol(linked_symbols, name, "STT_OBJECT", 8)
        read_only(elf, get_entry(linked_symbols, name), name)
        driver_offset = offset_for(elf, data, driver, 8)
        string, probe = struct.unpack_from("<II", data, driver_offset)
        expected_name, expected_probe = DRIVERS[name]
        if read_string(elf, data, string) != expected_name:
            raise ValueError(f"{name} name differs")
        if probe != symbol(linked_symbols, expected_probe, "STT_FUNC") or not probe & 1:
            raise ValueError(f"{name} probe differs")
        offset_for(elf, data, probe & ~1, 2, executable=True)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", required=True, type=Path)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    try:
        check_data(args.kernel.read_bytes(), args.object.read_bytes())
        print(f"PASS RP2040 generated device descriptors: {args.kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 generated device descriptors: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
