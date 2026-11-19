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
"""Run cpu.c against the SingleStepTests processor vectors.

Each vector gives the registers and memory before one instruction and what
a real processor left after it. This driver feeds the vectors of a
directory of NN.json.gz files (github.com/SingleStepTests/8088, v2) to
tests/sstrun and compares registers, flags under the suite's mask of
defined flags, and memory. The vectors are not part of this tree: the
directory is named on the command line.

    sst.py SSTRUN DIR [--limit N] [--min-pass PERCENT] [--only OP,...]

Exit status 0 when every documented form passes every sampled vector.
"""

import concurrent.futures
import gzip
import json
import os
import subprocess
import sys

REGS = ["ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"]


def form_meta(meta, name, test):
    """The metadata entry of a vector: per opcode, or per reg field."""
    entry = meta.get(name.split(".")[0], {})
    if "reg" in entry:
        if "." in name:
            reg = name.split(".")[1]
        else:
            body = [b for b in test["bytes"]]
            while body and body[0] in (0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF2, 0xF3):
                body = body[1:]
            reg = str((body[1] >> 3) & 7)
        entry = entry["reg"].get(reg, {})
    return entry


def run_file(args):
    sstrun, path, meta, limit = args
    name = os.path.basename(path).split(".json")[0]
    with gzip.open(path, "rt") as handle:
        tests = json.load(handle)
    if limit:
        tests = tests[:limit]
    lines = []
    for test in tests:
        init = test["initial"]
        want = dict(init["regs"])
        want.update(test["final"]["regs"])
        ram = {}
        for addr, value in init["ram"]:
            ram[addr] = value
        for addr, value in test["final"]["ram"]:
            ram[addr] = value
        test["_want"] = want
        test["_ram"] = sorted(ram.items())
        parts = [str(init["regs"][r]) for r in REGS]
        parts.append(str(len(init["ram"])))
        for addr, value in init["ram"]:
            parts += [str(addr), str(value)]
        parts.append(str(len(ram)))
        parts += [str(addr) for addr, _ in test["_ram"]]
        lines.append(" ".join(parts))
    done = subprocess.run(
        [sstrun, "0"], input="\n".join(lines) + "\n", capture_output=True, text=True, check=False
    )
    out = done.stdout.splitlines()
    if done.returncode != 0 or len(out) != len(tests):
        return (
            name,
            "normal",
            len(tests),
            len(tests),
            "sstrun failed: exit %d, %d replies" % (done.returncode, len(out)),
        )
    failed = 0
    first = None
    status = "normal"
    for test, line in zip(tests, out):
        entry = form_meta(meta, name, test)
        status = entry.get("status", "normal")
        mask = entry.get("flags-mask", 0xFFFF)
        got = [int(x) for x in line.split()]
        why = None
        # A divide error leaves the arithmetic flags undefined, in the
        # register and in the image the interrupt pushed.
        pushed = {}
        init_regs = test["initial"]["regs"]
        want_sp = test["_want"]["sp"]
        if name[:2] in ("D4", "F6", "F7") and want_sp == (init_regs["sp"] - 6) & 0xFFFF:
            mask &= ~0x08D5
            base = test["_want"]["ss"] << 4
            pushed[(base + ((want_sp + 4) & 0xFFFF)) & 0xFFFFF] = mask & 0xFF
            pushed[(base + ((want_sp + 5) & 0xFFFF)) & 0xFFFFF] = mask >> 8
        for i, reg in enumerate(REGS):
            a, b = got[i], test["_want"][reg]
            if reg == "flags":
                a &= mask
                b &= mask
            if a != b:
                why = "%s got %04x want %04x" % (reg, got[i], test["_want"][reg])
                break
        if why is None:
            for (addr, value), have in zip(test["_ram"], got[len(REGS) :]):
                if (value ^ have) & pushed.get(addr, 0xFF):
                    why = "ram %05x got %02x want %02x" % (addr, have, value)
                    break
        if why is not None:
            failed += 1
            if first is None:
                first = "%s [%s]: %s" % (
                    test["name"],
                    " ".join("%02x" % b for b in test["bytes"]),
                    why,
                )
    return name, status, len(tests), failed, first


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    sstrun, directory = argv[1], argv[2]
    limit, only, min_pass = 0, None, 100.0
    rest = argv[3:]
    while rest:
        if rest[0] == "--limit":
            limit = int(rest[1])
        elif rest[0] == "--only":
            only = set(rest[1].upper().split(","))
        elif rest[0] == "--min-pass":
            min_pass = float(rest[1])
        else:
            sys.stderr.write(__doc__)
            return 2
        rest = rest[2:]
    with open(os.path.join(directory, "metadata.json")) as handle:
        meta = json.load(handle)["opcodes"]
    jobs = []
    for entry in sorted(os.listdir(directory)):
        if not entry.endswith(".json.gz"):
            continue
        name = entry.split(".json")[0]
        if only and name.split(".")[0] not in only and name not in only:
            continue
        jobs.append((sstrun, os.path.join(directory, entry), meta, limit))
    if not jobs:
        sys.stderr.write("sst: no vectors in %s\n" % directory)
        return 2
    bad = total = 0
    with concurrent.futures.ProcessPoolExecutor() as pool:
        for name, status, count, failed, first in pool.map(run_file, jobs):
            documented = status == "normal"
            if documented:
                total += count
            if failed:
                rate = 100.0 * (count - failed) / count
                print("%-5s %-12s %5d/%d failed  %s" % (name, status, failed, count, first))
                if documented and rate < min_pass:
                    bad += 1
    print("sst: %d documented vectors, %d forms below %.1f%%" % (total, bad, min_pass))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
