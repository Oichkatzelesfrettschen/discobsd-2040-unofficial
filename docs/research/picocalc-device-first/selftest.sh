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
# Calibrate build.sh, run.py and oracle.sh on known-bad input before their verdict
# counts: an OUTDIR that aliases the golden file, a harness bzero that drifted
# from machdep.c, a missing or incomplete production compile command, and a
# decoder one byte short, which run.py must report as a FAIL row through its
# exit status, and a golden file off by one cycle, which must fail the
# comparison. The known-good run must pass. Each known-bad case runs against a
# copy of this directory, so a defective gate cannot damage the tracked golden
# file.
#
# usage: PYTHON=python3 selftest.sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${DISCOBSD_ROOT:-$(cd "$HERE/../../.." && pwd)}
export DISCOBSD_ROOT="$ROOT"
T=$(mktemp -d)
trap 'rm -r -- "$T"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cp -R "$HERE" "$T/h"
fail=0

# expect_status WANT LABEL CMD...: run CMD and compare its exit status.
expect_status() {
    want=$1
    label=$2
    shift 2
    set +e
    "$@" >"$T/log" 2>&1
    got=$?
    set -e
    if [ "$got" -ne "$want" ]; then
        echo "selftest: $label: exit $got, want $want" >&2
        cat "$T/log" >&2
        fail=1
    else
        echo "selftest: $label: exit $got as required"
    fi
}

# expect_exit WANT TEXT LABEL CMD...: CMD must exit WANT and print TEXT, so a
# case cannot pass through an unrelated failure such as an unrunnable helper.
expect_exit() {
    want=$1
    text=$2
    label=$3
    shift 3
    set +e
    "$@" >"$T/log" 2>&1
    got=$?
    set -e
    if [ "$got" -ne "$want" ] || ! grep -qF -- "$text" "$T/log"; then
        echo "selftest: $label: exit $got, want $want with \"$text\"" >&2
        cat "$T/log" >&2
        fail=1
    else
        echo "selftest: $label: exit $want with \"$text\" as required"
    fi
}

# expect_diag TEXT LABEL CMD...: the exit-2 refusals of build.sh.
expect_diag() {
    text=$1
    label=$2
    shift 2
    expect_exit 2 "$text" "$label" "$@"
}

# The tracked golden file belongs to one build of sh and awk, which a hosted
# tree does not reproduce, so every case below runs against a golden file this
# tree generates: the baseline run uses the default toolchain, needs every
# row to round-trip, and its output replaces the golden file in the copy.
set +e
sh "$T/h/build.sh" "$T/base" >"$T/log" 2>&1
set -e
if [ ! -s "$T/base/run-output.tsv" ] ||
    [ "$(grep -c 'ok$' "$T/base/run-output.tsv")" -ne 6 ] ||
    grep -q 'FAIL' "$T/base/run-output.tsv"; then
    echo "selftest: the baseline run did not produce six ok rows" >&2
    cat "$T/log" >&2
    exit 1
fi
cp "$T/base/run-output.tsv" "$T/h/run-output.tsv"
golden=$(cksum <"$T/h/run-output.tsv")
echo "selftest: baseline run produced the golden file for the cases below"

# oracle.sh compares model output with the committed oracle. Each case derives
# an oracle from the baseline output, so no case reruns the model.
perturb() { # row column file
    awk -v row="$1" -v col="$2" 'BEGIN { FS = OFS = "\t" }
        $1 == row { $col = $col + 1 } { print }' "$3"
}
run_oracle() {
    sh "$T/h/oracle.sh" --compare "$T/base/run-output.tsv" "$T/oracle.tsv"
}
cp "$T/base/run-output.tsv" "$T/oracle.tsv"
expect_exit 0 "the model matches the oracle" "oracle: identical output" run_oracle
perturb 'random 4 KB' 5 "$T/base/run-output.tsv" >"$T/oracle.tsv"
expect_exit 1 "row 'random 4 KB' differs" "oracle: synthetic row off by one cycle" run_oracle
sed 's/^# compiler .*/# compiler other-gcc 1.0/' "$T/base/run-output.tsv" >"$T/oracle.tsv"
expect_exit 3 "SKIP: the oracle pins" "oracle: another compiler is a skip" run_oracle
sed 's/^# cflags .*/# cflags -O2/' "$T/base/run-output.tsv" >"$T/oracle.tsv"
expect_exit 1 "compiler options differs" "oracle: other compiler options" run_oracle
perturb 'sh data+bss' 4 "$T/base/run-output.tsv" >"$T/oracle.tsv"
expect_exit 1 "row 'sh data+bss' differs" "oracle: sh row off with equal inputs" run_oracle
perturb 'sh data+bss' 4 "$T/base/run-output.tsv" |
    sed 's/^# input sh sha256 .*/# input sh sha256 0/' >"$T/oracle.tsv"
