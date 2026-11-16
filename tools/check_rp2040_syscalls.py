"""Check linked RP2040 syscall dispatch values against the syscall master."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from gen_kernel_metadata import MetadataError, parse_syscalls


def selected(row, inet, glob):
    if row.condition in ("IFNET", "ERRNET") and not inet:
        return ("nonet" if row.condition == "ERRNET" else "nosys"), 0
    if row.condition == "GLOB" and not glob:
        return "nosys", row.nargs
    return row.handler, row.nargs


def check_data(data, rows, *, inet, glob):
    if not rows:
        raise ValueError("empty syscall master")
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_EXEC"):
        raise ValueError("expected a linked little-endian ARM32 ELF")
    table = elf.get_section_by_name(".symtab")
    if table is None or table.header.sh_type != "SHT_SYMTAB":
        raise ValueError("symbol table is absent")
    count = symbol(table, "nsysent", "STT_OBJECT", 4)
    handlers = symbol(table, "syscall_handlers", "STT_OBJECT", len(rows) * 4)
    nargs = symbol(table, "syscall_nargs", "STT_OBJECT", len(rows))
    if count & 3 or handlers & 3:
        raise ValueError("syscall dispatch objects are misaligned")
    if struct.unpack_from("<I", data, offset_for(elf, data, count, 4))[0] != len(rows):
        raise ValueError("initialized syscall count differs")
    pointers = struct.unpack_from(f"<{len(rows)}I", data,
                                  offset_for(elf, data, handlers, len(rows) * 4))
    counts_offset = offset_for(elf, data, nargs, len(rows))
    for index, row in enumerate(rows):
        handler, argc = selected(row, inet, glob)
        expected = symbol(table, handler, "STT_FUNC")
        if pointers[index] != expected or not pointers[index] & 1:
            raise ValueError(f"syscall {index} pointer differs from {handler}")
        offset_for(elf, data, pointers[index] & ~1, 2, executable=True)
        if data[counts_offset + index] != argc:
            raise ValueError(f"syscall {index} argument count differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master", required=True, type=Path)
    parser.add_argument("--inet", action="store_true")
    parser.add_argument("--glob", action="store_true")
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        rows = parse_syscalls(args.master)
        for kernel in args.kernels:
            check_data(kernel.read_bytes(), rows, inet=args.inet, glob=args.glob)
            print(f"PASS RP2040 syscall dispatch: {kernel}")
    except (OSError, ValueError, MetadataError, ELFError, struct.error,
            IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 syscall dispatch: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
