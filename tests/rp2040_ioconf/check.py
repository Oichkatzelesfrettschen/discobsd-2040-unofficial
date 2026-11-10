"""Calibrate linked generated device and driver descriptor checks."""

import argparse
import io
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import check_rp2040_ioconf as checker  # noqa: E402


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


def check_driver_source(source, name):
    match = re.search(r"\bstruct\s+driver\s+" + name +
                      r'\s*=\s*\{\s*"([a-z]+)"\s*,\s*([a-z]+)\s*,?\s*\}', source)
    expected_name, expected_probe = checker.DRIVERS[name]
    if match is None or match.groups() != (expected_name.decode("ascii"), expected_probe):
        raise ValueError(f"{name} source initializer differs")
    return match


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--object", required=True, type=Path)
    args = parser.parse_args()
    data, obj = args.kernel.read_bytes(), args.object.read_bytes()
    names = checker.check_data(data, obj)
    generated = args.object.with_suffix(".c").read_text(encoding="utf-8")
    source_names = re.findall(r"^\s*\{\s*&([a-z_]+driver)\s*,", generated, re.MULTILINE)
    if source_names != names:
        raise AssertionError("generated source driver order differs from the object")
    driver_sources = {"uartdriver": "uart.c", "uartusbdriver": "usb.c",
                      "picocalcdriver": "lcdcons.c", "sddriver": "sd.c",
                      "psramdriver": "psram.c"}
    for name in names:
        source = (ROOT / "sys/arch/rp2040/dev" / driver_sources[name]).read_text(encoding="utf-8")
        match = check_driver_source(source, name)
        mutant = source[:match.start(2)] + "wrongprobe" + source[match.end(2):]
        try:
            check_driver_source(mutant, name)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{name} source mutation survived")
    if len(names) < 2:
        raise AssertionError("fixture needs at least two distinct drivers")
    elf = ELFFile(io.BytesIO(data))
    start, size = symbol_offset(elf, "conf_device_init")
    for index in range(size // 4):
        mutant = bytearray(data)
        original = struct.unpack_from("<I", mutant, start + index * 4)[0]
        struct.pack_into("<I", mutant, start + index * 4, original ^ 4)
        reject(mutant, obj, f"conf_device_init field {index} differs")
    for name in names:
        driver_offset, _ = symbol_offset(elf, name)
        mutant = bytearray(data)
        other = next(candidate for candidate in names if candidate != name)
        other_offset, _ = symbol_offset(elf, other)
        wrong = struct.unpack_from("<I", data, other_offset + 4)[0]
        struct.pack_into("<I", mutant, driver_offset + 4, wrong)
        reject(mutant, obj, f"{name} probe differs")
        mutant = bytearray(data)
        original = struct.unpack_from("<I", data, driver_offset + 4)[0]
        struct.pack_into("<I", mutant, driver_offset + 4, original & ~1)
        reject(mutant, obj, f"{name} probe differs")
        mutant = bytearray(data)
        alternate_name = struct.unpack_from("<I", data, other_offset)[0]
        struct.pack_into("<I", mutant, driver_offset, alternate_name)
        reject(mutant, obj, f"{name} name differs")
    object_elf = ELFFile(io.BytesIO(obj))
    object_symbols = object_elf.get_section_by_name(".symtab")
    device = object_symbols.get_symbol_by_name("conf_device_init")[0].entry
    relocation_section = next(section for section in object_elf.iter_sections()
                              if isinstance(section, RelocationSection) and
                              section.header.sh_info == device.st_shndx)
    relocation_symbols = object_elf.get_section(relocation_section.header.sh_link)
    alternate_index = next(index for index, item in enumerate(relocation_symbols.iter_symbols())
                           if item.name == names[1])
    for index, relocation in enumerate(relocation_section.iter_relocations()):
        if relocation.entry.r_offset == device.st_value:
            mutant = bytearray(obj)
            info_offset = relocation_section.header.sh_offset + index * 8 + 4
            struct.pack_into("<I", mutant, info_offset, (alternate_index << 8) | 2)
            reject(data, mutant, "conf_device_init field 0 differs")
            break
    else:
        raise AssertionError("first driver relocation is absent")
    section_index = elf.get_section_by_name(".symtab").get_symbol_by_name(
        "conf_service_init")[0].entry.st_shndx
    mutant = bytearray(data)
    section_type_offset = elf.header.e_shoff + section_index * elf.header.e_shentsize + 4
    struct.pack_into("<I", mutant, section_type_offset, 1)
    reject(mutant, obj, "conf_ctlr_init is not zero-initialized")
    symbols = elf.get_section_by_name(".symtab")
    bss_index = next(index for index, item in enumerate(symbols.iter_symbols())
                     if item.name == "_sbss")
    service = symbols.get_symbol_by_name("conf_service_init")[0].entry.st_value
    mutant = bytearray(data)
    symbol_value = symbols.header.sh_offset + bss_index * symbols.header.sh_entsize + 4
    struct.pack_into("<I", mutant, symbol_value, service + 4)
    reject(mutant, obj, "outside the startup bss range")
    with tempfile.TemporaryDirectory(prefix="rp2040-ioconf-") as directory:
        truncated = Path(directory) / "truncated.elf"
        truncated.write_bytes(data[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_ioconf.py"),
                                 "--object", str(args.object), str(truncated)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 ioconf: {size // 4} device words and {len(names)} probe mutants")


if __name__ == "__main__":
    main()