expect_exit 0 "NOTE: the sh and awk rows were not compared" \
    "oracle: sh row off with other inputs" run_oracle

# A failed round trip fails the oracle even when the input hashes differ and the
# sh row is therefore not compared.
run_oracle_with() {
    sh "$T/h/oracle.sh" --compare "$1" "$T/oracle.tsv"
}
cp "$T/base/run-output.tsv" "$T/oracle.tsv"
awk 'BEGIN { FS = OFS = "\t" } $1 == "sh data+bss" { $NF = "FAIL" } { print }' \
    "$T/base/run-output.tsv" |
    sed 's/^# input sh sha256 .*/# input sh sha256 0/' >"$T/failnew.tsv"
expect_exit 1 "round trip failed for: sh data+bss" \
    "oracle: failed round trip with other inputs" run_oracle_with "$T/failnew.tsv"

# Known-bad: bench.c without the inherited notice of machdep.c.
grep -v 'Regents of the University of California' "$HERE/bench.c" >"$T/h/bench.c"
if cmp -s "$HERE/bench.c" "$T/h/bench.c"; then
    echo "selftest: the notice removal changed nothing" >&2
    fail=1
fi
expect_diag "lacks the inherited notice" "bench.c without the machdep.c notice" \
    sh "$T/h/build.sh" "$T/notice"
cp "$HERE/bench.c" "$T/h/bench.c"

# Known-bad: the golden file disagrees with the model output by one cycle.
awk 'BEGIN { FS = OFS = "\t" } $1 == "sh data+bss" { $4 = $4 + 1 } { print }' \
    "$T/base/run-output.tsv" >"$T/h/run-output.tsv"
if cmp -s "$T/base/run-output.tsv" "$T/h/run-output.tsv"; then
    echo "selftest: the golden perturbation changed nothing" >&2
    fail=1
fi
expect_exit 1 "model output differs from run-output.tsv" "perturbed golden file" \
    sh "$T/h/build.sh" "$T/perturbed"
cp "$T/base/run-output.tsv" "$T/h/run-output.tsv"

# Known-bad: OUTDIR is the source directory, and a symlink to it.
ln -s "$T/h" "$T/alias"
expect_diag "aliases the golden" "OUTDIR is the source directory" \
    sh "$T/h/build.sh" "$T/h"
expect_diag "aliases the golden" "OUTDIR symlinks to the source directory" \
    sh "$T/h/build.sh" "$T/alias"
# Known-bad: the output file is a link to the golden file.
mkdir "$T/linked"
ln -s "$T/h/run-output.tsv" "$T/linked/run-output.tsv"
expect_diag "aliases the golden" "run-output.tsv links to the golden file" \
    sh "$T/h/build.sh" "$T/linked"
if [ "$(cksum <"$T/h/run-output.tsv")" != "$golden" ]; then
    echo "selftest: an aliased OUTDIR changed the golden file" >&2
    fail=1
fi

# Known-bad: bench.c bzero loses its word loop.
sed 's/\*aligned_dst++ = 0;/*dst++ = 0;/' "$HERE/bench.c" >"$T/h/bench.c"
if cmp -s "$HERE/bench.c" "$T/h/bench.c"; then
    echo "selftest: the bzero mutation changed nothing" >&2
    fail=1
fi
expect_diag "bench.c bzero differs from machdep.c" "bzero differs from machdep.c" \
    sh "$T/h/build.sh" "$T/drift"
cp "$HERE/bench.c" "$T/h/bench.c"

# Known-bad: the production compile command is unavailable, or lacks the
# kernel's code-generation options.
# BMAKE also stands in for a tree whose kernel build left current objects: it
# reports "up to date" unless the makefile list forces the recipe (-f).
cat >"$T/bmake-fresh" <<'EOF'
#!/bin/sh
case " $* " in *" -f "*) exec bmake "$@" ;; esac
for last; do :; done
echo "$last is up to date."
EOF
chmod 755 "$T/bmake-fresh"
printf '#!/bin/sh\nexit 0\n' >"$T/bmake-empty"
printf '#!/bin/sh\nexec "%s" "$@" | sed "s/ -fshort-enums//"\n' "$T/bmake-fresh" >"$T/bmake-nofl"
chmod 755 "$T/bmake-empty" "$T/bmake-nofl"
# The built-tree condition: a kernel build leaves current objects, and an
# unforced dry run then prints only "up to date". bmake-fresh reproduces it, so
# the unforced call below must print no compile line, and the forced call the
# stripping helper makes must print one without -fshort-enums. A helper or
# check that depended on the condition would otherwise pass only on trees whose
# objects are stale, as hosted CI's built tree showed.
picodir=$ROOT/sys/arch/rp2040/compile/PICO
(cd "$picodir" && "$T/bmake-nofl" MACHINE=rp2040 -n heatshrink_encoder.o) \
    >"$T/unforced.out" 2>&1 || true
