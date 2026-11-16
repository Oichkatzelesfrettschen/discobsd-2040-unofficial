#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Fail when a linked RP2040 kernel lets its hot path share XIP cache sets.

Usage: check_hot_layout.py --self-test
       check_hot_layout.py [--linker-script LD] [--way-bytes N] [--line-bytes N]
                           ELF...

The RP2040 XIP cache holds 16 KB in two ways of 8-byte lines (datasheet
2.6.3.1-2.6.3.2), so flash addresses equal modulo one way (8 KB) compete for
one set, and three lines a loop touches in one set evict each other on every
pass. conf/kern.ldscript opens .text with the hot path, the input sections
named between `__hot_start = .;` and `__hot_end = .;`. For each kernel this
tool requires:

- both bounds, with __hot_start aligned to a cache line;
- a span no longer than one way, so that each of its lines takes its own set;
- every listed symbol the kernel links inside the span, and no compiler clone
  of a listed function (name.constprop.N, name.isra.N, name.part.N) outside
  it, because a clone lives in a section the list does not name;
- at most one line of the listed symbols in any set.

A name the configuration does not link is reported, not failed. Exit status: 0
every kernel passes, 1 a kernel fails a requirement, 2 an input cannot be read.
--self-test runs the checks on synthetic ELF images, one passing and one per
requirement that must fail; an image whose ELF type is not an executable or
whose machine is not ARM exits 2 even when its symbols would pass.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

DEFAULT_LINKER_SCRIPT = (
    Path(__file__).resolve().parent.parent / "sys/arch/rp2040/conf/kern.ldscript"
)
# One way of the XIP cache, and its line (datasheet 2.6.3.2: 16 KB, 8-byte
# lines, two ways per set).
WAY_BYTES = 8192
LINE_BYTES = 8

# The XIP window, cached alias (datasheet 2.6.3.1).
XIP_START = 0x10000000
XIP_END = 0x11000000

ELF_MAGIC = b"\x7fELF"
EHDR = struct.Struct("<16sHHIIIIIHHHHHH")
SHDR = struct.Struct("<10I")
SYM = struct.Struct("<IIIBBH")
SHT_SYMTAB = 2
ET_EXEC = 2
EM_ARM = 40
STT_OBJECT, STT_FUNC = 1, 2

HOT_ENTRY = re.compile(r"\*\(\.(?:text|rodata)\.([A-Za-z_][A-Za-z0-9_]*)\)")
CLONE = re.compile(r"^(.*)\.(?:constprop|isra|part|cold)\.\d+$")


class InputError(Exception):
    """An input is unreadable or does not describe the expected object."""


def hot_names(script: str) -> list[str]:
    """Return the symbol names the hot block of a linker script lists."""
    start = script.find("__hot_start = .;")
    end = script.find("__hot_end = .;")
    if start < 0 or end < 0 or end < start:
        raise InputError("linker script has no __hot_start ... __hot_end block")
    names = HOT_ENTRY.findall(script[start:end])
    if not names:
        raise InputError("the hot block of the linker script lists no section")
    return names


