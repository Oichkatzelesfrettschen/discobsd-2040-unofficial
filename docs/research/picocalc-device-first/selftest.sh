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
# Calibrate build.sh and run.py on known-bad input before their verdict
# counts: an OUTDIR that aliases the golden file, a harness bzero that drifted
# from machdep.c, a missing or incomplete production compile command, and a
# decoder one byte short, which run.py must report as a FAIL row through its
# exit status. The known-good run must pass. Each known-bad case runs against a
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
trap 'rm -r -- "$T"' EXIT INT TERM
cp -R "$HERE" "$T/h"
golden=$(cksum <"$HERE/run-output.tsv")
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

# Known-bad: OUTDIR is the source directory, and a symlink to it.
ln -s "$T/h" "$T/alias"
expect_status 2 "OUTDIR is the source directory" sh "$T/h/build.sh" "$T/h"
expect_status 2 "OUTDIR symlinks to the source directory" \
    sh "$T/h/build.sh" "$T/alias"
# Known-bad: the output file is a link to the golden file.
mkdir "$T/linked"
ln -s "$T/h/run-output.tsv" "$T/linked/run-output.tsv"
expect_status 2 "run-output.tsv links to the golden file" \
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
expect_status 2 "bzero differs from machdep.c" sh "$T/h/build.sh" "$T/drift"
cp "$HERE/bench.c" "$T/h/bench.c"

# Known-bad: the production compile command is unavailable, or lacks the
# kernel's code-generation options.
printf '#!/bin/sh\nexit 0\n' >"$T/bmake-empty"
printf '#!/bin/sh\nexec bmake "$@" | sed "s/ -fshort-enums//"\n' >"$T/bmake-nofl"
expect_status 2 "no compile command from bmake" \
    env BMAKE="$T/bmake-empty" sh "$T/h/build.sh" "$T/noflags"
expect_status 2 "derived options lack -fshort-enums" \
    env BMAKE="$T/bmake-nofl" sh "$T/h/build.sh" "$T/nofl"

# Known-bad: a decoder one byte short; run.py must exit nonzero.
set +e
BENCH_CFLAGS=-DBENCH_MUTANT_SHORT_DECODE sh "$T/h/build.sh" "$T/mut" \
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

# Known-good: the unmodified harness matches its golden file.
expect_status 0 "known-good run" sh "$T/h/build.sh" "$T/good"

if [ "$fail" -ne 0 ]; then
    echo "selftest: FAILED" >&2
    exit 1
fi
echo "selftest: all cases behaved as required"