(cd "$picodir" && "$T/bmake-nofl" MACHINE=rp2040 -n -f Makefile \
    -f "$T/base/force-heatshrink_encoder.mk" heatshrink_encoder.o) \
    >"$T/helper.out" 2>&1 || true
grep -- ' -c ' "$T/unforced.out" >"$T/unforced.cc" || true
grep -- ' -c ' "$T/helper.out" >"$T/helper.cc" || true
if [ -s "$T/unforced.cc" ]; then
    echo "selftest: the unforced dry run printed a compile line, so the current-objects shim does not reproduce a built tree:" >&2
    cut -c1-200 "$T/unforced.out" >&2
    fail=1
fi
if [ ! -s "$T/helper.cc" ] ||
    ! grep -q -- '-fno-strict-aliasing' "$T/helper.cc" ||
    grep -q -- '-fshort-enums' "$T/helper.cc"; then
    echo "selftest: the stripping helper does not run as intended" >&2
    echo "  expected: one compile line holding -fno-strict-aliasing and no -fshort-enums" >&2
    echo "  actual output (first 6 lines, 200 columns):" >&2
    head -n 6 "$T/helper.out" | cut -c1-200 >&2
    fail=1
fi
expect_diag "no usable compile command" "no compile command from bmake" \
    env BMAKE="$T/bmake-empty" sh "$T/h/build.sh" "$T/noflags"
expect_diag "lack the kernel's code-generation flags" \
    "derived options lack -fshort-enums" \
    env BMAKE="$T/bmake-nofl" sh "$T/h/build.sh" "$T/nofl"

# Known-bad: a decoder one byte short; run.py must exit nonzero.
# The case also passes a relative OUTDIR, which build.sh must resolve before it
# changes into the PICO directory.
set +e
(cd "$T" && BENCH_CFLAGS=-DBENCH_MUTANT_SHORT_DECODE sh h/build.sh mut) \
    >"$T/log" 2>&1
set -e
if ! grep -q 'run.py failed' "$T/log" || ! grep -q 'FAIL' "$T/mut/run-output.tsv"; then
    echo "selftest: the short decoder did not fail through run.py" >&2
    cat "$T/log" >&2
    fail=1
else
    echo "selftest: short decoder: run.py reported FAIL as required"
fi
set +e
"$PYTHON" -I "$T/h/run.py" "$T/mut/bench.elf" \
    "${SH_AOUT:-$ROOT/bin/sh/sh}" "${AWK_AOUT:-$ROOT/usr.bin/awk/awk}" \
    >/dev/null 2>&1
got=$?
set -e
if [ "$got" -eq 0 ]; then
    echo "selftest: run.py exited 0 on a FAIL row" >&2
    fail=1
fi

# Known-bad: CC_ARM names a compiler whose prefix has no matching nm or objcopy.
mkdir "$T/tc-bad"
printf '#!/bin/sh\nexec arm-none-eabi-gcc "$@"\n' >"$T/tc-bad/zz-gcc"
chmod 755 "$T/tc-bad/zz-gcc"
expect_diag "binutils tool not found" "CC_ARM prefix without matching binutils" \
    env CC_ARM="$T/tc-bad/zz-gcc" sh "$T/h/build.sh" "$T/tcbad"

# Known-good: a compiler under another prefix and directory, with matching
# binutils that record their calls; run.py must use them, not the PATH tools.
mkdir "$T/tc"
: >"$T/tc.log"
for tool in gcc nm objcopy; do
    printf '#!/bin/sh\necho %s >>"%s"\nexec arm-none-eabi-%s "$@"\n' \
        "$tool" "$T/tc.log" "$tool" >"$T/tc/zz-$tool"
    chmod 755 "$T/tc/zz-$tool"
done
expect_status 0 "known-good run with a prefixed toolchain and current objects" \
    env CC_ARM="$T/tc/zz-gcc" BMAKE="$T/bmake-fresh" sh "$T/h/build.sh" "$T/good"
for tool in nm objcopy; do
    if ! grep -qx "$tool" "$T/tc.log"; then
        echo "selftest: run.py did not call the prefixed $tool" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "selftest: FAILED" >&2
    exit 1
fi
echo "selftest: all cases behaved as required"
