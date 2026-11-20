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

# Calibrate the bounds of mutant_unit.sh on a fixture of one function. A
# mutant run that executes a freshly built test has three ends: the test
# fails (the mutant is rejected), the test passes (the mutant survived), or
# the test never ends or never stops printing (no verdict). The cases below
# hold the script to one status for each:
#
#   rejected    exit 0
#   survived    exit 1, "passed the mutant"
#   unchanged   exit 1, "changed nothing"
#   looping     exit 3 within the time bound, "gave no verdict"
#   flooding    a mutant that prints without end: exit 3 within the time
#               bound, "gave no verdict", with the output kept to its bound
#   loud        a mutant that fails after printing more than the output
#               bound: exit 0, "mutant rejected", not a no-verdict
#
# A copy of mutant_unit.sh that runs the test directly must not give the
# looping case its exit 3, which shows the case depends on the bound.
#
# usage: mutant_bounds_test.sh CC
set -eu

cc=${1:?usage: mutant_bounds_test.sh CC}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mutant-bounds.XXXXXX")
cleanup() {
	trap - EXIT HUP INT TERM
	find "$work" -depth \( -type f -o -type l \) -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir "$work/dev" "$work/copy"
cat >"$work/dev/limit.c" <<'FIXTURE'
#include <stdio.h>
int limit(void) { return 1; }
FIXTURE
cat >"$work/test.c" <<'FIXTURE'
int limit(void);
int main(void) { return limit() == 1 ? 0 : 1; }
FIXTURE

# case SCRIPT NAME WANT-STATUS WANT-TEXT SED-EXPRESSION
case_run() {
	script=$1
	name=$2
	want=$3
	text=$4
	expr=$5
	rc=0
	MUTANT_TIMEOUT=2 MUTANT_OUTPUT_BYTES=4096 \
	    sh "$script" "$cc" -O0 "$work/dev" "$work/test.c" limit.c "$expr" limit.c \
	    >"$work/log" 2>&1 || rc=$?
	if [ "$rc" != "$want" ] || ! grep -F -q -- "$text" "$work/log"; then
		echo "mutant_bounds_test: case $name: status $rc (want $want), log:" >&2
		cat "$work/log" >&2
		return 1
	fi
	return 0
}

cases() {
	script=$1
	status=0
	case_run "$script" rejected 0 'mutant rejected' 's/return 1;/return 2;/' || status=1
	case_run "$script" survived 1 'passed the mutant' 's/int limit/static int unused; int limit/' || status=1
	case_run "$script" unchanged 1 'changed nothing' 's/return 9;/return 9;/' || status=1
	case_run "$script" looping 3 'gave no verdict' 's/return 1;/for (;;) { } return 1;/' || status=1
	case_run "$script" flooding 3 'gave no verdict' 's/return 1;/for (;;) { putchar(65); } return 1;/' || status=1
	case_run "$script" loud 0 'mutant rejected' 's/return 1;/{ int i; for (i = 0; i < 20000; i++) puts("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"); return 2; }/' || status=1
	return "$status"
}

cases "$here/mutant_unit.sh" || exit 1
echo "mutant_bounds_test: 6 cases pass"

# Known-bad copy: the test runs directly, without the bounds.
sed -e 's#^sh "\$here/\.\./\.\./tools/run_bounded\.sh" "\$work/output" "\$work/test" || rc=\$?$#"$work/test" >"$work/output" 2>\&1 || rc=$?#' \
    "$here/mutant_unit.sh" >"$work/copy/mutant_unit.sh"
if cmp -s "$here/mutant_unit.sh" "$work/copy/mutant_unit.sh"; then
	echo "mutant_bounds_test: the unbounded copy equals the script" >&2
	exit 1
fi
outer=
for name in timeout gtimeout; do
	if command -v "$name" >/dev/null 2>&1; then
		outer=$name
		break
	fi
done
if [ -z "$outer" ]; then
	echo "mutant_bounds_test: timeout(1) (coreutils) is required for the unbounded copy" >&2
	exit 2
fi
rc=0
MUTANT_TIMEOUT=2 "$outer" 8 sh "$work/copy/mutant_unit.sh" "$cc" -O0 "$work/dev" "$work/test.c" \
    limit.c 's/return 1;/for (;;) { } return 1;/' limit.c >/dev/null 2>&1 || rc=$?
if [ "$rc" != 124 ]; then
	echo "mutant_bounds_test: the unbounded copy ended with status $rc, not at the 8 s bound (124)" >&2
	exit 1
fi
echo "mutant_bounds_test: the unbounded copy runs to the outer 8 s bound (status 124), so the bound is what ends the looping case"
