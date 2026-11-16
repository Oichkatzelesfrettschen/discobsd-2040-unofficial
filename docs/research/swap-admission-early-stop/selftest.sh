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
# Calibrate build.sh on a known-good and a known-bad input.
#
# usage: PYTHON=python3 selftest.sh SCRATCH
#
# The known-good run builds rules.c as committed and must exit 0. The
# known-bad run builds it with -DRULES_MUTANT_NO_STOP, which removes both
# early exits of enc_stop(), and must be rejected by the model itself: build.sh
# exits 1 and its log, kept as SCRATCH/bad.log, carries run.py's report that
# the 1 KB budget did not stop the random input early. Any other failure, a
# compile error among them, fails the self-test.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
SCRATCH=${1:?usage: selftest.sh SCRATCH}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$SCRATCH"

if ! sh "$HERE/build.sh" "$SCRATCH/good"; then
    echo "selftest: known-good build failed" >&2
    exit 1
fi
rc=0
BENCH_CFLAGS=-DRULES_MUTANT_NO_STOP sh "$HERE/build.sh" "$SCRATCH/bad" \
    >"$SCRATCH/bad.log" 2>&1 || rc=$?
if [ "$rc" -ne 1 ] || ! grep -q 'did not stop early' "$SCRATCH/bad.log"; then
    echo "selftest: the model did not reject the known-bad build (exit $rc; $SCRATCH/bad.log)" >&2
    exit 1
fi
echo "selftest: known-good passes, known-bad fails"
