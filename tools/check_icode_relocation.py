"""Check the copied RP2040 process-1 entry code and its Thumb addresses."""

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "sys/arch/rp2040/rp2040/locore.S"
USER_BASE = 0x20000000
# MRS CONTROL; MOVS r1,1; ORRS; MSR CONTROL; ISB.
PRIVILEGE_SEQUENCE = bytes.fromhex("eff314800121084380f31488bff36f8f")


def fail(message):
    raise ValueError(message)


def check_image(image, positions):
    start = positions["icode"]
    end = positions["icodeend"]
    path = positions["sbininit"]
    flags = positions["initflags"]
    argv = positions["argv"]
    if not (0 <= start < path < flags < argv < end <= len(image)):
        fail("initial image symbol bounds")
    if image[start : start + 16] != PRIVILEGE_SEQUENCE:
        fail("privilege transition sequence")
    for offset, register, target in ((16, 0, path), (18, 1, argv)):
        instruction = struct.unpack_from("<H", image, start + offset)[0]
        if instruction & 0xFF00 != 0xA000 | register << 8:
            fail("address instruction opcode")
        copied_pc = (USER_BASE + offset + 4) & ~3
        address = copied_pc + (instruction & 0xFF) * 4
        if address != USER_BASE + target - start:
            fail("address instruction target")
    if image[start + 20 : start + 24] != bytes.fromhex("0bdf01df"):
        fail("exec and exit supervisor calls")
    if image[path : path + 11] != b"/sbin/init\0":
        fail("initial executable path")
    if image[flags : flags + 2] != b"-\0":
        fail("initial option string")
    expected = (USER_BASE + path + 6 - start, USER_BASE + flags - start, 0)
    if struct.unpack_from("<III", image, argv) != expected:
        fail("initial argument vector")


def read_object(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM":
            fail("initial image must be 32-bit little-endian ARM")
        symbols = elf.get_section_by_name(".symtab")
        names = ("icode", "icodeend", "sbininit", "initflags", "argv")
        selected = {}
        for symbol in symbols.iter_symbols():
            if symbol.name in names:
                if symbol.name in selected:
                    fail("duplicate initial image symbol")
                selected[symbol.name] = symbol
        if set(selected) != set(names):
            fail("missing initial image symbol")
        section_index = selected["icode"].entry.st_shndx
        if not isinstance(section_index, int) or any(
            symbol.entry.st_shndx != section_index for symbol in selected.values()
        ):
            fail("initial image symbols do not share a section")
        image = elf.get_section(section_index).data()
        positions = {name: symbol.entry.st_value for name, symbol in selected.items()}
        for section in elf.iter_sections():
            if section.header.sh_type not in ("SHT_REL", "SHT_RELA"):
                continue
            if section.header.sh_info != section_index:
                continue
            if any(positions["icode"] <= relocation.entry.r_offset < positions["icodeend"]
                   for relocation in section.iter_relocations()):
                fail("initial image contains unresolved relocations")
        return image, positions


def compile_object(command, source, output):
    result = subprocess.run(
        [*command, "-g", "-mcpu=cortex-m0plus", "-mthumb", "-mabi=aapcs",
         "-mlittle-endian", "-mfloat-abi=soft", "-Wall", "-Wextra", "-Werror",
         "-Wshadow", "-Wundef", "-Wformat=2", "-ffreestanding", "-fno-builtin",
         "-fno-common", "-fno-strict-aliasing", "-x", "assembler-with-cpp", "-I", str(ROOT / "sys"),
         "-c", str(source), "-o", str(output)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        fail(f"{command[0]} failed to assemble initial image: {result.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default="arm-none-eabi-gcc")
    args = parser.parse_args()
    gcc = shutil.which(args.gcc)
    clang = shutil.which("clang")
    if not gcc or not clang:
        fail("the selected ARM GCC and clang are required")
    with tempfile.TemporaryDirectory(prefix="discobsd-icode-") as directory:
        temporary = Path(directory)
        for label, compiler in (("gcc", [gcc]), ("clang", [clang, "--target=arm-none-eabi"])):
            obj = temporary / f"{label}.o"
            compile_object(compiler, SOURCE, obj)
            image, positions = read_object(obj)
            check_image(image, positions)
            for offset in (16, 18):
                mutant = bytearray(image)
                mutant[positions["icode"] + offset] ^= 1
                try:
                    check_image(mutant, positions)
                except ValueError as error:
                    if str(error) != "address instruction target":
                        raise
                else:
                    fail("altered address instruction was accepted")
            mutant = bytearray(image)
            mutant[positions["argv"]] ^= 1
            try:
                check_image(mutant, positions)
            except ValueError as error:
                if str(error) != "initial argument vector":
                    raise
            else:
                fail("altered argument vector was accepted")
            mutant = bytearray(image)
            mutant[positions["icode"] + 17] = 0x20
            try:
                check_image(mutant, positions)
            except ValueError as error:
                if str(error) != "address instruction opcode":
                    raise
            else:
                fail("immediate-move mutant was accepted")
            print(f"PASS {label}: copied initial image and four mutants")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"FAIL initial image: {error}", file=sys.stderr)
        sys.exit(1)
