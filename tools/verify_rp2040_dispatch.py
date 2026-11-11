"""Validate linked syscall metadata against the master and selected options."""

import argparse
import copy
import shlex
import struct
from dataclasses import dataclass
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Symbol:
    address: int
    size: int
    data: bytes = b""
    function: bool = False


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_symbols(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        require(elf.elfclass == 32 and elf.little_endian and
                elf["e_machine"] == "EM_ARM", "expected little-endian ARM ELF32")
        result = {}
        for entry in elf.get_section_by_name(".symtab").iter_symbols():
            if entry["st_shndx"] == "SHN_UNDEF":
                continue
            function = entry["st_info"]["type"] == "STT_FUNC"
            address, size = entry["st_value"], entry["st_size"]
            data = b""
            if isinstance(entry["st_shndx"], int) and size and not function:
                section = elf.get_section(entry["st_shndx"])
                offset = address - section["sh_addr"]
                if section["sh_type"] != "SHT_NOBITS":
                    data = section.data()[offset:offset + size]
            result[entry.name] = Symbol(address, size, data, function)
        if "syscall_handler_offsets" in result and "syscall_handler_offsets_end" in result:
            start = result["syscall_handler_offsets"]
            size = result["syscall_handler_offsets_end"].address - start.address
            require(0 <= size <= 65536, "compact table extent differs")
            entry = elf.get_section_by_name(".symtab").get_symbol_by_name(
                "syscall_handler_offsets")[0]
            section = elf.get_section(entry["st_shndx"])
            offset = start.address - section["sh_addr"]
            require(0 <= offset and offset + size <= section["sh_size"],
                    "compact table outside its section")
            start.size = size
            start.data = section.data()[offset:offset + size]
        return result


def master_rows():
    return [line.split("|") for line in (ROOT / "sys/kern/syscalls.master").read_text().splitlines()
            if line and not line.startswith("#")]


def validate(symbols, rows, defines):
    count = len(rows)
    for required in ("syscall_nargs", "nsysent"):
        require(required in symbols, f"missing {required}")
    require(symbols["nsysent"].data == struct.pack("<I", count), "syscall count differs")
    args = symbols["syscall_nargs"].data
    require(len(args) == count, "argument table extent differs")
    if "COMPACT_SYSCALL_DISPATCH" in defines:
        require("syscall_handlers" not in symbols, "full handler table retained")
        require(all(name in symbols for name in ("syscall_handler_offsets",
                "syscall_handler_offsets_end", "__syscall_text_anchor")),
                "missing compact table boundary")
        table = symbols["syscall_handler_offsets"]
        anchor = symbols["__syscall_text_anchor"].address
        require(anchor == 0x10000100, "compact anchor differs from RP2040 flash origin")
        require(table.address % 2 == 0, "compact table alignment differs")
        require(len(table.data) == count * 2 and
                symbols["syscall_handler_offsets_end"].address - table.address == count * 2,
                "compact table extent differs")
        pointers = [anchor + (offset << 1) + 1
                    for offset in struct.unpack("<" + "H" * count, table.data)]
    else:
        require("syscall_handler_offsets" not in symbols, "compact table retained")
        require("syscall_handlers" in symbols, "missing full handler table")
        table = symbols["syscall_handlers"]
        require(table.address % 4 == 0, "handler table alignment differs")
        require(len(table.data) == count * 4, "handler table extent differs")
        pointers = struct.unpack("<" + "I" * count, table.data)
    for index, row in enumerate(rows):
        number, nargs, handler, condition, _trace, _public = row
        require(int(number) == index, "master ordinal differs")
        require(condition in {"ALWAYS", "IFNET", "ERRNET", "GLOB"}, "master condition differs")
        if condition in ("IFNET", "ERRNET") and "INET" not in defines:
            handler = "nonet" if condition == "ERRNET" else "nosys"
            nargs = "0"
        elif condition == "GLOB" and "GLOB_ENABLED" not in defines:
            handler = "nosys"
        require(handler in symbols, f"missing handler {handler}")
        target = symbols[handler]
        require(target.function and target.address & 1, f"non-Thumb handler {handler}")
        require(pointers[index] == target.address, f"handler slot {index} differs")
        require(args[index] == int(nargs), f"argument slot {index} differs")
    names = ("syscall_name", "syscall_name_pool", "syscall_name_spans")
    if "SYSTRACE" not in defines:
        require(all(name not in symbols for name in names), "disabled trace metadata retained")
    else:
        require(all(name in symbols for name in names), "enabled trace metadata missing")
        require(symbols["syscall_name"].function, "trace decoder is not a function")
        pool = symbols["syscall_name_pool"].data
        spans = symbols["syscall_name_spans"]
        require(spans.address % 2 == 0 and len(spans.data) == count * 2,
                "trace span extent/alignment differs")
        for index, row in enumerate(rows):
            span = int.from_bytes(spans.data[index * 2:index * 2 + 2], "little")
            offset, length = span & 1023, span >> 10
            require(offset + length <= len(pool) and
                    pool[offset:offset + length] == row[4].encode(),
                    f"trace slot {index} differs")
    return count


def self_test():
    rows = [["0", "0", "nosys", "ALWAYS", "indir", "-"],
            ["1", "1", "rexit", "ALWAYS", "exit", "exit=1"],
            ["2", "2", "accept", "IFNET", "accept", "accept=2"]]
    good = {"nsysent": Symbol(0x10001000, 4, struct.pack("<I", 3)),
            "syscall_nargs": Symbol(0x10001004, 3, bytes((0, 1, 0))),
            "syscall_handlers": Symbol(0x10001008, 12,
                                       struct.pack("<III", 0x10000101, 0x10000111, 0x10000101)),
            "nosys": Symbol(0x10000101, 2, function=True),
            "rexit": Symbol(0x10000111, 2, function=True)}
    validate(good, rows, set())
    compact = copy.deepcopy(good)
    del compact["syscall_handlers"]
    compact.update({"__syscall_text_anchor": Symbol(0x10000100, 0),
                    "syscall_handler_offsets": Symbol(0x10001008, 6,
                                                       struct.pack("<HHH", 0, 8, 0)),
                    "syscall_handler_offsets_end": Symbol(0x1000100e, 0)})
    validate(compact, rows, {"COMPACT_SYSCALL_DISPATCH"})
    for name, field, value in (("__syscall_text_anchor", "address", 0x10000101),
                               ("syscall_handler_offsets", "data", bytes(6)),
                               ("syscall_handler_offsets_end", "address", 0x1000100c)):
        bad = copy.deepcopy(compact)
        setattr(bad[name], field, value)
        try:
            validate(bad, rows, {"COMPACT_SYSCALL_DISPATCH"})
        except ValueError:
            pass
        else:
            raise ValueError("accepted malformed compact table")
    mutations = (
        ("syscall_nargs", "data", bytes((0, 0, 0)), "argument slot"),
        ("syscall_handlers", "address", 0x10001009, "alignment"),
        ("syscall_handlers", "data", bytes(12), "handler slot"),
        ("syscall_handlers", "data", bytes(8), "extent"),
        ("rexit", "address", 0x10000110, "non-Thumb"),
        ("nsysent", "data", struct.pack("<I", 2), "count"),
    )
    for name, field, value, diagnostic in mutations:
        bad = copy.deepcopy(good)
        setattr(bad[name], field, value)
        try:
            validate(bad, rows, set())
        except ValueError as error:
            require(diagnostic in str(error), "unexpected calibration diagnostic")
        else:
            raise ValueError("accepted corrupted " + name)
    traced = copy.deepcopy(good)
    traced.update({"syscall_name": Symbol(0x10000201, 2, function=True),
                   "syscall_name_pool": Symbol(0x10000300, 15, b"indirexitaccept"),
                   "syscall_name_spans": Symbol(0x10000400, 6,
                                                struct.pack("<HHH", 5 << 10,
                                                            5 | 4 << 10, 9 | 6 << 10))})
    validate(traced, rows, {"SYSTRACE"})
    for symbols, defines, diagnostic in ((traced, set(), "disabled trace"),
                                         (good, {"SYSTRACE"}, "enabled trace")):
        try:
            validate(symbols, rows, defines)
        except ValueError as error:
            require(diagnostic in str(error), "unexpected trace calibration diagnostic")
        else:
            raise ValueError("accepted inconsistent trace selection")
    print("PASS dispatch image calibration: three controls, eleven corruptions rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--param", default="")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.elf:
        defines = {flag[2:].split("=", 1)[0] for flag in shlex.split(args.param)
                   if flag.startswith("-D")}
        count = validate(read_symbols(args.elf), master_rows(), defines)
        print(f"PASS dispatch image {args.elf}: {count} handlers, arguments and trace selection")
    elif not args.self_test:
        parser.error("--elf or --self-test is required")


if __name__ == "__main__":
    main()
