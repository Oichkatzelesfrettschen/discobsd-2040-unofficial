"""Calibrate linked RP2040 syscall dispatch pointers and argument counts."""

import argparse
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import check_rp2040_syscalls as checker  # noqa: E402


def symbol_offset(elf, name):
    entries = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    if entries is None or len(entries) != 1:
        raise AssertionError(f"missing or duplicate {name}")
    symbol = entries[0].entry
    section = elf.get_section(symbol.st_shndx)
    return section.header.sh_offset + symbol.st_value - section.header.sh_addr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--trace-kernel", required=True, type=Path)
    args = parser.parse_args()
    for condition, inet, glob, expected in (
        ("ALWAYS", False, False, ("handler", 3)),
        ("IFNET", False, False, ("nosys", 0)),
        ("ERRNET", False, False, ("nonet", 0)),
        ("IFNET", True, False, ("handler", 3)),
        ("ERRNET", True, False, ("handler", 3)),
        ("GLOB", False, False, ("nosys", 3)),
        ("GLOB", False, True, ("handler", 3)),
    ):
        row = SimpleNamespace(condition=condition, handler="handler", nargs=3)
        if checker.selected(row, inet, glob) != expected:
            raise AssertionError(f"wrong selection for {condition}")
    master = ROOT / "sys/kern/syscalls.master"
    rows = checker.parse_syscalls(master)
    data = args.kernel.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    handlers = symbol_offset(elf, "syscall_handlers")
    nargs = symbol_offset(elf, "syscall_nargs")
    count = symbol_offset(elf, "nsysent")
    pointers = struct.unpack_from(f"<{len(rows)}I", data, handlers)
    alternatives = set(pointers)
    if len(alternatives) < 2:
        raise AssertionError("fixture needs two distinct syscall targets")
    checker.check_data(data, rows, inet=False, glob=False)
    for inet, glob in ((True, False), (False, True)):
        try:
            checker.check_data(data, rows, inet=inet, glob=glob)
        except ValueError:
            pass
        else:
            raise AssertionError("incompatible configuration was accepted")
    for index, pointer in enumerate(pointers):
        modified = bytearray(data)
        struct.pack_into(
            "<I",
            modified,
            handlers + 4 * index,
            next(value for value in alternatives if value != pointer),
        )
        reject(modified, rows, f"syscall {index} pointer differs")
        modified = bytearray(data)
        modified[nargs + index] = (modified[nargs + index] + 1) % 7
        reject(modified, rows, f"syscall {index} argument count differs")
        modified = bytearray(data)
        struct.pack_into("<I", modified, handlers + 4 * index, pointer & ~1)
        reject(modified, rows, f"syscall {index} pointer differs")
    modified = bytearray(data)
    struct.pack_into("<I", modified, count, len(rows) - 1)
    reject(modified, rows, "initialized syscall count differs")
    with tempfile.TemporaryDirectory(prefix="rp2040-syscalls-") as directory:
        mutant = Path(directory) / "truncated.elf"
        mutant.write_bytes(data[:80])
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/check_rp2040_syscalls.py"),
                "--master",
                str(master),
                str(mutant),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 syscalls: {len(rows)} pointers, counts and Thumb-bit mutants")
    check_trace_mutants(rows, data, args.trace_kernel.read_bytes())


def check_trace_mutants(rows, plain, traced):
    checker.check_data(traced, rows, inet=False, glob=False, systrace=True)
    reject(plain, rows, "expected one definition of each syscall trace symbol", systrace=True)
    reject(traced, rows, "disabled syscall trace metadata is linked")
    elf = ELFFile(io.BytesIO(traced))
    spans = symbol_offset(elf, "syscall_name_spans")
    pool = symbol_offset(elf, "syscall_name_pool")
    for invalid_span in (0, 0xFFFF):
        modified = bytearray(traced)
        struct.pack_into("<H", modified, spans, invalid_span)
        reject(modified, rows, "syscall trace slot 0 span is out of bounds", systrace=True)
    start = struct.unpack_from("<H", traced, spans)[0] & (checker.NAME_POOL_LIMIT - 1)
    modified = bytearray(traced)
    modified[pool + start] ^= 0x80
    reject(modified, rows, "syscall trace slot 0 name differs", systrace=True)
    print("PASS RP2040 syscall trace calibration: five corruptions rejected")


def reject(data, rows, label, *, systrace=False):
    try:
        checker.check_data(data, rows, inet=False, glob=False, systrace=systrace)
    except ValueError as error:
        if label in str(error):
            return
        raise AssertionError(f"{label}: unexpected diagnostic: {error}") from error
    raise AssertionError(f"{label} mutant was accepted")


if __name__ == "__main__":
    main()
