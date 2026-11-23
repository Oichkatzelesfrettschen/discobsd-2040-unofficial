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
# The rp2040 bcopy hands disjoint ranges to the boot ROM's memcpy, which the
# host replaces with rom_memcpy_model(): the routine at 0x261d of the version 3
# ROM (Renode_RP2040 bootroms/rp2040/b2.elf) copies byte by byte from the end
# below 8 bytes or when source and destination differ in alignment, and
# forward in words otherwise, so only a same-alignment forward copy with the
# source below the destination is safe; bcopy sends the ROM disjoint ranges.
# The rp2040 copy runs twice, with the MC table slot present and with every
# slot read missing, and the run checks which path rom_fn() took. Four mutants
# must fail, each for its named reason: one without the backward branch, the
# forward-only copy the kernels shipped before; one that hands every range to
# the ROM; a wrong slot constant, which shows as a fallback to the lookup; and
# rom_fn() returning the halfword at the slot instead of slot + 2. Without those
# rejections the verdict counts for nothing.
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
#include <stdlib.h>
#include <string.h>
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
#define ROM_CODE(c1, c2) ((u_int)(c1) | ((u_int)(c2) << 8))
/*
 * The model boot ROM. With ROM_TABLE set it keeps the MC entry where ROM
 * version 3 does (Renode_RP2040 b2.elf, 0x92: "MC", then 0x261d), so rom_fn()
 * takes the slot read; without it every read misses and rom_fn() takes
 * rom_func_lookup(). rom_entry() maps the entry halfword to the host routine
 * and stops on any other value, which a read of the wrong halfword returns.
 */
