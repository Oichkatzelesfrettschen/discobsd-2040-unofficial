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
# Build rules.c with the PICO kernel's options and run the admission-rule
# model of docs/research/swap-admission-early-stop.md.
#
# usage: PYTHON=python3 build.sh OUTDIR
#
# rules.c and the encoder (sys/arch/rp2040/heatshrink) compile with the
# options "bmake -n" prints for the encoder's rule in
# sys/arch/rp2040/compile/PICO, the way
# docs/research/picocalc-device-first/build.sh derives them (pull request
# #644), so cycle counts and sizes are those of kernel objects. bcopy and
# bzero come from sys/arch/rp2040/rp2040/machdep.c at build time, with the
# notice that file carries, so the encoder's copies run the kernel's word
# loops. sizes.tsv lists the text of each rule function from "nm -S",
# once with both exits of enc_stop and once with each alone. run.py writes
# run-output.tsv and corpus.tsv; the corpus is the data+bss segment of
# every OMAGIC a.out a "bmake MACHINE=rp2040 build" left under bin, sbin,
# usr.bin, usr.sbin, libexec and games. The script exits 1 when any of the
# three files differs from the committed copy beside it, which includes a
# different compiler or different input binaries (their hashes head the
# files), and 2 on a setup error. BENCH_CFLAGS adds compiler options
# (selftest.sh passes the known-bad -DRULES_MUTANT_NO_STOP).
# Needs arm-none-eabi-gcc, bmake (BMAKE names another), Unicorn and Capstone.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
OUT=${1:?usage: build.sh OUTDIR}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${DISCOBSD_ROOT:-$(cd "$HERE/../../.." && pwd)}
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
case "$CC_ARM" in
*gcc) : "${NM_ARM:=${CC_ARM%gcc}nm}" "${OBJCOPY_ARM:=${CC_ARM%gcc}objcopy}" ;;
esac
: "${NM_ARM:?CC_ARM does not end in gcc; set NM_ARM}"
: "${OBJCOPY_ARM:?CC_ARM does not end in gcc; set OBJCOPY_ARM}"
export NM_ARM OBJCOPY_ARM
for tool in "$CC_ARM" "$NM_ARM" "$OBJCOPY_ARM"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build.sh: tool not found: $tool" >&2
        exit 2
    fi
done
SH_AOUT=${SH_AOUT:-$ROOT/bin/sh/sh}
AWK_AOUT=${AWK_AOUT:-$ROOT/usr.bin/awk/awk}
for f in "$SH_AOUT" "$AWK_AOUT"; do
    if [ ! -f "$f" ]; then
        echo "build.sh: $f is missing; run bmake MACHINE=rp2040 build" >&2
        exit 2
    fi
done

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
if [ "$(cd "$OUT" && pwd -P)" = "$(cd "$HERE" && pwd -P)" ]; then
    echo "build.sh: OUTDIR $OUT is the directory of the committed files" >&2
    exit 2
fi
mkdir -p "$OUT/inc"
ln -sfn "$ROOT/sys/arch/rp2040/include" "$OUT/inc/machine"

# prod_flags NAME prints the compiler options of the PICO kernel's NAME.o
# rule without the compiler path, the dependency-file options, the debug
# path map and the -c operand; a phony prerequisite keeps "bmake -n" from
# answering that a current object is up to date.
PICO=$ROOT/sys/arch/rp2040/compile/PICO
prod_flags() {
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
FLAGS=$(prod_flags heatshrink_encoder)
case "$FLAGS" in
'' | *\\* | *\"* | *\'*)
    echo "build.sh: no usable compile command from bmake -n in $PICO" >&2
    exit 2
    ;;
esac
case "$FLAGS" in
*-Os*-fshort-enums* | *-fshort-enums*-Os*) ;;
*)
    echo "build.sh: derived options lack the kernel's -Os and -fshort-enums" >&2
    exit 2
    ;;
esac

