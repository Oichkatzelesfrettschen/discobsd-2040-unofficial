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

"""Replay a Renode execution trace through a model of the RP2040 XIP cache.

Usage: xipsim.py replay TRACE BASE.map MARKER LAYOUT.map... [--sets N] [--allow-omitted]
       xipsim.py sections TRACE BASE.map
       xipsim.py pad TRACE BASE.map MARKER OBJECT:TEXT_BYTES:RODATA_BYTES...

TRACE is a gzip file of a Renode `CreateExecutionTracing ... PCAndOpcode` log
with `TrackMemoryAccesses`, recorded on the kernel whose link map is BASE.map
(trace_run.py beside this file records one). The cache has 1,024 sets of two
8-byte lines (RP2040 datasheet 2.6.3.1-2.6.3.2); the set of an address is its
bits 12 to 3. The datasheet names no replacement policy, so each replay runs
LRU, FIFO and seeded random replacement.

replay relocates every traced address through the input section that holds it
(name, object and ordinal) into each LAYOUT.map and reports XIP word accesses
and misses per entry of the section .text.MARKER. A traced address whose
section the layout resizes or lacks cannot be relocated: replay names each
such section with its omitted event count and exits 1 unless --allow-omitted
accepts the truncated stream; --sets lists the sets that miss most under LRU
with the input sections their lines belong to. sections
lists the input sections the trace executes or reads, with access counts and
sizes. pad models an all -Os kernel with TEXT_BYTES inserted after the last
text section of OBJECT, moving every later section, and RODATA_BYTES more
moving every later read-only section.

The fetch model issues one access per aligned 32-bit word the instruction
stream enters, as the Cortex-M0+ fetches words, and one per data read in the
XIP window. It omits interrupts the emulator does not model (USB) and the
board's timing, so it ranks layouts rather than predicting cycle counts.
"""

from __future__ import annotations

import gzip
import random
import re
import sys
from bisect import bisect_right

XIP_LO, XIP_HI = 0x10000000, 0x14000000
LINE, SETS = 8, 1024

