# Copyright (c) 2026 DiscoBSD
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
"""Write the Thumb assembly inputs of the as/ld scratch-stream gate.

Three sizes cover the three paths of the assembler's scratch streams and the
linker's segment buffer: `small.s` stays in memory in both tools, `mid.s`
fills the assembler's stream budget part way, and `big.s` carries more text
than the assembler's 32 KiB stream budget and the linker's 32 KiB segment
buffer, so both fall back to their file paths. Every module mixes the
relocation kinds the Thumb stream carries: a BL to a symbol in the same
segment (patched by the assembler), a BL to a symbol of another module, a
literal-pool load of a symbol, a word that holds a symbol address in data, an
odd-offset relocated word, strings, and common and local bss.
"""

import sys
from pathlib import Path


def function(name, callee, ext, body):
    lines = [
        "\t.globl\t%s" % name,
        "\t.thumb_func",
        "%s:" % name,
        "\tpush\t{r4, r5, lr}",
    ]
    for i in range(body):
        lines.append("\tmovs\tr%d, #%d" % (i % 4, i & 127))
        lines.append("\tadds\tr%d, r%d, #1" % ((i + 1) % 4, i % 4))
        lines.append("\tcmp\tr%d, #%d" % (i % 4, i & 127))
        lines.append("\tnop")
        lines.append("\tnop")
    lines.append("\tbne\t1f")
    lines.append("\tnop")
    lines.append("1:")
    if callee:
        lines.append("\tbl\t%s" % callee)
    if ext:
        lines.append("\tbl\t%s" % ext)
    lines.append("\tldr\tr0, =%s_data" % name)
    lines.append("\tldr\tr1, =%s_str" % name)
    lines.append("\tpop\t{r4, r5, pc}")
    lines.append("\t.ltorg")
    return lines


def module(prefix, count, body, ext_prefix=None, ext_count=0):
    out = ["\t.syntax\tunified", "\t.thumb", "\t.text"]
    for i in range(count):
        name = "%s_f%d" % (prefix, i)
        callee = "%s_f%d" % (prefix, i + 1) if i + 1 < count else None
        ext = None
        if ext_prefix and ext_count:
            ext = "%s_f%d" % (ext_prefix, i % ext_count)
        out.extend(function(name, callee, ext, body))
    out.append("\t.data")
    for i in range(count):
        name = "%s_f%d" % (prefix, i)
        out.append("\t.globl\t%s_data" % name)
        out.append("%s_data:" % name)
        out.append("\t.word\t%s" % name)
        out.append("\t.word\t%s_data+4" % name)
        out.append("\t.byte\t%d" % (i & 255))
        out.append("\t.word\t%s" % name)
    out.append("\t.section\t.rodata,\"a\",%progbits")
    for i in range(count):
        out.append("\t.globl\t%s_f%d_str" % (prefix, i))
        out.append("%s_f%d_str:" % (prefix, i))
        out.append("\t.asciz\t\"%s string %d\"" % (prefix, i))
    out.append("\t.bss")
    out.append("\t.comm\t%s_common, 64, 4" % prefix)
    out.append("\t.lcomm\t%s_local, 16" % prefix)
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: gen_inputs.py OUTDIR\n")
        return 2
    out = Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    files = {
        # main is the entry module; it calls into the library members.
        "small.s": module("small", 3, 4, "lib1", 2),
        "mid.s": module("mid", 12, 10, "lib1", 2),
        "big.s": module("big", 130, 32, "lib2", 2),
        "lib1.s": module("lib1", 2, 3),
        "lib2.s": module("lib2", 2, 3, "lib1", 2),
        "unused.s": module("unused", 2, 3),
    }
    for name, text in files.items():
        (out / name).write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
