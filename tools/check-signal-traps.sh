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
# also HUP, INT, TERM, QUIT or a signal number, wherever the command sits on
# its line (after a separator, in a group) and whatever follows it (a
# comment, a command, a redirection); a reset (trap - ...) passes.
# --selftest first runs the scan over known-bad and known-good command
# forms, including quoted # and signal names inside the action, then runs a
# conforming and a combined fixture under SIGTERM: the
# conforming one must exit 143 with its work directory removed, and the
# combined one must resume and reach its completion marker, which ties the
# scan's rule to the shell's behavior.
set -eu

# scan FILE: print each combined trap with its line number; status 1 if any.
# The awk program tokenizes the file as a shell would for this purpose: quotes
# (which may span lines), backslashes, a comment that starts a token, and the
# command separators ; & | ( ). A command whose first word is trap is
# combined when its operands name EXIT (or 0) and also HUP, INT, TERM, QUIT or
# a signal number; the action operand is never read as a signal, so a quoted
# action that mentions signal names, a trailing comment and a following
# command do not change the verdict.
scan() {
	awk '
	function start_token() {
		if (!intok) {
			intok = 1
			if (ntok == 0) { cmdline = NR; cmdtext = $0 }
		}
	}
	function end_token() {
		if (intok) { toks[++ntok] = tok; tok = ""; intok = 0 }
	}
	# unquote(t): the word as the shell resolves it before trap sees it --
	# single quotes literal, double quotes with the backslash escapes of
	# POSIX, a backslash outside quotes dropped.
	function unquote(t,   r, i, c, m, n, x) {
		r = ""; m = 0; n = length(t)
		for (i = 1; i <= n; i++) {
			c = substr(t, i, 1)
			if (m == 1) { if (c == SQ) m = 0; else r = r c; continue }
			if (m == 2) {
				if (c == DQ) { m = 0; continue }
				if (c == BS && i < n) {
					x = substr(t, i + 1, 1)
					if (x == "$" || x == "\140" || x == DQ || x == BS) { r = r x; i++; continue }
					if (x == "\n") { i++; continue }
				}
				r = r c; continue
			}
			if (c == SQ) { m = 1; continue }
			if (c == DQ) { m = 2; continue }
			if (c == BS && i < n) { r = r substr(t, i + 1, 1); i++; continue }
			r = r c
		}
		return r
	}
	# condition(t): "E" for EXIT, "S" for a signal, "" for anything else
	# (ERR, DEBUG, RETURN, an unexpanded variable). A condition is the
	# unquoted word in any case, with or without the SIG prefix, or a number
	# (0 is EXIT, any other number a signal).
	function condition(t,   s) {
		s = toupper(unquote(t))
		sub(/^SIG/, "", s)
		if (s == "EXIT" || s ~ /^0+$/) return "E"
		if (s ~ /^[0-9]+$/) return "S"
		if (s ~ /^(RTMIN|RTMAX)([+-][0-9]+)?$/) return "S"
		if (s ~ /^(HUP|INT|QUIT|ILL|TRAP|ABRT|IOT|EMT|FPE|KILL|BUS|SEGV|SYS|PIPE|ALRM|TERM|URG|STOP|TSTP|CONT|CHLD|CLD|TTIN|TTOU|IO|POLL|XCPU|XFSZ|VTALRM|PROF|WINCH|INFO|USR1|USR2|PWR|STKFLT|LOST|THR)$/) return "S"
		return ""
	}
	function end_command(   i, j, k, a, has_exit, has_sig, c) {
		end_token()
		i = 1
		while (i <= ntok && (toks[i] == "{" || toks[i] == "!" || toks[i] == "then" || toks[i] == "do" || toks[i] == "else")) i++
		if (i <= ntok && unquote(toks[i]) == "trap") {
			j = i + 1
			a = unquote(toks[j])
			if (a == "--") { j++; a = unquote(toks[j]) }
			# A first operand that is an unsigned integer makes every
			# operand a condition to reset, and "-" or an option is a
			# reset or a listing; neither installs an action.
			if (j <= ntok && a != "-" && a !~ /^-[a-zA-Z]/ && a !~ /^[0-9]+$/) {
				has_exit = 0; has_sig = 0
				for (k = j + 1; k <= ntok; k++) {
					c = condition(toks[k])
					if (c == "E") has_exit = 1
					else if (c == "S") has_sig = 1
				}
				if (has_exit && has_sig) {
					printf "%s:%d: %s\n", FILENAME, cmdline, cmdtext
					bad = 1
				}
			}
		}
		ntok = 0
	}
	BEGIN { SQ = "\047"; DQ = "\042"; BS = "\134" }
	{
		line = $0
		n = length(line)
		for (p = 1; p <= n; p++) {
			c = substr(line, p, 1)
			if (in_s) { tok = tok c; if (c == SQ) in_s = 0; continue }
			if (in_d) {
				if (c == BS && p < n) { tok = tok c substr(line, p + 1, 1); p++; continue }
				tok = tok c
				if (c == DQ) in_d = 0
				continue
			}
			if (c == BS) {
				if (p == n) { cont = 1; break }
				start_token(); tok = tok c substr(line, p + 1, 1); p++
				continue
			}
			if (c == SQ) { start_token(); in_s = 1; tok = tok c; continue }
			if (c == DQ) { start_token(); in_d = 1; tok = tok c; continue }
			if (c == "#" && !intok) break
			if (c == " " || c == "\t") { end_token(); continue }
			if (c == ";" || c == "&" || c == "|" || c == ")" ||
			    (c == "(" && substr(line, p - 1, 1) != "$")) { end_command(); continue }
			start_token(); tok = tok c
		}
		if (in_s || in_d) tok = tok "\n"
		else if (cont) cont = 0
		else end_command()
	}
	END { end_command(); exit bad }
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

# expect_scan VERDICT LABEL TEXT: write TEXT to a file and require the scan
# to reject it (bad) or accept it (good).
expect_scan() {
	printf '%s\n' "$3" >"$dir/case.scan"
	if scan "$dir/case.scan" >/dev/null; then
		found=good
	else
		found=bad
	fi
	if [ "$found" != "$1" ]; then
		echo "selftest: $2 ($3): expected $1, scan said $found" >&2
		exit 1
	fi
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
	# Known-bad: each combines EXIT with a signal in one trap command.
	expect_scan bad "trailing comment" "trap cleanup EXIT TERM # cleanup"
	expect_scan bad "trailing command" "trap cleanup EXIT TERM; echo done"
	expect_scan bad "trailing background" "trap cleanup EXIT INT &"
	expect_scan bad "quoted # in the action" "trap 'echo # x' EXIT TERM"
	expect_scan bad "double-quoted # in the action" 'trap "echo #" EXIT TERM'
	expect_scan bad "after &&" "true && trap cleanup EXIT INT"
	expect_scan bad "in a brace group" "{ trap cleanup EXIT HUP; }"
	expect_scan bad "in a subshell" "(trap cleanup EXIT HUP)"
	expect_scan bad "numeric signals" "trap 'rm -f x' 0 2"
	expect_scan bad "SIG prefix" "trap cleanup EXIT SIGTERM"
	expect_scan bad "after --" "trap -- cleanup EXIT TERM"
	expect_scan bad "signals before EXIT" "trap cleanup INT EXIT"
	expect_scan bad "redirected" "trap cleanup EXIT TERM 2>/dev/null"
	expect_scan bad "indented" "	trap cleanup EXIT HUP INT TERM"
	expect_scan bad "multi-line action" "trap 'rm -f a
rm -f b' EXIT HUP"
	expect_scan bad "continued line" "trap cleanup EXIT \\
    TERM"
	# The spellings POSIX sh accepts for a trap condition (XCU trap): EXIT or
	# 0, a signal name without or with the SIG prefix, a signal number; each
	# word after quote removal and, as shells accept, in any case.
	expect_scan bad "single-quoted conditions" "trap cleanup 'EXIT' 'TERM'"
	expect_scan bad "double-quoted conditions" 'trap cleanup "EXIT" "TERM"'
	expect_scan bad "backslash-quoted condition" 'trap cleanup EX\IT TERM'
	expect_scan bad "partly quoted conditions" "trap cleanup E'XI'T T\"ER\"M"
	expect_scan bad "lowercase conditions" "trap cleanup exit term"
	expect_scan bad "mixed-case conditions" "trap cleanup Exit TeRm"
	expect_scan bad "lowercase SIG prefix" "trap cleanup exit sigterm"
	expect_scan bad "quoted SIG prefix" "trap cleanup EXIT 'SIGINT'"
	expect_scan bad "EXIT as 0 and TERM as 15" "trap cleanup 0 15"
	expect_scan bad "quoted numbers" "trap cleanup '0' \"15\""
	expect_scan bad "leading-zero exit number" "trap cleanup 00 TERM"
	expect_scan bad "HUP as 1" "trap cleanup EXIT 1"
	expect_scan bad "INT as 2" "trap cleanup EXIT 2"
	expect_scan bad "QUIT" "trap cleanup EXIT QUIT"
	expect_scan bad "a signal other than HUP INT TERM" "trap cleanup EXIT USR1"
	expect_scan bad "lowercase other signal" "trap cleanup exit pipe"
	expect_scan bad "SIG-prefixed other signal" "trap cleanup EXIT SIGPIPE"
	expect_scan bad "other signal number" "trap cleanup EXIT 10"
	expect_scan bad "real-time signal" "trap cleanup EXIT RTMIN+1"
	expect_scan bad "quoted command name" "'trap' cleanup EXIT TERM"
	expect_scan bad "empty action" "trap '' EXIT TERM"
	expect_scan bad "quoted conditions after --" "trap -- cleanup 'exit' term"
	# Known-good: one signal class per command, or no trap command at all.
	expect_scan good "reset" "trap - EXIT HUP INT TERM"
	expect_scan good "split traps" "trap 'rm -f x' EXIT
trap 'exit 143' TERM"
	expect_scan good "two traps on a line" "trap cleanup EXIT; trap 'exit 143' TERM"
	expect_scan good "exit trap with comment" "trap cleanup EXIT # EXIT TERM"
	expect_scan good "signal names inside the action" "trap 'echo EXIT TERM' EXIT"
	expect_scan good "signal names inside a double-quoted action" 'trap "echo EXIT TERM" EXIT'
	expect_scan good "quoted # then comment" "trap 'echo # x' EXIT # TERM"
	expect_scan good "signal-only trap with comment" "trap 'exit 143' TERM # EXIT"
	expect_scan good "action word that names a signal" "trap INT EXIT"
	expect_scan good "unsigned integer first operand resets" "trap 0 2"
	expect_scan good "integer first operand resets EXIT too" "trap 0 EXIT 2"
	expect_scan good "single integer reset" "trap 2"
	expect_scan good "pseudo-condition with EXIT" "trap cleanup ERR EXIT"
	expect_scan good "quoted EXIT alone" "trap cleanup 'EXIT'"
	expect_scan good "0 alone" "trap cleanup 0"
	expect_scan good "ignore a signal" "trap '' TERM"
	expect_scan good "quoted reset" "trap '-' EXIT TERM"
	expect_scan good "commented trap" "# trap cleanup EXIT TERM"
	expect_scan good "trap in a quoted string" "printf '%s\n' 'trap cleanup EXIT TERM'"
	expect_scan good "trap listing" "trap -p"
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
