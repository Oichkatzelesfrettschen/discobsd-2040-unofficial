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
"""Write the processor vector samples of this directory.

    mksample.py 8088 DIR OUT.txt
    mksample.py 80286 DIR OUT.txt

For 8088, DIR holds metadata.json and the NN.json.gz files of
github.com/SingleStepTests/8088 v2 (revision aea84484 made
sst8088-sample.txt.gz). For 80286, DIR is a checkout of
github.com/SingleStepTests/80286 (revision 37c73caf made
sst80286-sample.txt.gz): its tools/moo2json.py converts each file of
v1_real_mode.

A sample takes vectors spread over each documented instruction form: twelve
of the 8088 (three hundred of DAA and DAS, whose high-digit rule few
vectors reach, and of both IDIV forms, where a repeat prefix inverts the
quotient in about one vector of fifty) and eight of the 80286 (three
hundred of DAA and DAS). A line holds six fields separated by "|", numbers
in hexadecimal: the form, the fourteen registers before (ax bx cx dx cs ss
ds es sp bp si di ip flags), the mask of defined flags, address-value pairs
to store, the registers after one instruction, and address-value-mask
triples to compare. Compress OUT.txt with gzip -9 -n.

What the samples leave out, by rule and not by result:

  - both suites: after a divide error the arithmetic flags, in the
    register and in the image the interrupt pushed, which no manual
    defines;
  - 80286: vectors that touch memory above one megabyte (the machine of
    pc.c holds address line 20 low), vectors that end in interrupt 12 or
    13 (the segment-limit checks cpu.c does not make) and the HALT each
    vector appends, whose single byte is subtracted from the final IP.
"""

import gzip
import json
import os
import subprocess
import sys
import tempfile

REGS = ["ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"]
PREFIXES = (0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF2, 0xF3)
PER_FORM = {"8088": 12, "80286": 8}
PER_FORM_OF = {
    "8088": {"27": 300, "2F": 300, "F6.7": 300, "F7.7": 300},
    "80286": {"27": 300, "2F": 300},
}
ARITHMETIC = 0x08D5


def hexes(values):
    return " ".join("%x" % value for value in values)


def body(test):
    data = list(test["bytes"])
    while data and data[0] in PREFIXES:
        data = data[1:]
    return data


def form_of(meta, name, test):
    form = meta.get(name.split(".")[0], {})
    if "reg" in form:
        if "." in name:
            reg = name.split(".")[1]
        else:
            data = body(test)
            reg = str((data[1] >> 3) & 7) if len(data) > 1 else "0"
        form = form["reg"].get(reg, {})
    return form


def line_of(suite, name, form, test):
    """The sample line of a vector, or None when a rule leaves it out."""
    mask = form.get("flags-mask", 0xFFFF)
    init = test["initial"]
    want = dict(init["regs"])
    want.update(test["final"]["regs"])
    ram = dict((a, v) for a, v in init["ram"])
    ram.update((a, v) for a, v in test["final"]["ram"])
    ram_mask = {}
    flags_at = None
    if suite == "8088":
        if name[:2] in ("D4", "F6", "F7") and want["sp"] == (init["regs"]["sp"] - 6) & 0xFFFF:
            flags_at = ((want["ss"] << 4) + ((want["sp"] + 4) & 0xFFFF)) & 0xFFFFF
            high = ((want["ss"] << 4) + ((want["sp"] + 5) & 0xFFFF)) & 0xFFFFF
    else:
        if any(a >= 0x100000 for a in ram):
            return None
        raised = test.get("exception")
        number = raised.get("number") if isinstance(raised, dict) else None
        if number in (12, 13):
            return None
        if number == 0:
            flags_at = raised.get("flag_address")
            high = None if flags_at is None else flags_at + 1
            if flags_at is None:
                mask &= ~ARITHMETIC
        if body(test)[:1] != [0xF4]:
            want["ip"] = (want["ip"] - 1) & 0xFFFF
    if flags_at is not None:
        mask &= ~ARITHMETIC
        ram_mask[flags_at] = mask & 0xFF
        ram_mask[high] = mask >> 8
    return "|".join(
        [
            name,
            hexes(init["regs"][r] for r in REGS),
            "%x" % mask,
            hexes(x for pair in init["ram"] for x in pair),
            hexes(want[r] for r in REGS),
            hexes(x for a, v in sorted(ram.items()) for x in (a, v, ram_mask.get(a, 0xFF))),
        ]
    )


def load(suite, src, entry):
    if suite == "8088":
        with gzip.open(os.path.join(src, entry), "rt") as handle:
            return json.load(handle)
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "form.json")
        subprocess.run(
            [
                sys.executable,
                "-I",
                os.path.join(src, "tools", "moo2json.py"),
                os.path.join(src, "v1_real_mode", entry),
                out,
            ],
            capture_output=True,
            check=True,
        )
        with open(out) as handle:
            return json.load(handle)


def main(argv):
    if len(argv) != 4 or argv[1] not in PER_FORM:
        sys.stderr.write(__doc__)
        return 2
    suite, src, out = argv[1:]
    where = src if suite == "8088" else os.path.join(src, "v1_real_mode")
    suffix = ".json.gz" if suite == "8088" else ".MOO.gz"
    with open(os.path.join(where, "metadata.json")) as handle:
        meta = json.load(handle)["opcodes"]
    lines = []
    for entry in sorted(os.listdir(where)):
        if not entry.endswith(suffix):
            continue
        name = entry[: -len(suffix)]
        tests = load(suite, src, entry)
        count = PER_FORM_OF[suite].get(name, PER_FORM[suite])
        picked = 0
        for test in tests[:: max(1, len(tests) // (2 * count))]:
            form = form_of(meta, name, test)
            if form.get("status", "normal") != "normal":
                continue
            line = line_of(suite, name, form, test)
            if line is None:
                continue
            lines.append(line)
            picked += 1
            if picked == count:
                break
    with open(out, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    print("%d vectors" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
