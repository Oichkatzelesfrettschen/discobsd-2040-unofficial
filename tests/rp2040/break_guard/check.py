"""Check the RP2040 break guard arithmetic against a PMSAv6 match model."""

# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
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

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

WINDOW_START = 0x20000000
WINDOW_END = 0x20030000
GUARD = 32
MARGIN = 1024
REGION = 7
AP_PRIV_RW_USER_RO = 2

# The consumer reads "brk saddr sp" triples in hexadecimal and prints
# "floor rbar rasr" for each, so the model below decides every verdict.
CONSUMER = r"""#include <stdio.h>
#include <machine/breakguard.h>

int
main(void)
{
    unsigned int brk, saddr, sp, floor;

    while (scanf("%x %x %x", &brk, &saddr, &sp) == 3) {
        floor = rp2040_break_guard_floor(brk, saddr, sp);
        printf("%x %x %x\n", floor, rp2040_break_guard_rbar(floor),
            rp2040_break_guard_rasr(floor));
    }
    return 0;
}
"""

# Each mutant replaces one anchor exactly once; the model must reject it.
MUTANTS = (
    ("subregion-from-bit-8", "((base >> 5) & 7U)", "((base >> 8) & 7U)"),
    ("srd-inverted", "0xffU & ~(1U << ((base >> 5) & 7U))", "0xffU & (1U << ((base >> 5) & 7U))"),
    (
        "threshold-off-by-one",
        "stack - base < BREAK_GUARD_BYTES + BREAK_GUARD_MARGIN)",
        "stack - base < BREAK_GUARD_BYTES + BREAK_GUARD_MARGIN - 1U)",
    ),
    (
        "ap-000",
        "#define\tBREAK_GUARD_RASR_AP\t(2U << 24)",
        "#define\tBREAK_GUARD_RASR_AP\t(0U << 24)",
    ),
    (
        "guard-past-p_saddr",
        "unsigned int stack = saddr < sp ? saddr : sp;",
        "unsigned int stack = sp; (void)saddr;",
    ),
    (
        "size-field-512",
        "#define\tBREAK_GUARD_RASR_SIZE\t(7U << 1)",
        "#define\tBREAK_GUARD_RASR_SIZE\t(8U << 1)",
    ),
)


class ContractError(ValueError):
    """The break guard arithmetic or a calibrated verdict differs."""


def require(condition, message):
    if not condition:
        raise ContractError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cases():
    """Every 32-byte guard base in the window, approached three ways."""
    rows = []
    top = WINDOW_END
    for base in range(WINDOW_START, WINDOW_END, GUARD):
        for brk in (base - 31, base - 1, base):
            if brk < WINDOW_START:
                continue
            for stack in (base, base + GUARD + MARGIN - 1, base + GUARD + MARGIN, top):
                if stack > top:
                    continue
                rows.append((brk, stack, top))
                rows.append((brk, top, stack))
                rows.append((brk, stack, stack + 1))
    return rows


def pmsav6_covered(rbar, rasr):
    """Byte intervals an enabled region matches, per ARMv6-M ARM B3.5.

    RASR.SIZE gives 2^(SIZE+1) bytes and must be at least 7; RBAR.ADDR must
    be aligned to the region size; a set SRD bit removes its eighth.
    """
    if rasr & 1 == 0:
        return []
    size_field = (rasr >> 1) & 0x1F
    require(size_field >= 7, "RASR SIZE below the 256-byte minimum")
    size = 1 << (size_field + 1)
    base = rbar & 0xFFFFFF00
    require(base % size == 0, "region base not aligned to its size")
    srd = (rasr >> 8) & 0xFF
    eighth = size // 8
    return [
        (base + index * eighth, base + (index + 1) * eighth)
        for index in range(8)
        if not srd & (1 << index)
    ]


