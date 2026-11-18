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
#
# Run the production loadav() of sys/kern/vm_sched.c on held and vanishing
# loads. The function, its LOADAV_* constants and its state are extracted
# from the source and built for the host by cc and clang with NPROC of 16
# (rp2040) and 25 (stm32), which also compiles the _Static_assert that bounds
# the product. On the scale-256 averages that sysctl reports it requires:
#   - a load of 1, 4 and NPROC held for 2,000 samples to read exactly 256 n;
#   - a full table that then empties to read exactly 0;
#   - the first sample of one process from zero to read 20, 4 and 1;
#   - the averages to pass 256 (1 - 1/e) after 12, 60 and 180 samples, one
#     either way.
# The 2.11BSD function the port carried before, eight fraction bits
# truncated toward minus infinity, must fail: a held load of 1 stops at 244,
# 205 and 129.
#
# usage: loadav_precision.py

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "sys/kern/vm_sched.c"
OLD = r"""
static void
loadav (register short *avg, register int n)
{
    register int    i;
    static const long cexp[3] = {
        0353,   /* 256 * exp(-1/12)  */
        0373,   /* 256 * exp(-1/60)  */
        0376,   /* 256 * exp(-1/180) */
    };

    for (i = 0; i < 3; i++)
        avg[i] = (cexp[i] * (avg[i]-(n<<8)) + (((long)n)<<16)) >> 8;
}
"""
HARNESS = r"""
#include <stdio.h>
#include <string.h>
static short avg[3];
static int fail(const char *what, int i)
{
    printf("%s (average %d: %d)\n", what, i, avg[i]);
    return 1;
}
int main(void)
{
    static const int constants[3] = {12, 60, 180};
    int loads[3] = {1, 4, NPROC};
    int i, k, f = 0;

    loadav(avg, 1);
    if (avg[0] != 20 || avg[1] != 4 || avg[2] != 1)
        f |= fail("first sample of one process is not 20, 4, 1", 0);
    for (int l = 0; l < 3; l++) {
        for (k = 0; k < 2000; k++)
            loadav(avg, loads[l]);
        for (i = 0; i < 3; i++)
            if (avg[i] != 256 * loads[l])
                f |= fail("a held load does not read 256 n", i);
    }
    for (k = 0; k < 4000; k++)
        loadav(avg, 0);
    for (i = 0; i < 3; i++)
        if (avg[i] != 0)
            f |= fail("an emptied table does not read 0", i);
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4000; k++)
            loadav(avg, 0);
        for (k = 1; k <= 1000; k++) {
            loadav(avg, 1);
            if (avg[i] >= 162)
                break;
        }
        if (k < constants[i] - 1 || k > constants[i] + 1) {
            printf("average %d passed 1 - 1/e after %d samples, not %d\n", i, k, constants[i]);
            f = 1;
        }
    }
    return f;
}
"""
PREFIX = "typedef unsigned int u_int;\n#define NPROC %d\n"


def extract():
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("#define LOADAV_FSHIFT")
    end = text.index("\n}\n", text.index("\nloadav (short *avg, int n)")) + 3
    return text[start:end]


def run(compiler, directory, body, nproc):
    fixture = directory / "loadav.c"
    fixture.write_text(PREFIX % nproc + body + HARNESS, encoding="utf-8")
    binary = directory / "loadav"
    built = subprocess.run(
        [
            compiler,
            "-std=gnu17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(fixture),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    if built.returncode != 0:
        return None, built.stderr.strip()
    result = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
    return result.returncode, result.stdout.strip()


def main():
    compilers = [name for name in ("cc", "clang") if shutil.which(name)]
    if not compilers:
        print("ERROR loadav-precision: no host C compiler (cc or clang)")
        return 2
    body = extract()
    with tempfile.TemporaryDirectory(prefix="discobsd-loadav-") as temporary:
        directory = Path(temporary)
        for compiler in compilers:
            for nproc in (16, 25):
                status, output = run(compiler, directory, body, nproc)
                if status != 0:
                    print(f"FAIL loadav-precision: {compiler} NPROC={nproc}: {output}")
                    return 1
            status, output = run(compiler, directory, OLD, 16)
            if status in (None, 0):
                print(
                    f"FAIL loadav-precision: {compiler}: the 2.11BSD loadav was not "
                    f"rejected: {output}"
                )
                return 1
    print(
        f"PASS loadav-precision: vm_sched.c loadav with NPROC 16 and 25 by "
        f"{' and '.join(compilers)}, 2.11BSD loadav rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
