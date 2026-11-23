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
# own status; 124 when the time bound ended it (the run returns at the bound
# even when the command left a child that holds the output open or ignores the
# signal; such a child can outlive the run); 125 when the run could not
# happen (a bad bound, no timeout(1), a command that timeout(1) could not start,
# which it reports as 125, 126 or 127). A caller treats 124 and 125 as "no
# verdict" and any other failing status as the mutant being rejected, so a
# missing tool never turns into a rejection. The time bound is timeout(1), or gtimeout(1) where coreutils
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
	# Each bound is a positive decimal integer without a leading zero (the
	# shell would read 08 as octal) and of at most nine digits (the arithmetic
	# below must not overflow): the two are checked one at a time.
	for bound_value in "$secs" "$cap"; do
		case $bound_value in
		'' | *[!0-9]* | 0 | 0[0-9]* | [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]*)
			echo "run_bounded.sh: MUTANT_TIMEOUT and MUTANT_OUTPUT_BYTES are positive integers of at most nine digits" >&2
			exit 125
			;;
		esac
	done
	bound=$(bound_command)
	if [ -z "$bound" ]; then
		echo "run_bounded.sh: timeout(1) from coreutils is required" >&2
		exit 125
	fi
	status="$out.status"
	# true, not :, so that a failed redirection does not end the shell.
	if ! { true >"$status" && true >"$out"; } 2>/dev/null; then
		echo "run_bounded.sh: cannot write $out or its status files" >&2
		exit 125
	fi
	# The whole pipeline, the command and the reader of its output, runs under
	# the one timeout, whose process group the signal reaches: a descendant of
	# the command that keeps the output pipe open after the command has ended
	# ends with the bound too, instead of holding the reader. The command's own
	# status goes to a file, since the pipe keeps only the reader's.
	outer=0
	"$bound" -k 1 "$secs" sh -c '
		# The wrapper takes the first signal without dying, so that the shell
		# ends only when its pipeline has closed; the command and the reader
		# run in subshells that the signal ends. A descendant that ignores the
		# signal can outlive the run, which returns at the bound.
		trap : TERM
		status=$1
		out=$2
		cap=$3
		shift 3
		# The status of the reader ends the wrapper, so that a failure to keep
		# the output reaches the caller without another write to the same disk.
		{ "$@" 2>&1; echo $? >"$status"; } | {
			head -c "$cap" >"$out"
			hrc=$?
			cat >/dev/null
			exit "$hrc"
		}
	' sh "$status" "$out" "$((cap + 1))" "$@" || outer=$?
	rc=$(cat "$status")
	rm -f "$status"
	case $outer in
	0 | 124 | 137) ;;
	*)
		# The output could not be kept whole, or timeout(1) could not run the
		# wrapper: no verdict on the command.
		echo "run_bounded.sh: the output of $1 could not be kept (status $outer)" >&2
		return 125
		;;
	esac
	size=$(wc -c <"$out" | tr -d ' ')
	if [ "$size" -gt "$cap" ]; then
		echo "run_bounded.sh: output of $1 cut at $cap bytes" >&2
	fi
	if [ -z "$rc" ]; then
		# The command had not ended: the time bound, or a run that did not start.
		case $outer in
		124 | 137) return 124 ;;
		esac
		return 125
	fi
	case $rc in
	125 | 126 | 127) return 125 ;;
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
# try_config NAME WANT-STATUS VAR=VALUE...: the runner, started with the named
# variables and a command that would pass, must end with WANT-STATUS.
try_config() {
	name=$1
	want=$2
	shift 2
	rc=0
	env "$@" "$self" "$work/out" true >/dev/null 2>&1 || rc=$?
	if [ "$rc" != "$want" ]; then
		echo "run_bounded.sh: scenario $name: status $rc, want $want" >&2
		status=1
		if [ -n "$failfast" ]; then
			return 1
		fi
	fi
	return 0
}

# try_raw NAME WANT-STATUS OUTPUT COMMAND...: a run whose output path is
# unusable must end with WANT-STATUS.
try_raw() {
	name=$1
	want=$2
	shift 2
	rc=0
	"$self" "$@" >/dev/null 2>&1 || rc=$?
	if [ "$rc" != "$want" ]; then
		echo "run_bounded.sh: scenario $name: status $rc, want $want" >&2
		status=1
		if [ -n "$failfast" ]; then
			return 1
		fi
	fi
	return 0
}

# try_capture: a run whose output file cannot hold what the reader writes
# (a file-size limit of one block) must end with 125.
try_capture() {
	rc=0
	(
		ulimit -f 1
		"$self" "$work/capture" sh -c 'head -c 3000 /dev/zero'
	) >/dev/null 2>&1 || rc=$?
	if [ "$rc" != 125 ]; then
		echo "run_bounded.sh: scenario capture-failure: status $rc, want 125" >&2
		status=1
		if [ -n "$failfast" ]; then
			return 1
		fi
	fi
	return 0
}

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
	try unrunnable 125 - /nonexistent/run-bounded-command || return 1
	try_config bad-output-bound 125 MUTANT_OUTPUT_BYTES=1:2 || return 1
	try_config huge-output-bound 125 MUTANT_OUTPUT_BYTES=99999999999 || return 1
	try_config bad-timeout-octal 125 MUTANT_TIMEOUT=08 || return 1
	try_config bad-timeout-zero 125 MUTANT_TIMEOUT=0 || return 1
	try_raw bad-output-path 125 /nonexistent/run-bounded-dir/out true || return 1
	# A file-size limit of one block makes the capture fail after the command
	# has printed: the status of the command must not be taken for a verdict.
	try_capture || return 1
	# The command ends at once and leaves a child that holds the output pipe for
	# 6 s: the run ends with the bound, and the status is the command's own.
	try descendant 3 - sh -c '(sleep 6) & exit 3' || return 1
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
	echo "run_bounded.sh: 16 scenarios pass"
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
	# BSD sed (macOS) reads \t literally in an expression: the tab is a character here.
	tab=$(printf '\t')
	mutant 'no time bound' 's/"\$bound" -k 1 "\$secs" sh -c/sh -c/' descendant
	mutant 'no output bound' 's/head -c "\$cap"/cat/' failing-loudly-stdout
	mutant 'status of the command lost' 's/echo \$? >"\$status"/echo 0 >"$status"/' failing
	mutant 'a command that did not start taken for its failure' "s/^${tab}125 | 126 | 127) return 125 ;;\$/${tab}125 | 126 | 127) return \"\$rc\" ;;/" unrunnable
	mutant 'a capture failure ignored' 's/exit "\$hrc"/exit 0/' capture-failure
	mutant 'a malformed bound accepted' "s/^${tab}${tab}${tab}exit 125\$/${tab}${tab}${tab}exit 0/" bad-output-bound
	mutant 'a failing status taken for a timeout' "s/^${tab}return \"\$rc\"\$/${tab}return 124/" passing
	echo "run_bounded.sh: selftest ok"
	exit 0
}

if [ "$#" -eq 0 ]; then
	echo "usage: run_bounded.sh OUTPUT COMMAND [ARGUMENT...] | --selftest" >&2
	exit 125
fi
if [ "$1" = --selftest ]; then
	selftest
fi
if [ "$#" -lt 2 ]; then
	echo "usage: run_bounded.sh OUTPUT COMMAND [ARGUMENT...] | --selftest" >&2
	exit 125
fi
run "$@"
