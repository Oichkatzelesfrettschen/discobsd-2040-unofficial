#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""List the tracked source files that hold a literal address inside each block.

The Pico SDK address map (hardware/regs/addressmap.h) names the base of every
RP2040 block. A driver that reaches a register through a macro of its own
(0x4001800cUL, not IO_QSPI_BASE + 0x0c) escapes a search for the block name or
its base, so this tool classifies every eight-digit hexadecimal literal of the
tracked port sources by the address range of the block that contains it and
prints, per block, the files and the literals they hold. A hit is a lead, not
a use: a size constant such as 0x40000000 falls inside the SYSINFO range too.

usage: rp2040_block_scan.py [--sdk DIR] [--root DIR]
       rp2040_block_scan.py --selftest
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

BASE_RE = re.compile(r"^#define\s+(\w+)_BASE\s+_u\((0x[0-9a-fA-F]+)\)", re.M)
LITERAL_RE = re.compile(r"0[xX][0-9a-fA-F]{8,16}")
SOURCE_SUFFIXES = {".c", ".h", ".S", ".def", ".ldscript", ".sh", ".py"}
SOURCE_ROOTS = ("sys/arch/rp2040", "usr.bin", "lib", "sbin", "bin")
SKIP_PREFIXES = ("sys/arch/rp2040/doc/", "sys/arch/rp2040/compile/")
ADDRESS_MAP = "src/rp2040/hardware_regs/include/hardware/regs/addressmap.h"
PERIPHERAL_FIRST = 0x14000000


def extent(base):
    """Bytes a block spans from its base, 0 for memories (no literal scan)."""
    if 0x40000000 <= base < 0x50000000:
        return 0x4000
    if base in (0x14000000, 0x18000000):
        return 0x4000
    if 0x50000000 <= base < 0x60000000 or base == 0xD0000000:
        return 0x10000
    if base == 0xE0000000:
        return 0x100000
    return 0


def blocks_from_map(text):
    """Return [(name, low, high)] for every block with a register range."""
    found = []
    for name, base in BASE_RE.findall(text):
        low = int(base, 16)
        span = extent(low)
        if span:
            found.append((name, low, low + span))
    return sorted(found, key=lambda b: b[1])


def block_of(blocks, address):
    for name, low, high in blocks:
        if low <= address < high:
            return name
    return None


def literals(text):
    """The eight-digit hexadecimal literals of text, suffixes and longer ones aside."""
    return [x.lower() for x in LITERAL_RE.findall(text) if len(x) == 10]


def scan(root, blocks):
    names = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--", *SOURCE_ROOTS],
        check=True,
        capture_output=True,
    ).stdout.decode()
    hits = {}
    for rel in filter(None, names.split("\0")):
        if rel.startswith(SKIP_PREFIXES) or Path(rel).suffix not in SOURCE_SUFFIXES:
            continue
        text = (root / rel).read_text(errors="replace")
        for literal in literals(text):
            owner = block_of(blocks, int(literal, 16))
            if owner:
                hits.setdefault(owner, {}).setdefault(rel, set()).add(literal)
    return hits


def selftest():
    blocks = [
        ("IO_BANK0", 0x40014000, 0x40018000),
        ("IO_QSPI", 0x40018000, 0x4001C000),
        ("SIO", 0xD0000000, 0xD0010000),
    ]
    known = {
        0x4001800C: "IO_QSPI",
        0x40017FFC: "IO_BANK0",
        0x4001C000: None,
        0xD0000060: "SIO",
        0x20000000: None,
    }
    for address, want in known.items():
        got = block_of(blocks, address)
        if got != want:
            raise SystemExit(f"selftest: {address:#x} is {got}, expected {want}")
    sample = "#define A 0x40018000UL\n#define B 0x4001800cUL\nx = 0x40018000123;"
    if literals(sample) != ["0x40018000", "0x4001800c"]:
        raise SystemExit("selftest: the literal pattern misreads a suffix or a long literal")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "sys/arch/rp2040").mkdir(parents=True)
        source = root / "sys/arch/rp2040/a.c"
        source.write_text("#define A 0x40018000UL\n")
        subprocess.run(["git", "-C", tmp, "init", "-q"], check=True)
        subprocess.run(["git", "-C", tmp, "add", "."], check=True)
        if scan(root, blocks).get("IO_QSPI", {}).get("sys/arch/rp2040/a.c") != {"0x40018000"}:
            raise SystemExit("selftest: the scan misses a literal of a tracked file")
        source.unlink()
        try:
            scan(root, blocks)
        except OSError:
            pass
        else:
            raise SystemExit("selftest: the scan ignores a tracked file it cannot read")
    print("rp2040_block_scan: selftest ok")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    root = args.root.resolve()
    sdk = args.sdk
    if sdk is None:
        sdk = Path(
            subprocess.run(
                ["sh", str(root / "tools/pico-sdk/sdk-path.sh")],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
        )
    blocks = blocks_from_map((sdk / ADDRESS_MAP).read_text())
    if not blocks:
        print("rp2040_block_scan: no block with a register range in the map", file=sys.stderr)
        return 1
    try:
        hits = scan(root, blocks)
    except OSError as err:
        print(f"rp2040_block_scan: a tracked source is unreadable: {err}", file=sys.stderr)
        return 1
    for name, _, _ in blocks:
        for rel in sorted(hits.get(name, {})):
            print(f"{name}\t{rel}\t{','.join(sorted(hits[name][rel]))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