SECTION_ALONE = re.compile(r"^ (\.(?:text|rodata|isr_vector)[^\s]*)\s*$")
SECTION_LINE = re.compile(
    r"^ (\.(?:text|rodata|isr_vector)[^\s]*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
CONTINUATION = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
INSTRUCTION = re.compile(r"^0x([0-9A-F]+): 0x([0-9A-F]+)")
READ = re.compile(r"^MemoryRead with address 0x([0-9A-F]+)")


def parse_map(path):
    """Return [(address, size, (section, object, ordinal))] of the flash input sections."""
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    raw = []
    i = 0
    while i < len(lines):
        alone = SECTION_ALONE.match(lines[i])
        if alone and i + 1 < len(lines):
            cont = CONTINUATION.match(lines[i + 1])
            if cont:
                raw.append((int(cont.group(1), 16), int(cont.group(2), 16), alone.group(1),
                            cont.group(3)))
                i += 2
                continue
        full = SECTION_LINE.match(lines[i])
        if full:
            raw.append((int(full.group(2), 16), int(full.group(3), 16), full.group(1),
                        full.group(4)))
        i += 1
    seen = {}
    keyed = []
    for address, size, name, obj in sorted(r for r in raw if r[1] and XIP_LO <= r[0] < XIP_HI):
        key = (name, obj.rsplit("/", 1)[-1])
        seen[key] = seen.get(key, 0) + 1
        keyed.append((address, size, key + (seen[key],)))
    return keyed


def section_start(layout, name):
    for address, _size, key in layout:
        if key[0] == name:
            return address
    raise SystemExit(f"xipsim: no input section {name} in the base map")


def relocator(base, target):
    """Return reloc(address) -> (new address, None) or (None, key of the section
    that has no same-size counterpart in target)."""
    starts = [s[0] for s in base]
    where = {key: (address, size) for address, size, key in target}

    def reloc(address):
        i = bisect_right(starts, address) - 1
        if i < 0 or address >= base[i][0] + base[i][1]:
            return address, None
        start, size, key = base[i]
        found = where.get(key)
        if found is None or found[1] != size:
            return None, key
        return found[0] + address - start, None
    return reloc


def read_trace(path):
    events = []
    with gzip.open(path, "rt", errors="replace") as handle:
        for line in handle:
            m = INSTRUCTION.match(line)
            if m:
                events.append((0, int(m.group(1), 16), 4 if len(m.group(2)) > 6 else 2))
                continue
            m = READ.match(line)
            if m:
                address = int(m.group(1), 16)
                if XIP_LO <= address < XIP_HI:
                    events.append((1, address, 4))
    return events


def accesses(events, reloc):
    """Return the relocated XIP access stream and, per input section that the
    target resizes or lacks, the number of traced events omitted from it."""
    out = []
    omitted = {}
    last = None
    for kind, address, size in events:
        if not XIP_LO <= address < XIP_HI:
            if kind == 0:
                last = None
            continue
        moved, lost = reloc(address)
        if moved is None:
            omitted[lost] = omitted.get(lost, 0) + 1
            last = None
            continue
        if kind == 0:
            for word in range(moved >> 2, ((moved + size - 1) >> 2) + 1):
                if word != last:
                    out.append(word << 2)
                    last = word
        else:
            out.append(moved)
    return out, omitted


def simulate(stream, policy, seed=1):
    tags = [[-1, -1] for _ in range(SETS)]
    victim = [0] * SETS
    rng = random.Random(seed)
    misses = 0
    per_set = [0] * SETS
    for address in stream:
        line = (address & 0xFFFFFF) >> 3
        index = line & (SETS - 1)
        tag = line >> 10
        ways = tags[index]
        if tag in ways:
            if policy == "lru":
                victim[index] = 1 - ways.index(tag)
            continue
        misses += 1
        per_set[index] += 1
        if -1 in ways:
            way = ways.index(-1)
        elif policy == "random":
            way = rng.randrange(2)
        else:
            way = victim[index]
        ways[way] = tag
        victim[index] = 1 - way
    return misses, per_set


def name_of(layout, starts, address):
    i = bisect_right(starts, address) - 1
    if i < 0 or address >= layout[i][0] + layout[i][1]:
        return hex(address)
    return f"{layout[i][2][0]}({layout[i][2][1]})+{address - layout[i][0]:#x}"


def report(label, stream, marks, target, show_sets, omitted, total):
    """Print the replay of one layout; return the number of omitted events."""
    lost = sum(omitted.values())
    if lost:
        names = ", ".join(f"{k[0]}({k[1]}) {n}" for k, n in
                          sorted(omitted.items(), key=lambda kv: -kv[1]))
        print(f"{label}: OMITTED {lost} of {total} traced XIP events "
              f"({100.0 * lost / total:.1f}%) from {len(omitted)} sections the layout "
              f"resizes or lacks: {names}")
    results = []
    for policy in ("lru", "fifo", "random"):
        misses, _ = simulate(stream, policy)
        results.append(f"{policy} {misses / marks:.2f}")
    print(f"{label}: accesses/iter {len(stream) / marks:.1f}, misses/iter " + ", ".join(results))
    if not show_sets:
        return lost
    _, per_set = simulate(stream, "lru")
    members = {}
    for address in stream:
        line = (address & 0xFFFFFF) >> 3
        members.setdefault(line & (SETS - 1), set()).add(line)
    starts = [s[0] for s in target]
    for index in sorted(range(SETS), key=lambda s: -per_set[s])[:show_sets]:
        if per_set[index] == 0:
            break
        lines = ", ".join(name_of(target, starts, 0x10000000 | (line << 3))
                          for line in sorted(members[index]))
        print(f"   set {index}: {per_set[index] / marks:.2f}/iter, lines {lines}")
    return lost


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    mode, trace, basemap = argv[0], argv[1], argv[2]
    base = parse_map(basemap)
    events = read_trace(trace)
    if mode == "sections":
        starts = [s[0] for s in base]
        counts = {}
        for _kind, address, _size in events:
            i = bisect_right(starts, address) - 1
            if XIP_LO <= address < XIP_HI and i >= 0 and address < base[i][0] + base[i][1]:
                counts[i] = counts.get(i, 0) + 1
        total = 0
        for i, count in sorted(counts.items(), key=lambda kv: -kv[1]):
            total += base[i][1]
            print(f"{count:9d} {base[i][1]:6d} {base[i][2][0]} {base[i][2][1]}")
        print(f"sections {len(counts)} bytes {total}")
        return 0
    total = sum(1 for _k, address, _s in events if XIP_LO <= address < XIP_HI)
    marker = section_start(base, ".text." + argv[3])
    marks = sum(1 for kind, address, _ in events if kind == 0 and address == marker)
    print(f"trace events {len(events)}, {argv[3]} entries {marks}")
    if mode == "replay":
        show = 0
        args = argv[4:]
        allow = "--allow-omitted" in args
        args = [a for a in args if a != "--allow-omitted"]
        lost = 0
        if "--sets" in args:
            at = args.index("--sets")
            show = int(args[at + 1])
            args = args[:at] + args[at + 2:]
        for path in args:
            target = parse_map(path)
            stream, omitted = accesses(events, relocator(base, target))
            lost += report(path, stream, marks, target, show, omitted, total)
        if lost and not allow:
            print("xipsim: a layout omitted traced events; its figures describe a truncated "
                  "stream (pass --allow-omitted to accept that)", file=sys.stderr)
            return 1
        return 0
    if mode == "pad":
        for spec in argv[4:]:
            obj, text_bytes, rodata_bytes = spec.split(":")
            end = max(a + s for a, s, k in base if k[1] == obj and k[0].startswith(".text"))
            target = [(a + (int(text_bytes) + (int(rodata_bytes) if k[0].startswith(".rodata")
                                                else 0) if a >= end else 0), s, k)
                      for a, s, k in base]
            stream, omitted = accesses(events, relocator(base, target))
            if report(f"pad {spec}", stream, marks, target, 0, omitted, total):
                return 1
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
