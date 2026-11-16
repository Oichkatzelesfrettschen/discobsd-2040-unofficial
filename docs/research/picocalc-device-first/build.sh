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
# compile with the kernel's -Os and include path; harness.ld from
# rp2040-second-core-emulation links them at 0x20000000. run.py reads the
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
# trip. BENCH_CFLAGS adds compiler flags (selftest.sh uses it for a mutant).
# Needs arm-none-eabi-gcc, Unicorn and Capstone.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
OUT=${1:?usage: build.sh OUTDIR}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${DISCOBSD_ROOT:-$(cd "$HERE/../../.." && pwd)}
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
SH_AOUT=${SH_AOUT:-$ROOT/bin/sh/sh}
AWK_AOUT=${AWK_AOUT:-$ROOT/usr.bin/awk/awk}
HS=$ROOT/sys/arch/rp2040/heatshrink

mkdir -p "$OUT"
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
if [ "$(grep '^#define UNALIGNED' "$ROOT/sys/arch/rp2040/rp2040/machdep.c")" != \
    "$(grep '^#define UNALIGNED' "$HERE/bench.c")" ]; then
    echo "build.sh: bench.c UNALIGNED differs from machdep.c" >&2
    exit 2
fi

mkdir -p "$OUT/inc"
ln -sfn "$ROOT/sys/arch/rp2040/include" "$OUT/inc/machine"
KFLAGS="-DKERNEL -mcpu=cortex-m0plus -mthumb -ffreestanding -fno-builtin \
-fno-common -std=gnu17 -Os -nostdinc"
KINC="-I$ROOT/sys -I$ROOT/sys/arch -I$OUT/inc \
-I$ROOT/sys/arch/rp2040/dhara/compat -I$HS -I$HS/compat"
for src in "$HERE/bench.c" "$HS/heatshrink_encoder.c" \
    "$ROOT/sys/arch/rp2040/rp2040/hsx_decoder.c"; do
    # shellcheck disable=SC2086
    "$CC_ARM" $KFLAGS ${BENCH_CFLAGS:-} $KINC -Wno-implicit-fallthrough -c "$src" \
        -o "$OUT/$(basename "$src" .c).o"
done
"$CC_ARM" -mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles \
    -Wl,--no-warn-rwx-segments \
    -Wl,-T,"$ROOT/docs/research/rp2040-second-core-emulation/harness.ld" \
    -o "$OUT/bench.elf" "$OUT/bench.o" "$OUT/heatshrink_encoder.o" \
    "$OUT/hsx_decoder.o" -lgcc
if ! {
    printf '# compiler %s\n' "$("$CC_ARM" --version | head -n 1)"
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
