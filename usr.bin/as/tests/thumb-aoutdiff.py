#!/usr/bin/env python3
"""Compare the text of an a.out object against the same source assembled by
arm-none-eabi-as.

The two assemblers must agree on every instruction they encode. They cannot
agree inside a relocated field: an a.out record stores a plain displacement
or a segment-relative address, while the ELF object stores what
R_ARM_THM_CALL and R_ARM_ABS32 prescribe, and neither is wrong. A field
covered by a relocation on either side is therefore excluded, and any other
difference fails the comparison.

GCC splits code across .text, .text.startup and similar sections while the
a.out object has one text segment, so the ELF side is concatenated in the
order the source introduces the sections, word aligned between them.

usage: thumb-aoutdiff.py SOURCE.s OBJECT.aout GNUOBJECT.o
"""

import re
import struct
import subprocess
import sys


def text_sections(src):
    """Names of the executable sections, in the order the source opens them.

    A semicolon separates statements, so a macro that expands to a whole
    function on one line -- which is how libc's syscall stubs are written --
    still has its .text found.
    """
    order = []
    cur = None
    for line in open(src, errors="replace"):
        if line.lstrip().startswith("#"):
            continue                            # a cpp line marker
        for stmt in line.split(";"):
            stmt = stmt.strip()
            m = re.match(r'\.section\s+"?(\.[\w.$]+)', stmt)
            if m:
                cur = m.group(1)
            elif re.match(r"\.text\b", stmt):
                cur = ".text"
            elif re.match(r"\.(data|bss|rodata)\b", stmt):
                cur = None
            if cur and cur.startswith(".text") and cur not in order:
                order.append(cur)
    return order


def gnu_text(gnuobj, order, tmp):
    """The ELF text, concatenated in source order, plus each section's base."""
    blob = b""
    base = {}
    for sec in order:
        r = subprocess.run(
            ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=" + sec,
             gnuobj, tmp], capture_output=True)
        if r.returncode != 0:
            continue
        try:
            part = open(tmp, "rb").read()
        except OSError:
            continue
        while len(blob) % 4:
            blob += b"\0"
        base[sec] = len(blob)
        blob += part
    return blob, base


def covered_by_relocs(gnuobj, base, aout, textlen):
    """Byte offsets that hold a relocated field on either side."""
    hit = set()

    out = subprocess.run(["arm-none-eabi-readelf", "-rW", gnuobj],
                         capture_output=True, text=True).stdout
    sec = None
    for line in out.splitlines():
        m = re.match(r"Relocation section '\.rel(\.[\w.$]+)'", line)
        if m:
            sec = m.group(1)
        m = re.match(r"^([0-9a-f]{8})\s+\S+\s+(R_ARM_\w+)", line)
        if m and sec in base:
            off = base[sec] + int(m.group(1), 16)
            width = 4 if m.group(2) in ("R_ARM_THM_CALL", "R_ARM_ABS32",
                                        "R_ARM_THM_JUMP24") else 2
            for i in range(off, off + width, 2):
                hit.add(i)

    d = open(aout, "rb").read()
    _, text, data, _, rt, _, _, _ = struct.unpack("<8I", d[:32])
    buf = d[32 + text + data:32 + text + data + rt]
    i = 0
    while i + 5 <= len(buf):
        flags = buf[i]
        addr = struct.unpack("<I", buf[i + 1:i + 5])[0]
        i += 5
        if (flags & 0x70) == 0x70:          # REXT carries a symbol index
            i += 3
        width = 4 if (flags & 0x0f) in (1, 8) else 2
        for k in range(addr, addr + width, 2):
            hit.add(k)
    return hit


def main():
    src, aout, gnuobj = sys.argv[1:4]
    order = text_sections(src)
    gnu, base = gnu_text(gnuobj, order, aout + ".sec")

    d = open(aout, "rb").read()
    text = struct.unpack("<8I", d[:32])[1]
    mine = d[32:32 + text]

    hit = covered_by_relocs(gnuobj, base, aout, text)
    n = min(len(gnu), len(mine))
    differ = [i for i in range(0, n, 2) if gnu[i:i + 2] != mine[i:i + 2]]
    bad = [i for i in differ if i not in hit]

    # The a.out text is padded out to a word; GNU as leaves the section short.
    tail = mine[len(gnu):]
    if tail and tail not in (b"\0" * len(tail), b"\xc0\x46" * (len(tail) // 2)):
        bad.append(len(gnu))

    print("%-28s %4d halfwords, %3d relocated, %d unexplained"
          % (src.split("/")[-1], n // 2, len(differ), len(bad)))
    for i in bad[:8]:
        print("    @%d expected %s got %s"
              % (i, gnu[i:i + 2].hex(), mine[i:i + 2].hex()))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
