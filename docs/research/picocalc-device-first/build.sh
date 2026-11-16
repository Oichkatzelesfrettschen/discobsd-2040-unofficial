#!/bin/sh
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
# Build the Cortex-M0+ swap codec workload and run the cycle model.
#
# usage: PYTHON=python3 build.sh OUTDIR
#
# The encoder (sys/arch/rp2040/heatshrink) and decoder (rp2040/hsx_decoder.c)
# compile with the options and include path that "bmake -n" prints for their
# rules in sys/arch/rp2040/compile/PICO (COMPILE_C), so the cycle counts are
# those of the shipped objects; the golden file records them on its "# cflags"
# line; bench.c compiles with them too, warnings as errors included.
# harness.ld from rp2040-second-core-emulation links the objects at
# 0x20000000. run.py reads the
# data segments of bin/sh/sh and usr.bin/awk/awk, which a
# "bmake MACHINE=rp2040 build" leaves in the tree (SH_AOUT and AWK_AOUT name
# other copies), and prints one TSV row per
# segment. The script compares that output with run-output.tsv and exits
# nonzero on any difference, which includes different input binaries: their
# SHA-256 heads the output. It refuses an OUTDIR that is, or whose
# run-output.tsv is, the source directory's golden file, because the redirect
# would truncate the oracle and the comparison would read it against itself.
# It also exits nonzero when bench.c's bzero, bcopy or UNALIGNED differ from
# sys/arch/rp2040/rp2040/machdep.c, or when run.py reports a failed round
# trip. NM_ARM and OBJCOPY_ARM default to CC_ARM with "gcc" replaced by "nm" and
# "objcopy". BENCH_CFLAGS adds compiler flags (selftest.sh uses it for a mutant).
# Needs arm-none-eabi-gcc, bmake (BMAKE names another), Unicorn and Capstone.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
OUT=${1:?usage: build.sh OUTDIR}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${DISCOBSD_ROOT:-$(cd "$HERE/../../.." && pwd)}
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
# run.py reads the ELF through binutils of the compiler's prefix, so a CC_ARM
# with another prefix or directory takes its nm and objcopy; NM_ARM and
# OBJCOPY_ARM override that when CC_ARM does not end in "gcc".
case "$CC_ARM" in
*gcc) : "${NM_ARM:=${CC_ARM%gcc}nm}" "${OBJCOPY_ARM:=${CC_ARM%gcc}objcopy}" ;;
esac
: "${NM_ARM:?CC_ARM does not end in gcc; set NM_ARM}"
: "${OBJCOPY_ARM:?CC_ARM does not end in gcc; set OBJCOPY_ARM}"
export NM_ARM OBJCOPY_ARM
for tool in "$CC_ARM" "$NM_ARM" "$OBJCOPY_ARM"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build.sh: binutils tool not found: $tool" >&2
        exit 2
    fi
done
SH_AOUT=${SH_AOUT:-$ROOT/bin/sh/sh}
AWK_AOUT=${AWK_AOUT:-$ROOT/usr.bin/awk/awk}
HS=$ROOT/sys/arch/rp2040/heatshrink

mkdir -p "$OUT"
# Later steps change into the PICO directory, so OUTDIR must be absolute.
OUT=$(cd "$OUT" && pwd)
if [ "$(cd "$OUT" && pwd -P)" = "$(cd "$HERE" && pwd -P)" ] ||
    { [ -e "$OUT/run-output.tsv" ] && [ "$OUT/run-output.tsv" -ef "$HERE/run-output.tsv" ]; }; then
    echo "build.sh: OUTDIR $OUT aliases the golden run-output.tsv" >&2
    exit 2
fi

# fn_text FILE NAME prints the function NAME of FILE from its name line to
# the closing brace in column one.
fn_text() {
    awk -v n="$2" '$0 ~ "^" n "\\(" { p = 1 } p { print } p && /^}/ { exit }' "$1"
}
for fn in bzero bcopy; do
    k=$(fn_text "$ROOT/sys/arch/rp2040/rp2040/machdep.c" "$fn")
    h=$(fn_text "$HERE/bench.c" "$fn")
    if [ -z "$k" ] || [ "$k" != "$h" ]; then
        echo "build.sh: bench.c $fn differs from machdep.c" >&2
        exit 2
    fi
