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

# Run one command with its output in a file, its wall-clock time bounded and
# the output it keeps bounded, for the mutant runs that execute a freshly built
# test: a mutant that loops never ends the run and one that prints without end
# fills the disk, and neither is a verdict about the mutant. A mutant that
# fails loudly, with more output than the bound, still ends by itself and is
# still rejected.
#
# usage: run_bounded.sh OUTPUT COMMAND [ARGUMENT...]
#        run_bounded.sh --selftest
#
# Standard output and standard error of COMMAND go to OUTPUT, which keeps the
# first MUTANT_OUTPUT_BYTES bytes (default 1048576) and one byte more when the
# command wrote more; the rest is read and discarded so that the command runs
# to its own end. MUTANT_TIMEOUT seconds (default 120; the slowest passing
# sanitizer test of tests/picocalc runs in under one second) end the command,
# with a kill one second after the first signal. Exit status: the command's
# own status, or 124 when the time bound ended it. A caller treats 124 as "no
# verdict" and any other failing status as the mutant being rejected. The time bound is timeout(1), or gtimeout(1) where coreutils
# installs that name.
set -eu

bound_command() {
	if command -v timeout >/dev/null 2>&1; then
		echo timeout
	elif command -v gtimeout >/dev/null 2>&1; then
		echo gtimeout
	fi
}

run() {
	out=$1
	shift
	secs=${MUTANT_TIMEOUT:-120}
	cap=${MUTANT_OUTPUT_BYTES:-1048576}
	case $secs:$cap in
	*[!0-9:]* | :* | *: | 0:* | *:0)
		echo "run_bounded.sh: MUTANT_TIMEOUT and MUTANT_OUTPUT_BYTES are positive integers" >&2
		exit 2
		;;
	esac
	bound=$(bound_command)
	if [ -z "$bound" ]; then
		echo "run_bounded.sh: timeout(1) from coreutils is required" >&2
		exit 2
	fi
	status="$out.status"
	: >"$status"
	# The subshell leaves the command's status in a file, since the pipe
	# keeps only the status of the reader.
	(
		rc=0
		"$bound" -k 1 "$secs" "$@" 2>&1 || rc=$?
		echo "$rc" >"$status"
	) | {
		head -c "$((cap + 1))" >"$out"
		cat >/dev/null
	}
	rc=$(cat "$status")
	rm -f "$status"
	size=$(wc -c <"$out" | tr -d ' ')
	if [ "$size" -gt "$cap" ]; then
		echo "run_bounded.sh: output of $1 cut at $cap bytes" >&2
	fi
	case $rc in
	137) return 124 ;;
	esac
	return "$rc"
}

# check NAME WANT-STATUS WANT-SIZE COMMAND...: one scenario of the selftest.
# WANT-SIZE is the exact number of bytes the output file must hold, or "-".
check() {
	name=$1
	want=$2
	wantsize=$3
	shift 3
	start=$(date +%s)
	rc=0
	"$self" "$work/out" "$@" || rc=$?
	elapsed=$(($(date +%s) - start))
	size=$(wc -c <"$work/out" | tr -d ' ')
	if [ "$rc" != "$want" ]; then
		echo "run_bounded.sh: scenario $name: status $rc, want $want" >&2
		return 1
	fi
	if [ "$wantsize" != - ] && [ "$size" != "$wantsize" ]; then
		echo "run_bounded.sh: scenario $name: output of $size bytes, want $wantsize" >&2
		return 1
	fi
	if [ "$elapsed" -ge 4 ]; then
		echo "run_bounded.sh: scenario $name: took $elapsed s" >&2
		return 1
	fi
	return 0
}

# Every scenario but the last ends by itself within a few seconds even when
# the bound under test is absent, so a broken copy fails instead of hanging;
# the last one is reached only by a copy that passed the others.
# try SCENARIO...: run one check and, under failfast (the mutant runs), stop
# the list at the first failure.
try() {
	if ! check "$@"; then
		status=1
		if [ -n "$failfast" ]; then
			return 1
		fi
	fi
	return 0
}

