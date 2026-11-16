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
# Signal-trap check for shell gates. A POSIX shell that runs a trap action
# for HUP, INT or TERM resumes the script afterward, so one trap command
# bound to EXIT and to a signal cleans the work directory and then lets the
# script continue against the deleted directory. The idiom that terminates
# is an EXIT trap that cleans, and one trap per signal that exits with the
# signal's status (129, 130, 143); the EXIT trap then runs once, at exit.
#
# usage: check-signal-traps.sh FILE...
#        check-signal-traps.sh --selftest
#
# The scan rejects any trap command whose signal list names EXIT (or 0) and
# also HUP, INT, TERM, QUIT or a signal number; a reset (trap - ...) passes.
# --selftest first runs the scan over a conforming and a combined fixture
# (the second must be rejected), then runs both fixtures under SIGTERM: the
# conforming one must exit 143 with its work directory removed, and the
# combined one must resume and reach its completion marker, which ties the
# scan's rule to the shell's behavior.
set -eu

# scan FILE: print each combined trap with its line number; status 1 if any.
scan() {
	awk '
	function in_list(w) {
		return w ~ /^(EXIT|0|HUP|INT|TERM|QUIT|1|2|3|15)$/
	}
	{
		line = $0
		n = split(line, f, /[ \t]+/)
		while (n > 0 && f[n] == "") n--
		tail = n
		while (tail > 0 && in_list(f[tail])) tail--
		if (n - tail < 2) next
		has_exit = 0; has_sig = 0
		for (i = tail + 1; i <= n; i++) {
			if (f[i] == "EXIT" || f[i] == "0") has_exit = 1
			else has_sig = 1
		}
		if (!has_exit || !has_sig) next
		if (line ~ /^[ \t]*trap[ \t]+-/) next
		if (line ~ /^[ \t]*trap[ \t]/ || line ~ /[\x27"][ \t]+(EXIT|0|HUP|INT|TERM|QUIT|1|2|3|15)[ \t]/) {
			printf "%s:%d: %s\n", FILENAME, NR, line
			bad = 1
		}
	}
	END { exit bad }
	' "$1"
}

# fixture KIND PATH: a script that creates a work directory, installs its
# traps, announces readiness and then idles five seconds before writing a
# completion marker beside the work directory.
fixture() {
	{
		printf '%s\n' '#!/bin/sh' 'set -eu' 'work=$1' 'marker=$2' 'mkdir "$work"'
		if [ "$1" = good ]; then
			printf '%s\n' 'cleanup() { rm -rf "$work"; }' \
				'trap cleanup EXIT' "trap 'exit 129' HUP" \
				"trap 'exit 130' INT" "trap 'exit 143' TERM"
		else
			printf '%s\n' "trap 'rm -rf \"\$work\"' EXIT HUP INT TERM"
		fi
		printf '%s\n' ': >"$work/ready"' 'i=0' \
			'while [ "$i" -lt 25 ]; do sleep 0.2; i=$((i + 1)); done' \
			': >"$marker"'
	} >"$2"
}

# run_term KIND DIR: run the fixture, send SIGTERM once it is ready and
# print its exit status.
run_term() {
	fixture "$1" "$2/$1.sh"
	sh "$2/$1.sh" "$2/$1.work" "$2/$1.marker" &
	pid=$!
	waited=0
	while [ ! -f "$2/$1.work/ready" ] && [ "$waited" -lt 50 ]; do
		sleep 0.1
		waited=$((waited + 1))
	done
	[ -f "$2/$1.work/ready" ] || { echo "selftest: $1 fixture never became ready" >&2; exit 1; }
	kill -TERM "$pid"
	status=0
	wait "$pid" || status=$?
	echo "$status"
}

selftest() {
	dir=$(mktemp -d)
	trap 'rm -rf "$dir"' EXIT
	trap 'exit 129' HUP
	trap 'exit 130' INT
	trap 'exit 143' TERM
	fixture good "$dir/good.scan"
	fixture bad "$dir/bad.scan"
	scan "$dir/good.scan" >/dev/null || { echo "selftest: the conforming fixture was rejected" >&2; exit 1; }
	if scan "$dir/bad.scan" >/dev/null; then
		echo "selftest: the combined trap was accepted" >&2
		exit 1
	fi
	printf '%s\n' "trap - EXIT HUP INT TERM" >"$dir/reset.scan"
	scan "$dir/reset.scan" >/dev/null || { echo "selftest: a trap reset was rejected" >&2; exit 1; }
	printf '%s\n' "trap 'rm -f x' 0 2" >"$dir/numeric.scan"
	if scan "$dir/numeric.scan" >/dev/null; then
		echo "selftest: a combined trap on 0 and signal 2 was accepted" >&2
		exit 1
	fi
	printf '%s\n' "trap 'rm -f x' EXIT" "trap 'exit 143' TERM" >"$dir/split.scan"
	scan "$dir/split.scan" >/dev/null || { echo "selftest: split traps were rejected" >&2; exit 1; }
	good_status=$(run_term good "$dir")
	bad_status=$(run_term bad "$dir")
	[ "$good_status" -eq 143 ] || { echo "selftest: conforming fixture exited $good_status, not 143" >&2; exit 1; }
	[ ! -e "$dir/good.work" ] || { echo "selftest: conforming fixture left its work directory" >&2; exit 1; }
	[ ! -e "$dir/good.marker" ] || { echo "selftest: conforming fixture ran past SIGTERM" >&2; exit 1; }
	[ -e "$dir/bad.marker" ] || { echo "selftest: combined fixture did not resume after SIGTERM (status $bad_status)" >&2; exit 1; }
	echo "check-signal-traps: selftest ok (combined trap rejected and resumes after SIGTERM, conforming exits 143)"
}

if [ "$#" -eq 0 ]; then
	echo "usage: check-signal-traps.sh FILE... | --selftest" >&2
	exit 2
fi
if [ "$1" = --selftest ]; then
	selftest
	exit 0
fi
status=0
for file in "$@"; do
	scan "$file" || status=1
done
if [ "$status" -ne 0 ]; then
	echo "check-signal-traps: a trap binds EXIT and a signal to one action; use an EXIT trap and one exiting trap per signal" >&2
	exit 1
fi
echo "check-signal-traps: $# files, no combined traps"
