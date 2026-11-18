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
# Run the production bcopy of each machdep.c on overlapping ranges.
#
# namei() shifts the rest of a path within its own buffer when it follows a
# symbolic link, a copy whose destination can start inside its source, and
# the dhara compat memmove passes overlapping slides straight to bcopy. Each
# bcopy is extracted from sys/arch/{rp2040,stm32}/.../machdep.c with the
# UNALIGNED macro above it, compiled for the host by cc and clang, and run on:
#   - three namei shifts: "l/file" through a link to "ab", "bin/ls" through
#     "usr/bin" (no overlap) and a long remainder through "target";
#   - every length 0..40 at source offsets 0..7 and destination offsets
#     -8..8 from it, against memmove, with guard bytes around the span.
# A mutant without the backward branch, the forward-only copy the kernels
# shipped before, must fail; without that rejection the verdict counts for
# nothing.
#
# usage: bcopy_overlap.py

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("sys/arch/rp2040/rp2040/machdep.c", "sys/arch/stm32/stm32/machdep.c")
PREFIX = """\
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u_char;
typedef unsigned int u_int;
"""
HARNESS = r"""
static int shift(const char *start, unsigned consumed, const char *target)
{
    char path[64] = {0}, want[64] = {0};
    strcpy(path, start);
    strcpy(want, start);
    char *cp = path + consumed;
    unsigned pathlen = strlen(cp) + 1, size = strlen(target);
    memmove(want + size, want + consumed, pathlen);
    memcpy(want, target, size);
    bcopy(cp, path + size, pathlen);
    bcopy(target, path, size);
    if (memcmp(path, want, sizeof(path)) != 0) {
        printf("namei shift of %s through %s gave %.40s\n", start, target, path);
        return 1;
    }
    return 0;
}

int main(void)
{
    _Alignas(8) unsigned char buffer[96], expected[96];
    int failures = 0;

    failures += shift("l/file", 1, "ab");
    failures += shift("bin/ls", 3, "usr/bin");
    failures += shift("a/long/remaining/path/x", 1, "target");
    for (int length = 0; length <= 40; length++)
        for (int source = 0; source < 8; source++)
            for (int delta = -8; delta <= 8; delta++) {
                for (int index = 0; index < 96; index++)
                    buffer[index] = expected[index] = (unsigned char)(index * 7 + 1);
                memmove(expected + 24 + source + delta, expected + 24 + source, length);
                bcopy(buffer + 24 + source, buffer + 24 + source + delta, length);
                if (memcmp(buffer, expected, sizeof(buffer)) != 0)
                    failures++;
            }
    if (failures)
        printf("%d cases differ\n", failures);
    return failures != 0;
}
"""
BACKWARD = re.compile(
    r"\t/\* dst inside [^\n]*\n"
    r"\tif \(\(u_int\)dst - \(u_int\)src < nbytes && dst != src\) \{\n"
    r".*?\n\t\}\n",
    re.S,
)


def extract(path):
    """bcopy of PATH and the UNALIGNED macro above it, as written."""
    source = (ROOT / path).read_text(encoding="utf-8")
    start = source.index("/* Nonzero if pointer is not aligned")
    end = source.index("\n}\n", source.index("\nbcopy(const void *src0")) + 3
    return source[start:end]


# The kernel casts pointers to the 32-bit u_int, a narrowing on a 64-bit
# host, so the host copy casts to unsigned long; u_int stays 32 bits, as the
# word loops assume. Each spelling must occur exactly once.
HOST_CASTS = (
    ("((u_int)(p) & ((sz) - 1))", "((unsigned long)(p) & ((sz) - 1))"),
    ("(u_int)dst - (u_int)src", "(unsigned long)dst - (unsigned long)src"),
)


def for_host(path, body, required):
    for kernel, host in HOST_CASTS:
        count = body.count(kernel)
        if count > 1 or (kernel in required and count != 1):
            raise SystemExit(f"FAIL bcopy-overlap: {path}: {kernel} changed")
        body = body.replace(kernel, host)
    return body


def run(compiler, directory, body):
    fixture = directory / "bcopy.c"
    fixture.write_text(PREFIX + body + HARNESS, encoding="utf-8")
    binary = directory / "bcopy"
    subprocess.run(
        [
            compiler,
            "-std=gnu17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fno-builtin",
            str(fixture),
            "-o",
            str(binary),
        ],
        check=True,
    )
    return subprocess.run([str(binary)], capture_output=True, text=True, check=False)


def main():
    compilers = [name for name in ("cc", "clang") if shutil.which(name)]
    if not compilers:
        print("ERROR bcopy-overlap: no host C compiler (cc or clang)")
        return 2
    with tempfile.TemporaryDirectory(prefix="discobsd-bcopy-") as temporary:
        directory = Path(temporary)
        for path in SOURCES:
            original = extract(path)
            mutant, removed = BACKWARD.subn("", original)
            body = for_host(path, original, {kernel for kernel, _ in HOST_CASTS})
            mutant = for_host(path, mutant, {HOST_CASTS[0][0]})
            if removed != 1:
                print(f"FAIL bcopy-overlap: {path}: no backward branch to remove")
                return 1
            for compiler in compilers:
                result = run(compiler, directory, body)
                if result.returncode != 0:
                    print(f"FAIL bcopy-overlap: {path} with {compiler}: {result.stdout.strip()}")
                    return 1
                result = run(compiler, directory, mutant)
                if result.returncode == 0:
                    print(
                        f"FAIL bcopy-overlap: {path} with {compiler}: "
                        "the forward-only mutant passed"
                    )
                    return 1
    print(
        f"PASS bcopy-overlap: {len(SOURCES)} machdep.c copies with "
        f"{' and '.join(compilers)}, forward-only mutant rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