def read_symbols(data: bytes) -> dict[str, list[tuple[int, int, int]]]:
    """Map each symbol name of an ELF32 little-endian ARM image to (value, size, type)."""
    if len(data) < EHDR.size or data[:4] != ELF_MAGIC or data[4] != 1 or data[5] != 1:
        raise InputError("not a 32-bit little-endian ELF image")
    fields = EHDR.unpack_from(data)
    if fields[1] != ET_EXEC or fields[2] != EM_ARM:
        raise InputError(f"not a linked ARM executable (e_type {fields[1]}, e_machine "
                         f"{fields[2]})")
    shoff, shentsize, shnum = fields[6], fields[11], fields[12]
    if shoff == 0 or shentsize < SHDR.size or shoff + shnum * shentsize > len(data):
        raise InputError("section header table out of range")
    sections = [SHDR.unpack_from(data, shoff + i * shentsize) for i in range(shnum)]
    symbols: dict[str, list[tuple[int, int, int]]] = {}
    found = False
    for sh in sections:
        if sh[1] != SHT_SYMTAB:
            continue
        found = True
        offset, size, link, entsize = sh[4], sh[5], sh[6], sh[9]
        if link >= len(sections) or entsize < SYM.size or offset + size > len(data):
            raise InputError("symbol table out of range")
        stroff, strsize = sections[link][4], sections[link][5]
        strings = data[stroff:stroff + strsize]
        for i in range(size // entsize):
            name_off, value, sym_size, info, _, _ = SYM.unpack_from(data, offset + i * entsize)
            if name_off >= len(strings):
                raise InputError("symbol name out of range")
            name = strings[name_off:strings.index(b"\0", name_off)].decode("ascii", "replace")
            symbols.setdefault(name, []).append((value, sym_size, info & 15))
    if not found:
        raise InputError("no symbol table")
    return symbols


def check(symbols: dict[str, list[tuple[int, int, int]]], names: list[str],
          way: int, line: int) -> tuple[list[str], list[str]]:
    """Return (failures, notes) for one kernel's symbols against the hot list."""
    failures: list[str] = []
    notes: list[str] = []
    bounds = [symbols.get(n) for n in ("__hot_start", "__hot_end")]
    if not bounds[0] or not bounds[1]:
        return ["no __hot_start or __hot_end: the kernel was linked without the hot block"], notes
    start, end = bounds[0][0][0], bounds[1][0][0]
    span = end - start
    if start % line:
        failures.append(f"__hot_start {start:#x} is not aligned to a {line}-byte cache line")
    if span < 0 or span > way:
        failures.append(f"hot span {span} bytes exceeds one {way}-byte cache way")
    occupancy: dict[int, set[int]] = {}
    absent = []
    listed = set(names)
    for name in names:
        hits = [(v & ~1, s) for v, s, t in symbols.get(name, [])
                if t in (STT_FUNC, STT_OBJECT) and XIP_START <= v < XIP_END]
        if not hits:
            absent.append(name)
            continue
        for value, size in hits:
            if not (start <= value and value + max(size, 1) <= end):
                failures.append(f"{name} at {value:#x}+{size} lies outside the hot span "
                                f"{start:#x}-{end:#x}")
            for addr in range(value - value % line, value + max(size, 1), line):
                occupancy.setdefault((addr // line) % (way // line), set()).add(addr)
    for name, entries in symbols.items():
        clone = CLONE.match(name)
        if clone and clone.group(1) in listed:
            for value, _size, kind in entries:
                if kind == STT_FUNC and XIP_START <= value < XIP_END and \
                        not start <= (value & ~1) < end:
                    failures.append(f"{name}, a clone of listed {clone.group(1)}, lies outside "
                                    "the hot span; list its section")
    crowded = sorted(s for s, lines in occupancy.items() if len(lines) > 1)
    if crowded:
        first = ", ".join(hex(a) for a in sorted(occupancy[crowded[0]]))
        failures.append(f"{len(crowded)} cache sets hold more than one hot line, first set "
                        f"{crowded[0]}: {first}")
    lines = sum(len(v) for v in occupancy.values())
    notes.append(f"hot span {start:#x}-{end:#x} ({span} bytes), {lines} lines of "
                 f"{len(names) - len(absent)} listed symbols")
    if absent:
        notes.append("not linked: " + " ".join(absent))
    return failures, notes


def run(paths: list[str], script: Path, way: int, line: int) -> int:
    try:
        names = hot_names(script.read_text())
    except (OSError, InputError) as error:
        print(f"check_hot_layout: {script}: {error}", file=sys.stderr)
        return 2
    status = 0
    for path in paths:
        try:
            symbols = read_symbols(Path(path).read_bytes())
        except (OSError, InputError, struct.error, ValueError) as error:
            print(f"check_hot_layout: {path}: {error}", file=sys.stderr)
            return 2
        failures, notes = check(symbols, names, way, line)
        for failure in failures:
            print(f"FAIL hot-layout {path}: {failure}")
        if not failures:
            print(f"PASS hot-layout {path}: " + "; ".join(notes))
        status = max(status, 1 if failures else 0)
    return status


def synthetic_elf(symbols: list[tuple[str, int, int, int]], e_type: int = ET_EXEC,
                  machine: int = EM_ARM) -> bytes:
    """Build an ELF32 image holding only a symbol table of (name, value, size, type)."""
    strtab = b"\0"
    entries = [SYM.pack(0, 0, 0, 0, 0, 0)]
    for name, value, size, kind in symbols:
        entries.append(SYM.pack(len(strtab), value, size, kind, 0, 1))
        strtab += name.encode() + b"\0"
    symtab = b"".join(entries)
    shstrtab = b"\0.symtab\0.strtab\0.shstrtab\0"
    body_offset = EHDR.size
    symoff = body_offset
    stroff = symoff + len(symtab)
    shstroff = stroff + len(strtab)
    shoff = (shstroff + len(shstrtab) + 3) & ~3
    sections = [
        SHDR.pack(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        SHDR.pack(1, SHT_SYMTAB, 0, 0, symoff, len(symtab), 2, 0, 4, SYM.size),
        SHDR.pack(9, 3, 0, 0, stroff, len(strtab), 0, 0, 1, 0),
        SHDR.pack(17, 3, 0, 0, shstroff, len(shstrtab), 0, 0, 1, 0),
    ]
    ident = ELF_MAGIC + bytes([1, 1, 1]) + bytes(9)
    header = EHDR.pack(ident, e_type, machine, 1, 0, 0, shoff, 0x5000200, EHDR.size, 0, 0,
                       SHDR.size, len(sections), 3)
    image = header + symtab + strtab + shstrtab
    image += bytes(shoff - len(image))
    return image + b"".join(sections)


SELF_TEST_SCRIPT = """
.text : {
    __hot_start = .;
    *(.text.syscall)
    *(.text.rwip)
    *(.rodata.nsysent)
    __hot_end = .;
    *(.text*)
}
"""


def self_test() -> int:
    base = [("__hot_start", 0x10000200, 0, 0), ("__hot_end", 0x10001200, 0, 0),
            ("syscall", 0x10000201, 0x264, STT_FUNC), ("rwip", 0x10000465, 0x424, STT_FUNC),
            ("nsysent", 0x10000890, 4, STT_OBJECT), ("cold", 0x10004000, 0x400, STT_FUNC)]

    def replace(name: str, value: int, size: int | None = None) -> list:
        return [(n, value if n == name else v, (size if size is not None else s) if n == name
                 else s, k) for n, v, s, k in base]

    cases = [
        ("a span holding every listed symbol", base, 0),
        ("a listed function outside the span", replace("rwip", 0x10003465), 1),
        ("a listed table outside the span", replace("nsysent", 0x10002890), 1),
        ("a span longer than one way", replace("__hot_end", 0x10002208), 1),
        ("a misaligned span start", replace("__hot_start", 0x10000204), 1),
        ("no span bounds", [s for s in base if s[0] != "__hot_end"], 1),
        ("a clone of a listed function outside the span",
         base + [("rwip.constprop.0", 0x10005001, 0x40, STT_FUNC)], 1),
        ("a listed line one way above another listed line",
         replace("__hot_end", 0x10002200) + [("rwip", 0x100023f8, 0x10, STT_FUNC)], 1),
    ]
    names = hot_names(SELF_TEST_SCRIPT)
    if names != ["syscall", "rwip", "nsysent"]:
        print(f"self-test: hot block parsed as {names}", file=sys.stderr)
        return 1
    status = 0
    with tempfile.TemporaryDirectory() as scratch:
        script = Path(scratch) / "kern.ldscript"
        script.write_text(SELF_TEST_SCRIPT)
        for what, symbols, want in cases:
            elf = Path(scratch) / "unix.elf"
            elf.write_bytes(synthetic_elf(symbols))
            got = run([str(elf)], script, WAY_BYTES, LINE_BYTES)
            verdict = "ok" if got == want else "WRONG"
            print(f"self-test: {what}: exit {got}, expected {want}: {verdict}")
            if got != want:
                status = 1
        bad_script = Path(scratch) / "empty.ldscript"
        bad_script.write_text(".text : { *(.text*) }\n")
        got = run([str(elf)], bad_script, WAY_BYTES, LINE_BYTES)
        print(f"self-test: a linker script without the hot block: exit {got}, expected 2: "
              + ("ok" if got == 2 else "WRONG"))
        status = status or (got != 2)
        for what, kwargs in (("an x86-64 machine field", {"machine": 62}),
                             ("a relocatable object", {"e_type": 1})):
            other = Path(scratch) / "other.elf"
            other.write_bytes(synthetic_elf(base, **kwargs))
            got = run([str(other)], script, WAY_BYTES, LINE_BYTES)
            print(f"self-test: {what} with passing symbols: exit {got}, expected 2: "
                  + ("ok" if got == 2 else "WRONG"))
            status = status or (got != 2)
        junk = Path(scratch) / "junk.elf"
        junk.write_bytes(b"not an image")
        got = run([str(junk)], script, WAY_BYTES, LINE_BYTES)
        print(f"self-test: an input that is not ELF: exit {got}, expected 2: "
              + ("ok" if got == 2 else "WRONG"))
        status = status or (got != 2)
    print("check_hot_layout self-test: " + ("pass" if status == 0 else "FAIL"))
    return int(status)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--linker-script", type=Path, default=DEFAULT_LINKER_SCRIPT)
    parser.add_argument("--way-bytes", type=int, default=WAY_BYTES)
    parser.add_argument("--line-bytes", type=int, default=LINE_BYTES)
    parser.add_argument("elf", nargs="*")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.elf:
        parser.error("name at least one kernel ELF")
    if args.line_bytes <= 0 or args.way_bytes % args.line_bytes:
        parser.error("--way-bytes must be a multiple of a positive --line-bytes")
    return run(args.elf, args.linker_script, args.way_bytes, args.line_bytes)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
