"""Check linked RP2040 file-operation tables and callback pointers."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

TABLES = {
    "Fops": ("NULL", "inodeops", "socketops", "pipeops"),
    "inodeops": ("ino_rw", "ino_ioctl", "ino_select", "vn_closefile"),
    "socketops": ("sorw", "soctl", "sosel", "socls"),
    "pipeops": ("pipe_rw", "ino_ioctl", "pipe_select", "pipe_close"),
}


def check_data(data):
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_EXEC"):
        raise ValueError("expected a linked little-endian ARM32 ELF")
    symbols = elf.get_section_by_name(".symtab")
    if symbols is None or symbols.header.sh_type != "SHT_SYMTAB":
        raise ValueError("symbol table is absent")
    for name, targets in TABLES.items():
        address = symbol(symbols, name, "STT_OBJECT", len(targets) * 4)
        if address & 3:
            raise ValueError(f"{name} is misaligned")
        offset = offset_for(elf, data, address, len(targets) * 4)
        words = struct.unpack_from(f"<{len(targets)}I", data, offset)
        for index, (target, value) in enumerate(zip(targets, words)):
            if target == "NULL":
                expected = 0
            else:
                expected = symbol(symbols, target, "STT_OBJECT" if name == "Fops" else "STT_FUNC")
            if value != expected:
                raise ValueError(f"{name} field {index} differs from {target}")
            if name != "Fops":
                if not value & 1:
                    raise ValueError(f"{name} field {index} lacks the Thumb bit")
                offset_for(elf, data, value & ~1, 2, executable=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        for kernel in args.kernels:
            check_data(kernel.read_bytes())
            print(f"PASS RP2040 fileops: {kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 fileops: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
