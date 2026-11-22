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
"""Count Cortex-M0+ cycles of the decoder loops in dec_kernels.c.

Input is the assembly arm-none-eabi-gcc writes for dec_kernels.c. A loop is a backward branch
and its target; the body cost adds the Cortex-M0+ cycle counts of its instructions (loads and
stores 2, other ALU 1, taken branch 2, not taken 1, push or pop 1 + registers). The function
epilogue (pop of pc) lies in the span of a compiler-rotated loop but runs once at exit, so it
is left out and the branch that guards it counts as taken. A loop with a conditional skip is
reported for the skip not taken and taken. Nested loops need the formula in README.md.
"""

import argparse
import re

NO_B = ("bics", "bl", "bx", "blx")


def cycles(op, args, taken=True):
    o = op.replace(".n", "")
    if o in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "str", "strb", "strh"):
        return 2
    if o in ("push", "pop"):
        n = len([a for a in args.strip("{}").split(",") if a.strip()])
        return 1 + n + (2 if (o == "pop" and "pc" in args) else 0)
    if o in ("ldm", "ldmia", "stm", "stmia"):
        return 1 + len(args.split("{")[1].split("}")[0].split(","))
    if o == "b":
        return 2
    if o.startswith("b") and o not in NO_B:
        return 2 if taken else 1
    if o == "bl":
        return 3
    if o in ("bx", "blx"):
        return 2
    return 1


def parse(path):
    funcs = {}
    cur = None
    for ln in open(path).read().split("\n"):
        m = re.match(r"^([a-z_0-9A-Z]+):$", ln)
        if m and not ln.startswith("."):
            cur = m.group(1)
            funcs[cur] = []
            continue
        if cur is None:
            continue
        if ln.startswith("\t.size"):
            cur = None
            continue
        m = re.match(r"^(\.L\d+):", ln)
        if m:
            funcs[cur].append(("L", m.group(1)))
            continue
        m = re.match(r"^\t([a-z][a-z0-9.]*)\s*(.*)$", ln)
        if m and not ln.startswith("\t."):
            funcs[cur].append(("I", m.group(1), m.group(2).split("@")[0].strip()))
    return funcs


def is_exit(ins):
    return ins[1] == "pop" and "pc" in ins[2]


def body_cycles(body):
    lo = hi = 0
    for k, (_, op, args) in enumerate(body):
        last = k == len(body) - 1
        if is_exit(body[k]):
            continue
        guards_exit = k + 1 < len(body) and is_exit(body[k + 1])
        cond = op.startswith("b") and op not in NO_B and op != "b"
        if cond and not last and not guards_exit:
            lo += cycles(op, args, False)
            hi += cycles(op, args, True)
        else:
            lo += cycles(op, args, True)
            hi += cycles(op, args, True)
    return lo, hi


def main():
    ap = argparse.ArgumentParser(description="Cortex-M0+ cycles of the decoder loops.")
    ap.add_argument("--asm", required=True, help="dec_kernels.s from arm-none-eabi-gcc -S")
    args = ap.parse_args()
    for fn, ins in parse(args.asm).items():
        labels = {}
        seq = []
        for it in ins:
            if it[0] == "L":
                labels[it[1]] = len(seq)
            else:
                seq.append(it)
        loops = []
        for idx, (_, op, a) in enumerate(seq):
            if op.startswith("b") and op not in NO_B and a.strip() in labels:
                if labels[a.strip()] <= idx:
                    loops.append((labels[a.strip()], idx))
        print("==", fn, "instrs", len(seq), "loops", loops)
        for a, b in sorted(loops, key=lambda x: x[1] - x[0])[:2]:
            body = seq[a : b + 1]
            lo, hi = body_cycles(body)
            print(
                "  loop %d..%d: %d instrs, %d cycles (skips not taken) to %d (taken)"
                % (a, b, len(body), lo, hi)
            )
            print("    " + " ; ".join(op + " " + x for _, op, x in body))


main()
