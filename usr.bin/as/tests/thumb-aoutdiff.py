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

The data segment is compared the same way. An a.out object holds
a_data = count[SDATA] + count[SSTRNG], so the ELF side is every .data
section in source order followed by every .rodata section in source order.

usage: thumb-aoutdiff.py SOURCE.s OBJECT.aout GNUOBJECT.o
"""

import re
import struct
import subprocess
import sys


def sections_matching(src, want):
    """Section names the source opens, in order, whose kind is in want.

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
            else:
                m = re.match(r"\.(text|data|bss|rodata)\b", stmt)
                if m:
                    cur = "." + m.group(1)
            if cur is None:
                continue
            for kind in want:
                if cur.startswith(kind) and cur not in order:
                    order.append(cur)
    return order


def text_sections(src):
    return sections_matching(src, (".text",))


def data_sections(src):
    """a_data is the data segment then the rodata pseudo-segment."""
    return (sections_matching(src, (".data",))
            + sections_matching(src, (".rodata",)))


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


def covered_by_relocs(gnuobj, base, aout, which):
    """Byte offsets holding a relocated field on either side of one segment.

    A relocated field cannot agree between the two: an a.out record stores a
    plain displacement or a segment-relative address, an ELF record stores
    what R_ARM_THM_CALL and R_ARM_ABS32 prescribe, and neither is wrong. The
    offsets come from each object's own relocation table, so the exclusion is
    read from the data rather than assumed.
    """
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
            for i in range(off, off + width):
                hit.add(i)

    d = open(aout, "rb").read()
    _, text, data, _, rt, rd, _, _ = struct.unpack("<8I", d[:32])
    if which == "text":
        buf = d[32 + text + data:32 + text + data + rt]
    else:
        buf = d[32 + text + data + rt:32 + text + data + rt + rd]
    i = 0
    while i + 5 <= len(buf):
        flags = buf[i]
        addr = struct.unpack("<I", buf[i + 1:i + 5])[0]
        i += 5
        if (flags & 0x70) == 0x70:          # REXT carries a symbol index
            i += 3
        width = 4 if (flags & 0x0f) in (1, 8) else 2
        for k in range(addr, addr + width):
            hit.add(k)
    return hit


def compare(kind, gnu, mine, hit, step):
    """Halfword or byte comparison, excluding the relocated fields."""
    n = min(len(gnu), len(mine))
    differ = [i for i in range(0, n, step) if gnu[i:i + step] != mine[i:i + step]]
    bad = [i for i in differ
           if not any(k in hit for k in range(i, i + step))]
    return differ, bad, n


def main():
    src, aout, gnuobj = sys.argv[1:4]

    d = open(aout, "rb").read()
    _, text, data, _, _, _, _, _ = struct.unpack("<8I", d[:32])

    torder = text_sections(src)
    gnutext, tbase = gnu_text(gnuobj, torder, aout + ".sec")
    mytext = d[32:32 + text]
    thit = covered_by_relocs(gnuobj, tbase, aout, "text")
    tdiff, tbad, tn = compare("text", gnutext, mytext, thit, 2)

    # The a.out text is padded out to a word; GNU as leaves the section short.
    tail = mytext[len(gnutext):]
    if tail and tail not in (b"\0" * len(tail),
                             b"\xc0\x46" * (len(tail) // 2)):
        tbad.append(len(gnutext))

    dorder = data_sections(src)
    gnudata, dbase = gnu_text(gnuobj, dorder, aout + ".sec")
    mydata = d[32 + text:32 + text + data]
    dhit = covered_by_relocs(gnuobj, dbase, aout, "data")
    ddiff, dbad, dn = compare("data", gnudata, mydata, dhit, 1)

    # The data segment is padded out to a word in the same way.
    dtail = mydata[len(gnudata):]
    if dtail and dtail != b"\0" * len(dtail):
        dbad.append(len(gnudata))
    if len(gnudata) > len(mydata):
        dbad.append(len(mydata))

    print("%-26s text %4d hw %3d reloc %d bad | data %4d B %3d reloc %d bad"
          % (src.split("/")[-1], tn // 2, len(tdiff), len(tbad),
             dn, len(ddiff), len(dbad)))
    for i in tbad[:6]:
        print("    text @%d expected %s got %s"
              % (i, gnutext[i:i + 2].hex(), mytext[i:i + 2].hex()))
    for i in dbad[:6]:
        print("    data @%d expected %s got %s"
              % (i, gnudata[i:i + 4].hex(), mydata[i:i + 4].hex()))
    return 1 if (tbad or dbad) else 0


if __name__ == "__main__":
    sys.exit(main())