scenarios() {
	self=$1
	failfast=${2:-}
	status=0
	export MUTANT_TIMEOUT=1 MUTANT_OUTPUT_BYTES=4096
	try passing 0 - true || return 1
	try failing 3 - sh -c 'echo diagnostic; exit 3' || return 1
	try sleeping 124 - sleep 4 || return 1
	# A command that ignores TERM and spins for 6 s: the 1 s bound plus the 1 s
	# kill-after ends it at 2 s; without the kill it runs to 6 s.
	try term-ignoring 124 - sh -c \
	    'trap "" TERM; end=$(($(date +%s) + 6)); while [ "$(date +%s)" -lt "$end" ]; do :; done' || return 1
	try failing-loudly-stdout 3 4097 sh -c \
	    'i=0; while [ $i -lt 20000 ]; do echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx; i=$((i+1)); done; exit 3' || return 1
	try failing-loudly-stderr 3 4097 sh -c \
	    'i=0; while [ $i -lt 20000 ]; do echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >&2; i=$((i+1)); done; exit 3' || return 1
	# Last, since a copy without the output bound would write for a second.
	try flooding-without-end 124 4097 yes || return 1
	return "$status"
}

# Remove the selftest's work directory file by file, then directory by
# directory, so that no recursive deletion runs.
cleanup() {
	trap - EXIT HUP INT TERM
	find "$work" -depth \( -type f -o -type l \) -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
}

selftest() {
	if [ -z "$(bound_command)" ]; then
		echo "SKIP run_bounded selftest: timeout(1) (coreutils) is unavailable; no execution occurred"
		exit 0
	fi
	here=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
	work=$(mktemp -d "${TMPDIR:-/tmp}/run-bounded.XXXXXX")
	trap cleanup EXIT
	trap 'exit 1' HUP INT TERM
	scenarios "$here" "" || {
		echo "run_bounded.sh: selftest: the script fails its own scenarios" >&2
		exit 1
	}
	echo "run_bounded.sh: 8 scenarios pass"
	# mutant LABEL SED-EXPRESSION SCENARIO: the copy must fail SCENARIO and no
	# earlier one, so that a copy that does not run at all (status 127) is not
	# taken for a rejection.
	mutant() {
		label=$1
		expr=$2
		expected=$3
		sed -e "$expr" "$here" >"$work/mutant.sh"
		if cmp -s "$here" "$work/mutant.sh"; then
			echo "run_bounded.sh: selftest: mutant '$label' changed nothing" >&2
			exit 1
		fi
		chmod +x "$work/mutant.sh"
		if scenarios "$work/mutant.sh" failfast >/dev/null 2>"$work/mutant.err"; then
			echo "run_bounded.sh: selftest: mutant '$label' passed every scenario" >&2
			exit 1
		fi
		if ! grep -F -q "scenario $expected:" "$work/mutant.err"; then
			echo "run_bounded.sh: selftest: mutant '$label' did not fail scenario $expected:" >&2
			cat "$work/mutant.err" >&2
			exit 1
		fi
		echo "mutant rejected: $label (scenario $expected)"
	}
	mutant 'no time bound' 's/"\$bound" -k 1 "\$secs" "\$@"/"$@"/' sleeping
	mutant 'no kill after the first signal' 's/-k 1 //' term-ignoring
	mutant 'no output bound' 's/head -c "\$((cap + 1))"/cat/' failing-loudly-stdout
	mutant 'status of the command lost' 's/echo "\$rc" >"\$status"/echo 0 >"$status"/' failing
	mutant 'a failing status taken for a timeout' 's/^\treturn "\$rc"$/\treturn 124/' passing
	echo "run_bounded.sh: selftest ok"
	exit 0
}

if [ "$#" -eq 0 ]; then
	echo "usage: run_bounded.sh OUTPUT COMMAND [ARGUMENT...] | --selftest" >&2
	exit 2
fi
if [ "$1" = --selftest ]; then
	selftest
fi
if [ "$#" -lt 2 ]; then
	echo "usage: run_bounded.sh OUTPUT COMMAND [ARGUMENT...] | --selftest" >&2
	exit 2
fi
run "$@"
