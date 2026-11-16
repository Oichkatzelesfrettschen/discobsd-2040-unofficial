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
# Compare the cycle model with the committed oracle run-output.tsv.
#
# usage: PYTHON=python3 oracle.sh [--probe | --compare NEW [ORACLE]]
#
# A row whose round trip did not reproduce its input fails whatever the oracle holds.
# The committed file is the fixed oracle of the cycle-model measurements in
# docs/research/picocalc-device-first-memory.md. Cycle counts belong to one
# compiler, so an oracle recorded by another compiler version gives exit 3 with
# the reason, which the caller reports as a skip, and the oracle is never
# regenerated here. With the pinned compiler the script runs build.sh into a
# scratch directory and compares: the "# cflags" line and the four synthetic
# rows (stack, u area, zeros, random) must be identical, which any change of
# codec source, compiler options or cycle accounting breaks. The sh and awk
# rows depend on the a.outs of the tree under test, so they are compared when
# both input hashes match the oracle's and otherwise reported as not compared.
# --probe stops after the compiler check; --compare NEW checks an existing
# output file against ORACLE (default: the committed file) without running the
# model, which selftest.sh uses to calibrate the comparison.
#
# exit status: 0 matches, 1 drifted, 2 unusable input, 3 skipped (compiler).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
ORACLE=$HERE/run-output.tsv
mode=run
new=
case "${1:-}" in
'') ;;
--probe) mode=probe ;;
--compare)
    mode=compare
    new=${2:?usage: oracle.sh --compare NEW [ORACLE]}
    ORACLE=${3:-$ORACLE}
    ;;
*)
    echo "usage: oracle.sh [--probe | --compare NEW [ORACLE]]" >&2
    exit 2
    ;;
esac
[ -s "$ORACLE" ] || {
    echo "oracle.sh: no oracle at $ORACLE" >&2
    exit 2
}

# field FILE PREFIX prints the first line of FILE that starts with PREFIX.
field() {
    grep -m 1 -- "^$2" "$1" || true
}

pinned=$(field "$ORACLE" '# compiler ')
if [ -z "$pinned" ]; then
    echo "oracle.sh: the oracle names no compiler" >&2
    exit 2
fi
if [ "$mode" = compare ]; then
    have=$(field "$new" '# compiler ')
else
    have="# compiler $("$CC_ARM" --version | head -n 1)"
fi
if [ "$have" != "$pinned" ]; then
    echo "SKIP: the oracle pins '${pinned#\# compiler }' and this run has '${have#\# compiler }'; cycle counts belong to one compiler, so nothing is compared" >&2
    exit 3
fi
[ "$mode" = probe ] && exit 0

if [ "$mode" = run ]; then
    : "${PYTHON:?set PYTHON to the intended interpreter}"
    T=$(mktemp -d)
    trap 'rm -r -- "$T"' EXIT INT TERM
    set +e
    sh "$HERE/build.sh" "$T/out" >"$T/log" 2>&1
    status=$?
    set -e
    if [ "$status" -gt 1 ] || [ ! -s "$T/out/run-output.tsv" ]; then
        echo "oracle.sh: build.sh exited $status without output" >&2
        cat "$T/log" >&2
        exit 2
    fi
    new=$T/out/run-output.tsv
fi

drift=0
# A failed round trip fails the gate whether or not its cycle counts compare.
bad=$(grep -v '^#' "$new" | grep -v '^segment' | awk -F'\t' '$NF != "ok" { print $1 }' || true)
if [ -n "$bad" ]; then
    echo "oracle.sh: round trip failed for: $bad" >&2
    drift=1
fi
check_line() { # label old new
    if [ "$2" != "$3" ]; then
        echo "oracle.sh: $1 differs from the oracle" >&2
        printf '  oracle: %s\n  model:  %s\n' "$2" "$3" >&2
        drift=1
    fi
}
check_line "compiler options" "$(field "$ORACLE" '# cflags ')" "$(field "$new" '# cflags ')"
for row in 'stack 4 KB synthetic' 'u area 3 KB synthetic' 'zeros 4 KB' 'random 4 KB'; do
    old=$(grep -m 1 -F -- "$row	" "$ORACLE" || true)
    cur=$(grep -m 1 -F -- "$row	" "$new" || true)
    if [ -z "$old" ]; then
        echo "oracle.sh: the oracle lacks the row '$row'" >&2
        exit 2
    fi
    check_line "row '$row'" "$old" "$cur"
done
if [ "$(field "$ORACLE" '# input sh ')" = "$(field "$new" '# input sh ')" ] &&
    [ "$(field "$ORACLE" '# input awk ')" = "$(field "$new" '# input awk ')" ]; then
    for row in 'sh data+bss' 'awk data+bss'; do
        check_line "row '$row'" "$(grep -m 1 -F -- "$row	" "$ORACLE" || true)" \
            "$(grep -m 1 -F -- "$row	" "$new" || true)"
    done
else
    echo "NOTE: the sh and awk rows were not compared: the input a.outs differ from the oracle's"
fi
if [ "$drift" -ne 0 ]; then
    exit 1
fi
echo "oracle.sh: the model matches the oracle"