# fn_text FILE NAME prints function NAME of FILE from its name line to the
# closing brace in column one.
fn_text() {
    awk -v n="$2" '$0 ~ "^" n "\\(" { p = 1 } p { print } p && /^}/ { exit }' "$1"
}
MD=$ROOT/sys/arch/rp2040/rp2040/machdep.c
{
    sed -n 1,7p "$MD"
    printf '#include <sys/types.h>\n'
    grep '^#define UNALIGNED' "$MD"
    printf 'void bcopy(const void *, void *, size_t);\n'
    printf 'void bzero(void *, size_t);\n'
    printf 'void *memset(void *, int, size_t);\n'
    printf 'void *memcpy(void *, const void *, size_t);\n'
    printf 'void *memmove(void *, const void *, size_t);\n'
    printf 'void\n'
    fn_text "$MD" bcopy
    printf 'void\n'
    fn_text "$MD" bzero
    cat <<'EOF'
void *memset(void *p, int c, size_t n)
{ unsigned char *q = p; while (n--) *q++ = (unsigned char)c; return p; }
void *memcpy(void *d, const void *s, size_t n)
{ bcopy(s, d, n); return d; }
void *memmove(void *d, const void *s, size_t n)
{ unsigned char *a = d; const unsigned char *b = s;
  if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
  return d; }
EOF
} >"$OUT/kcopy.c"
if ! grep -q '^bcopy(' "$OUT/kcopy.c" || ! grep -q '^bzero(' "$OUT/kcopy.c"; then
    echo "build.sh: bcopy or bzero not found in machdep.c" >&2
    exit 2
fi

set -f
# shellcheck disable=SC2086
compile() {
    (cd "$PICO" && "$CC_ARM" $FLAGS ${BENCH_CFLAGS:-} \
        -I"$OUT/inc" "$@")
}
compile -c "$HERE/rules.c" -o "$OUT/rules.o"
compile -c "$ROOT/sys/arch/rp2040/heatshrink/heatshrink_encoder.c" \
    -o "$OUT/heatshrink_encoder.o"
compile -c "$OUT/kcopy.c" -o "$OUT/kcopy.o"
compile -DRULES_NO_RATIO -c "$HERE/rules.c" -o "$OUT/rules-budget.o"
compile -DRULES_NO_BUDGET -c "$HERE/rules.c" -o "$OUT/rules-ratio.o"
compile -DRULES_NO_RATIO -DRULES_NO_BUDGET -c "$HERE/rules.c" -o "$OUT/rules-none.o"
set +f
"$CC_ARM" -mcpu=cortex-m0plus -mthumb -nostdlib -nostartfiles \
    -Wl,--no-warn-rwx-segments \
    -Wl,-T,"$ROOT/docs/research/rp2040-second-core-emulation/harness.ld" \
    -o "$OUT/rules.elf" "$OUT/rules.o" "$OUT/heatshrink_encoder.o" \
    "$OUT/kcopy.o" -lgcc

# sym_size OBJ NAME prints the decimal size nm -S gives NAME in OBJ, or 0.
sym_size() {
    "$NM_ARM" -S -t d "$1" | awk -v n="$2" '$NF == n { print $2 + 0; f = 1 } END { if (!f) print 0 }'
}
{
    printf '# compiler %s\n' "$("$CC_ARM" --version | head -n 1)"
    printf 'object\tfunction\ttext_bytes\n'
    for fn in enc enc_stop enc_prefix entropy_q4 log2_q4 log2frac same_filled largest_run; do
        printf 'rules.o\t%s\t%s\n' "$fn" "$(sym_size "$OUT/rules.o" "$fn")"
    done
    for v in budget ratio none; do
        printf 'rules-%s.o\tenc_stop\t%s\n' "$v" "$(sym_size "$OUT/rules-$v.o" enc_stop)"
    done
} >"$OUT/sizes.tsv"

# The corpus: every OMAGIC (0407) a.out under the program trees, by
# repository-relative path.
(cd "$ROOT" && find bin sbin usr.bin usr.sbin libexec games -type f 2>/dev/null |
    sort | while IFS= read -r f; do
        if [ "$(od -An -tx4 -N4 "$f" 2>/dev/null | tr -d ' ')" = 00000107 ]; then
            printf '%s\n' "$f"
        fi
    done) >"$OUT/corpus.list"

if ! (cd "$ROOT" && "$PYTHON" -I "$HERE/run.py" "$OUT/rules.elf" "$SH_AOUT" \
    "$AWK_AOUT" "$OUT/corpus.list" "$OUT"); then
    echo "build.sh: run.py failed" >&2
    exit 1
fi
{
    printf '# compiler %s\n' "$("$CC_ARM" --version | head -n 1)"
    cat "$OUT/run-output.tsv"
} >"$OUT/run-output.tsv.tmp"
mv "$OUT/run-output.tsv.tmp" "$OUT/run-output.tsv"
rc=0
for f in sizes.tsv run-output.tsv corpus.tsv; do
    if ! diff "$HERE/$f" "$OUT/$f" >/dev/null 2>&1; then
        echo "build.sh: $f differs from the committed copy" >&2
        rc=1
    fi
done
[ "$rc" -eq 0 ] && echo "build.sh: sizes, model and corpus match the committed files"
exit "$rc"