#define ROM_HALF(a) rom_half(a)
#define ROM_ENTRY(a) rom_entry(ROM_HALF(a))
#define MODEL_MC_ENTRY 0x261dU
int rom_calls, lookup_calls;
void *rom_memcpy_model(void *dst, const void *src, u_int n);
u_short rom_half(u_int address);
void *rom_entry(u_short half);
u_short rom_half(u_int address)
{
#if ROM_TABLE
    if (address == 0x92)
        return (u_short)ROM_CODE('M', 'C');
    if (address == 0x94)
        return MODEL_MC_ENTRY;
#endif
    (void)address;
    return 0;
}
void *rom_entry(u_short half)
{
    if (half == MODEL_MC_ENTRY)
        return (void *)rom_memcpy_model;
    printf("rom_fn returned the halfword %#x, not the routine address\\n", half);
    exit(3);
}
void *rom_memcpy_model(void *dst, const void *src, u_int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    rom_calls++;
    if (n < 8 || (((unsigned long)d - (unsigned long)s) & 3) != 0) {
        while (n--)
            d[n] = s[n];
        return dst;
    }
    for (u_int i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}
void *rom_func_lookup(u_int code);
void *rom_func_lookup(u_int code)
{
    lookup_calls++;
    return code == ROM_CODE('M', 'C') ? (void *)rom_memcpy_model : 0;
}
/* The macOS SDK defines bcopy as a fortified macro; the kernel's is a function. */
#undef bcopy
#define bcopy kernel_bcopy
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
#if EXPECT_ROM
    if (rom_calls == 0) {
        printf("the ROM memcpy ran for no disjoint copy\n");
        failures++;
    }
#if ROM_TABLE
    if (lookup_calls != 0) {
        printf("the slot read missed: rom_fn fell back to rom_func_lookup %d times\n",
            lookup_calls);
        failures++;
    }
#else
    if (lookup_calls == 0) {
        printf("rom_fn never took rom_func_lookup with the slot read missing\n");
        failures++;
    }
#endif
#endif
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
# The rp2040 copy's ROM path: its disjointness test and the slot's address.
ROM_CASTS = (
    ("(u_int)src - (u_int)dst", "(unsigned long)src - (unsigned long)dst"),
    ("(void *)(u_int)ROM_HALF", "(void *)ROM_ENTRY"),
)
ROM_GUARD = "\tif ((u_int)src - (u_int)dst >= nbytes) {\n"
SLOT_DEFINE = "ROM_SLOT_MC\t0x92U"
SLOT_READ = "(u_int)ROM_HALF(slot + 2)"


def casts(path):
    return HOST_CASTS + (ROM_CASTS if "rp2040" in path else ())


def for_host(path, body, required):
    for kernel, host in casts(path):
        count = body.count(kernel)
        if count > 1 or (kernel in required and count != 1):
            raise SystemExit(f"FAIL bcopy-overlap: {path}: {kernel} changed")
        body = body.replace(kernel, host)
    return body


def run(compiler, directory, body, rom=None):
    """Build and run BODY; ROM is None (no ROM path), "table" (the MC slot
    holds its entry) or "lookup" (every slot read misses)."""
    fixture = directory / "bcopy.c"
    defines = [f"-DEXPECT_ROM={rom is not None:d}", f"-DROM_TABLE={rom == 'table':d}"]
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
            *defines,
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
    with tempfile.TemporaryDirectory(prefix="gizmobsd-bcopy-") as temporary:
        directory = Path(temporary)
        for path in SOURCES:
            original = extract(path)
            mutant, removed = BACKWARD.subn("", original)
            body = for_host(path, original, {kernel for kernel, _ in casts(path)})
            mutant = for_host(path, mutant, {HOST_CASTS[0][0]})
            if removed != 1:
                print(f"FAIL bcopy-overlap: {path}: no backward branch to remove")
                return 1
            rp = "rp2040" in path
            differ = ("cases differ", "namei shift")
            mutants = [("the forward-only mutant", mutant, differ, "table" if rp else None)]
            if rp:
                if original.count(ROM_GUARD) != 1:
                    print(f"FAIL bcopy-overlap: {path}: no disjointness test before the ROM call")
                    return 1
                every = original.replace(ROM_GUARD, "\tif (1) {\n")
                every = for_host(path, every, {HOST_CASTS[0][0]})
                mutants.append(
                    ("the mutant that hands every range to the ROM", every, differ, "table")
                )
                for label, old, new, reason in (
                    (
                        "the wrong slot constant (0x90 for 0x92)",
                        SLOT_DEFINE,
                        "ROM_SLOT_MC\t0x90U",
                        "fell back to rom_func_lookup",
                    ),
                    (
                        "the mutant that returns the slot halfword, not slot + 2",
                        SLOT_READ,
                        "(u_int)ROM_HALF(slot)",
                        "not the routine address",
                    ),
                ):
                    if original.count(old) != 1:
                        print(f"FAIL bcopy-overlap: {path}: {old} changed")
                        return 1
                    text = for_host(path, original.replace(old, new), {HOST_CASTS[0][0]})
                    mutants.append((label, text, (reason,), "table"))
            for compiler in compilers:
                for rom in ("table", "lookup") if rp else (None,):
                    result = run(compiler, directory, body, rom)
                    if result.returncode != 0:
                        print(
                            f"FAIL bcopy-overlap: {path} with {compiler}"
                            f"{f' ({rom} path)' if rom else ''}: {result.stdout.strip()}"
                        )
                        return 1
                for name, text, reasons, rom in mutants:
                    result = run(compiler, directory, text, rom)
                    if result.returncode == 0:
                        print(f"FAIL bcopy-overlap: {path} with {compiler}: {name} passed")
                        return 1
                    if not any(reason in result.stdout for reason in reasons):
                        print(
                            f"FAIL bcopy-overlap: {path} with {compiler}: {name} failed for "
                            f"another reason: {result.stdout.strip()[:120]}"
                        )
                        return 1
    print(
        f"PASS bcopy-overlap: {len(SOURCES)} machdep.c copies with "
        f"{' and '.join(compilers)}, slot and lookup ROM paths, four mutants rejected for "
        "their named reasons"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
