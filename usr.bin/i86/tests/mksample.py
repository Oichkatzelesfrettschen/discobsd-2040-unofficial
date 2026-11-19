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
"""Write tests/sst8088-sample.txt.gz from the SingleStepTests 8088 vectors.

    mksample.py DIR OUT.txt

DIR holds metadata.json and the NN.json.gz files of
github.com/SingleStepTests/8088 v2 (revision aea84484 made the sample in
this tree). The sample takes twelve vectors spread over each documented
instruction form, and three hundred of DAA and DAS, whose high-digit rule
few vectors reach, and of both IDIV forms, where a repeat prefix inverts
the quotient in about one vector of fifty. A line holds six fields separated by "|", numbers in
hexadecimal: the form, the fourteen registers before (ax bx cx dx cs ss ds
es sp bp si di ip flags), the mask of defined flags, address-value pairs to
store, the registers after, and address-value-mask triples to compare.
Compress OUT.txt with gzip -9 -n.
"""

import gzip
import json
import os
import sys

REGS = ["ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"]
PREFIXES = (0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF2, 0xF3)
PER_FORM = 12
PER_FORM_OF = {"27": 300, "2F": 300, "F6.7": 300, "F7.7": 300}


def hexes(values):
    return " ".join("%x" % value for value in values)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    src, out = argv[1], argv[2]
    with open(os.path.join(src, "metadata.json")) as handle:
        meta = json.load(handle)["opcodes"]
    lines = []
    for entry in sorted(os.listdir(src)):
        if not entry.endswith(".json.gz"):
            continue
        name = entry.split(".json")[0]
        with gzip.open(os.path.join(src, entry), "rt") as handle:
            tests = json.load(handle)
        picked = 0
        count = PER_FORM_OF.get(name, PER_FORM)
        for test in tests[:: max(1, len(tests) // (2 * count))]:
            form = meta.get(name.split(".")[0], {})
            if "reg" in form:
                if "." in name:
                    reg = name.split(".")[1]
                else:
                    body = list(test["bytes"])
                    while body and body[0] in PREFIXES:
                        body = body[1:]
                    reg = str((body[1] >> 3) & 7)
                form = form["reg"].get(reg, {})
            if form.get("status", "normal") != "normal":
                continue
            mask = form.get("flags-mask", 0xFFFF)
            init = test["initial"]
            want = dict(init["regs"])
            want.update(test["final"]["regs"])
            ram = dict((a, v) for a, v in init["ram"])
            ram.update((a, v) for a, v in test["final"]["ram"])
            # A divide error leaves the arithmetic flags undefined, in
            # the register and in the image the interrupt pushed.
            ram_mask = {}
            if name[:2] in ("D4", "F6", "F7") and want["sp"] == (init["regs"]["sp"] - 6) & 0xFFFF:
                mask &= ~0x08D5
                base = want["ss"] << 4
                low = (base + ((want["sp"] + 4) & 0xFFFF)) & 0xFFFFF
                high = (base + ((want["sp"] + 5) & 0xFFFF)) & 0xFFFFF
                ram_mask[low] = mask & 0xFF
                ram_mask[high] = mask >> 8
            lines.append(
                "|".join(
                    [
                        name,
                        hexes(init["regs"][r] for r in REGS),
                        "%x" % mask,
                        hexes(x for pair in init["ram"] for x in pair),
                        hexes(want[r] for r in REGS),
                        hexes(
                            x for a, v in sorted(ram.items()) for x in (a, v, ram_mask.get(a, 0xFF))
                        ),
                    ]
                )
            )
            picked += 1
            if picked == count:
                break
    with open(out, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    print("%d vectors" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