done
# The copied routines keep the Regents notice and version banner of machdep.c.
inherited=$(sed -n 1,7p "$ROOT/sys/arch/rp2040/rp2040/machdep.c")
case "$(cat "$HERE/bench.c")" in
*"$inherited"*) ;;
*)
    echo "build.sh: bench.c lacks the inherited notice of machdep.c lines 1-7" >&2
    exit 2
    ;;
esac
if [ "$(grep '^#define UNALIGNED' "$ROOT/sys/arch/rp2040/rp2040/machdep.c")" != \
    "$(grep '^#define UNALIGNED' "$HERE/bench.c")" ]; then
    echo "build.sh: bench.c UNALIGNED differs from machdep.c" >&2
    exit 2
fi

mkdir -p "$OUT/inc"
ln -sfn "$ROOT/sys/arch/rp2040/include" "$OUT/inc/machine"

# prod_flags NAME prints the compiler options of the PICO kernel's NAME.o rule
# as "bmake -n" expands COMPILE_C in sys/arch/rp2040/compile/PICO (forced, so a
# current object still prints its recipe), without the
# compiler path, the dependency-file options, the debug path map (a local
# path that changes no instruction) and the -c operand.
PICO=$ROOT/sys/arch/rp2040/compile/PICO
prod_flags() {
    # A kernel build that left a current NAME.o makes "bmake -n NAME.o" print
    # only that the target is up to date, so a small makefile gives the object
    # an always-run phony prerequisite, which puts the object out of date and
    # keeps the recipe's source operand.
    printf '%s.o: force-%s\n.PHONY: force-%s\nforce-%s:\n' "$1" "$1" "$1" "$1" \
        >"$OUT/force-$1.mk"
    (cd "$PICO" && "${BMAKE:-bmake}" MACHINE=rp2040 -n -f Makefile \
        -f "$OUT/force-$1.mk" "$1.o") |
        awk -v s="/$1.c" 'index($0, " -c ") && substr($0, length($0) - length(s) + 1) == s' |
        tail -n 1 |
        sed -e 's/^[^ ]*gcc //' -e 's/ -MT [^ ]*//' -e 's/ -MF [^ ]*//' \
            -e 's/ -MP//' -e 's/ -MD//' -e 's/ -fdebug-prefix-map=[^ ]*//' \
            -e 's/ -c [^ ]*$//'
}
PROD_FLAGS=$(prod_flags heatshrink_encoder)
PROD_DEC=$(prod_flags hsx_decoder)
case "$PROD_FLAGS" in
'' | *\\* | *\"* | *\'*)
    echo "build.sh: no usable compile command from bmake -n in $PICO" >&2
    exit 2
    ;;
esac
if [ "$PROD_FLAGS" != "$PROD_DEC" ]; then
    echo "build.sh: the encoder and decoder compile with different options" >&2
    exit 2
fi
case "$PROD_FLAGS" in
*-fno-strict-aliasing*-fshort-enums* | *-fshort-enums*-fno-strict-aliasing*) ;;
*)
    echo "build.sh: derived options lack the kernel's code-generation flags" >&2
    exit 2
    ;;
esac
set -f
for src in "$HERE/bench.c" "$HS/heatshrink_encoder.c" \
    "$ROOT/sys/arch/rp2040/rp2040/hsx_decoder.c"; do
    # shellcheck disable=SC2086
    (cd "$PICO" && "$CC_ARM" $PROD_FLAGS ${BENCH_CFLAGS:-} -I"$OUT/inc" -c "$src" \
        -o "$OUT/$(basename "$src" .c).o")
done
set +f
"$CC_ARM" -mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles \
    -Wl,--no-warn-rwx-segments \
    -Wl,-T,"$ROOT/docs/research/rp2040-second-core-emulation/harness.ld" \
    -o "$OUT/bench.elf" "$OUT/bench.o" "$OUT/heatshrink_encoder.o" \
    "$OUT/hsx_decoder.o" -lgcc
if ! {
    printf '# compiler %s\n' "$("$CC_ARM" --version | head -n 1)"
    printf '# cflags %s\n' "$PROD_FLAGS"
    "$PYTHON" -I "$HERE/run.py" "$OUT/bench.elf" "$SH_AOUT" "$AWK_AOUT"
} >"$OUT/run-output.tsv"; then
    echo "build.sh: run.py failed (see $OUT/run-output.tsv)" >&2
    exit 1
fi
if diff "$HERE/run-output.tsv" "$OUT/run-output.tsv"; then
    echo "build.sh: model output matches run-output.tsv"
else
    echo "build.sh: model output differs from run-output.tsv" >&2
    exit 1
fi