def verdicts(rows, output_lines):
    """Return the first contract violation, or None."""
    require(len(output_lines) == len(rows), "consumer output count differs")
    for (brk, saddr, sp), line in zip(rows, output_lines):
        floor, rbar, rasr = (int(field, 16) for field in line.split())
        label = f"brk {brk:#x} saddr {saddr:#x} sp {sp:#x}"
        guard = -(-brk // GUARD) * GUARD
        stack = min(saddr, sp)
        armed = stack >= guard + GUARD + MARGIN
        try:
            intervals = pmsav6_covered(rbar, rasr)
        except ContractError as error:
            return f"{label}: {error}"
        if rbar & 0xF != REGION:
            return f"{label}: RBAR region {rbar & 0xF}, expected {REGION}"
        if rbar & 0x10 == 0:
            return f"{label}: RBAR VALID clear"
        for low, high in intervals:
            if low < brk:
                return f"{label}: covers {low:#x}, below the break"
            if high > stack - MARGIN:
                return f"{label}: covers up to {high:#x}, within the margin"
            if low < WINDOW_START or high > WINDOW_END:
                return f"{label}: covers {low:#x}..{high:#x} outside the window"
        if armed:
            if intervals != [(guard, guard + GUARD)]:
                return f"{label}: covers {intervals}, expected [{guard:#x}, +32)"
            if (rasr >> 24) & 7 != AP_PRIV_RW_USER_RO:
                return f"{label}: AP {(rasr >> 24) & 7}, expected 010"
            if rasr & (1 << 28) == 0:
                return f"{label}: XN clear"
            if floor != guard + GUARD + MARGIN:
                return f"{label}: floor {floor:#x}, expected {guard + 1056:#x}"
        else:
            if intervals or rasr != 0:
                return f"{label}: armed with RASR {rasr:#x}, expected off"
            if floor != 0:
                return f"{label}: floor {floor:#x} while off"
    return None


def evaluate(compiler, include, workspace, label, rows, stdin, record):
    consumer = workspace / "consumer.c"
    executable = workspace / (label + ".test")
    command = [
        *compiler,
        "-std=c17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I" + str(include),
        str(consumer),
        "-o",
        str(executable),
    ]
    built = subprocess.run(
        command, capture_output=True, text=True, env=dict(os.environ, LC_ALL="C")
    )
    require(built.returncode == 0, label + " compile: " + built.stderr)
    ran = subprocess.run([str(executable)], input=stdin, capture_output=True, text=True)
    require(ran.returncode == 0, label + " execute: " + ran.stderr)
    violation = verdicts(rows, ran.stdout.splitlines())
    record.append({"label": label, "argv": command, "violation": violation})
    return violation


def check(root, host_cc, output):
    header = root / "sys/arch/rp2040/include/breakguard.h"
    original = header.read_text()
    rows = cases()
    stdin = "".join(f"{b:x} {s:x} {p:x}\n" for b, s, p in rows)
    host = host_cc.split()
    require(shutil.which(host[0]) is not None, host[0] + " is not installed")
    record = []
    with tempfile.TemporaryDirectory(prefix="break-guard-") as temporary:
        workspace = Path(temporary)
        (workspace / "consumer.c").write_text(CONSUMER)
        real = workspace / "real"
        (real / "machine").mkdir(parents=True)
        (real / "machine/breakguard.h").write_text(original)
        violation = evaluate(host, real, workspace, "header", rows, stdin, record)
        require(violation is None, "header violates the contract: " + str(violation))
        arm = shutil.which("arm-none-eabi-gcc")
        if arm is None:
            record.append({"label": "arm.c17", "skip": "arm-none-eabi-gcc is not installed"})
            print("SKIP break guard arm.c17: arm-none-eabi-gcc not installed")
        else:
            command = [
                arm,
                "-std=c17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-mcpu=cortex-m0plus",
                "-mthumb",
                "-fsyntax-only",
                "-I" + str(real),
                str(workspace / "consumer.c"),
            ]
            built = subprocess.run(command, capture_output=True, text=True)
            require(built.returncode == 0, "arm.c17: " + built.stderr)
            record.append({"label": "arm.c17", "argv": command, "violation": None})
        for name, anchor, replacement in MUTANTS:
            require(original.count(anchor) == 1, name + ": anchor does not occur exactly once")
            tree = workspace / name
            (tree / "machine").mkdir(parents=True)
            (tree / "machine/breakguard.h").write_text(original.replace(anchor, replacement, 1))
            violation = evaluate(host, tree, workspace, name, rows, stdin, record)
            require(violation is not None, name + " mutant was accepted")
            print(f"PASS break guard mutant {name} rejected: {violation}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "source_hashes": {str(header.relative_to(root)): digest(header)},
                "rows": len(rows),
                "cases": record,
            },
            indent=2,
        )
        + "\n"
    )
    return len(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        count = check(arguments.root.resolve(), arguments.host_cc, arguments.output)
    except (ContractError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit("break guard contracts: " + str(error)) from error
    print(f"PASS RP2040 break guard: {count} cases, {len(MUTANTS)} mutants rejected")


if __name__ == "__main__":
    main()
