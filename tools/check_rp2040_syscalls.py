"""Check linked RP2040 syscall dispatch values against the syscall master."""

import argparse
import io
import shlex
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from gen_kernel_metadata import NAME_OFFSET_BITS, NAME_POOL_LIMIT, MetadataError, parse_syscalls


def selected(row, inet, glob):
    if row.condition in ("IFNET", "ERRNET") and not inet:
        return ("nonet" if row.condition == "ERRNET" else "nosys"), 0
    if row.condition == "GLOB" and not glob:
        return "nosys", row.nargs
    return row.handler, row.nargs


def check_trace(elf, table, data, rows, enabled):
    names = ("syscall_name", "syscall_name_spans", "syscall_name_pool")
    trace = [[item.entry for item in table.get_symbol_by_name(name) or []
              if item.entry.st_shndx != "SHN_UNDEF"] for name in names]
    if not enabled:
        if any(trace):
            raise ValueError("disabled syscall trace metadata is linked")
        return
    if any(len(entries) != 1 for entries in trace):
        raise ValueError("expected one definition of each syscall trace symbol")
    decoder = symbol(table, names[0], "STT_FUNC")
    if not decoder & 1:
        raise ValueError("syscall trace decoder is not Thumb")
    offset_for(elf, data, decoder & ~1, 2, executable=True)
    span_size = 2 * len(rows)
    spans = symbol(table, names[1], "STT_OBJECT", span_size)
    if spans & 1:
        raise ValueError("syscall trace spans are misaligned")
    span_offset = offset_for(elf, data, spans, span_size)
    pool_size = trace[2][0].st_size
    if not 1 < pool_size <= NAME_POOL_LIMIT + 1:
        raise ValueError("syscall trace pool size differs")
    pool = symbol(table, names[2], "STT_OBJECT", pool_size)
    pool_offset = offset_for(elf, data, pool, pool_size)
    if data[pool_offset + pool_size - 1] != 0:
        raise ValueError("syscall trace pool terminator differs")
    for index, row in enumerate(rows):
        span = struct.unpack_from("<H", data, span_offset + 2 * index)[0]
        start = span & (NAME_POOL_LIMIT - 1)
        length = span >> NAME_OFFSET_BITS
        if not length or start + length >= pool_size:
            raise ValueError(f"syscall trace slot {index} span is out of bounds")
        if data[pool_offset + start:pool_offset + start + length] != row.trace_name.encode():
            raise ValueError(f"syscall trace slot {index} name differs")


def check_data(data, rows, *, inet, glob, systrace=False):
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
    check_trace(elf, table, data, rows, systrace)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master", required=True, type=Path)
    parser.add_argument("--inet", action="store_true")
    parser.add_argument("--glob", action="store_true")
    parser.add_argument("--param", default="", help="configured compiler definitions")
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        rows = parse_syscalls(args.master)
        defines = {flag[2:].split("=", 1)[0] for flag in shlex.split(args.param)
                   if flag.startswith("-D")}
        for kernel in args.kernels:
            check_data(kernel.read_bytes(), rows, inet=args.inet or "INET" in defines,
                       glob=args.glob or "GLOB_ENABLED" in defines,
                       systrace="SYSTRACE" in defines)
            print(f"PASS RP2040 syscall dispatch: {kernel}")
    except (OSError, ValueError, MetadataError, ELFError, struct.error,
            IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 syscall dispatch: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
