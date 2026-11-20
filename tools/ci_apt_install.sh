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

# Install packages on a CI runner with each network phase of apt-get bounded
# in wall-clock time and retried. An apt-get that reads a mirror trickling
# bytes never trips its idle timeout: on 2026-11-18 the 57.5 MB
# gcc-arm-none-eabi download took 16 minutes (a healthy run takes about 1
# minute for the whole install) and the flash-id job hit its 20-minute limit
# inside the step. Three phases run in order:
#
#   apt-get update                    bounded, retried
#   apt-get install --download-only   bounded, retried; a killed download
#                                     leaves no dpkg state, and apt resumes the
#                                     partial archive on the next attempt
#   apt-get install                   unbounded here, from the local archives
#                                     (the step's timeout-minutes covers it)
#
# usage: ci_apt_install.sh PACKAGE...
#        ci_apt_install.sh --selftest
#
# Environment: CI_APT_ATTEMPTS (default 3) and CI_APT_SECONDS (default 150) set
# the tries and the wall-clock bound of one network phase, and
# CI_APT_KILL_SECONDS (default 10) the time between the TERM that ends the phase
# and the KILL that follows when the process ignores it; CI_APT_SUDO is the
# privilege prefix (default sudo, empty in a root container); CI_APT_GET names
# the apt-get command. The bound command is timeout(1), or gtimeout(1).
set -eu

self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
apt_get=${CI_APT_GET:-apt-get}
attempts=${CI_APT_ATTEMPTS:-3}
seconds=${CI_APT_SECONDS:-150}
kill_after=${CI_APT_KILL_SECONDS:-10}
priv=${CI_APT_SUDO-sudo}

usage() {
	echo "usage: ci_apt_install.sh PACKAGE... | --selftest" >&2
	exit 2
}

bound_command() {
	if command -v timeout >/dev/null 2>&1; then
		echo timeout
	elif command -v gtimeout >/dev/null 2>&1; then
		echo gtimeout
	fi
}

# phase LABEL APT-GET-ARGUMENT...: one network phase, retried up to $attempts.
phase() {
	label=$1
	shift
	n=1
	while :; do
		rc=0
		# priv is empty or "sudo" and bound is a command name; both stay
		# unquoted so that an empty prefix vanishes.
		# shellcheck disable=SC2086
		$priv $bound -k "$kill_after" "$seconds" "$apt_get" "$@" || rc=$?
		if [ "$rc" -eq 0 ]; then
			return 0
		fi
		echo "ci_apt_install: $label attempt $n of $attempts ended with status $rc (124 is the ${seconds} s bound)" >&2
		if [ "$n" -ge "$attempts" ]; then
			return 1
		fi
		n=$((n + 1))
	done
}

# One scenario runner shared by the selftest and its mutants. Each scenario
# runs the script SCRIPT against a fake apt-get that logs the phase of every
# call and misbehaves as FAKE_MODE says, then compares the call counts, the
# exit status and, for a permanent hang, the elapsed time.
scenario() {
	script=$1
	name=$2
	mode=$3
	want_rc=$4
	want_calls=$5
	shift 5
	: >"$work/log"
	rm -f "$work/hung"
	start=$(date +%s)
	rc=0
	env FAKE_MODE="$mode" FAKE_LOG="$work/log" FAKE_STATE="$work/hung" FAKE_SLEEP=5 \
	    CI_APT_GET="$work/apt-get" CI_APT_SUDO= CI_APT_SECONDS=1 CI_APT_KILL_SECONDS=1 \
	    CI_APT_ATTEMPTS=2 \
	    sh "$script" "$@" >"$work/out" 2>&1 || rc=$?
	elapsed=$(($(date +%s) - start))
	calls=$(tr '\n' ' ' <"$work/log")
	if [ "$rc" != "$want_rc" ] || [ "$calls" != "$want_calls" ]; then
		echo "ci_apt_install: scenario $name: status $rc (want $want_rc), calls '$calls' (want '$want_calls')" >&2
		return 1
	fi
	# Two attempts of a 1 s bound and a 1 s kill delay end within 4 s; a fake
	# that sleeps or spins for 5 s per attempt shows an absent bound.
	case $mode in
	hang-download | ignore-term-download)
		if [ "$elapsed" -ge 8 ]; then
			echo "ci_apt_install: scenario $name: ran $elapsed s, the bound did not hold" >&2
			return 1
		fi
		;;
	esac
	return 0
}

# try SCENARIO-ARGUMENT...: run one scenario, record a failure, and under
# FAILFAST (the mutant runs) stop the list at the first one.
try() {
	if ! scenario "$@"; then
		status=1
		if [ -n "$failfast" ]; then
			return 1
		fi
	fi
	return 0
}

scenarios() {
	script=$1
	failfast=${2:-}
	status=0
	try "$script" good ok 0 'update download install ' pkga pkgb || return 1
	try "$script" stalled-download hang-first-download 0 'update download download install ' pkga || return 1
	try "$script" stalled-update hang-first-update 0 'update update download install ' pkga || return 1
	try "$script" permanent-stall hang-download 1 'update download download ' pkga || return 1
	try "$script" term-ignored ignore-term-download 1 'update download download ' pkga || return 1
	try "$script" install-failure fail-install 100 'update download install ' pkga || return 1
	try "$script" no-package ok 2 '' || return 1
	return "$status"
}

# Remove the selftest's work directory file by file: the files first, then
# the directories from the deepest, so that no recursive deletion runs.
cleanup() {
	trap - EXIT HUP INT TERM
	find "$work" -depth \( -type f -o -type l \) -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
}

selftest() {
	bound=$(bound_command)
	if [ -z "$bound" ]; then
		echo "SKIP ci_apt_install selftest: timeout(1) (coreutils) is unavailable; no execution occurred"
		exit 0
	fi
	work=$(mktemp -d "${TMPDIR:-/tmp}/ci-apt-install.XXXXXX")
	trap cleanup EXIT
	trap 'exit 1' HUP INT TERM
	cat >"$work/apt-get" <<'FAKE'
#!/bin/sh
# Fake apt-get: logs the phase of the call; FAKE_MODE picks the misbehavior.
case $1 in
update) phase=update ;;
install)
	phase=install
	for arg in "$@"; do
		if [ "$arg" = --download-only ]; then phase=download; fi
	done
	;;
*) exit 64 ;;
esac
echo "$phase" >>"$FAKE_LOG"
case $FAKE_MODE:$phase in
hang-first-download:download | hang-first-update:update)
	if [ ! -e "$FAKE_STATE" ]; then
		: >"$FAKE_STATE"
		exec sleep "$FAKE_SLEEP"
	fi
	;;
hang-download:download) exec sleep "$FAKE_SLEEP" ;;
ignore-term-download:download)
	# A download that ignores TERM and burns CPU for FAKE_SLEEP seconds.
	trap '' TERM
	end=$(($(date +%s) + FAKE_SLEEP))
	while [ "$(date +%s)" -lt "$end" ]; do :; done
	;;
fail-install:install) exit 100 ;;
esac
exit 0
FAKE
	chmod +x "$work/apt-get"
	scenarios "$self" || {
		echo "ci_apt_install: selftest: the script fails its own scenarios" >&2
		exit 1
	}
	echo "ci_apt_install: 7 scenarios pass"
	# Known-bad copies must fail at least one scenario.
	mutant() {
		label=$1
		expr=$2
		sed -e "$expr" "$self" >"$work/mutant.sh"
		if cmp -s "$self" "$work/mutant.sh"; then
			echo "ci_apt_install: selftest: mutant '$label' changed nothing" >&2
			exit 1
		fi
		if scenarios "$work/mutant.sh" failfast >/dev/null 2>&1; then
			echo "ci_apt_install: selftest: mutant '$label' passed every scenario" >&2
			exit 1
		fi
		echo "mutant rejected: $label"
	}
	mutant 'phase without the time bound' 's/\$bound -k "\$kill_after" "\$seconds" //'
	mutant 'phase without a kill after the first signal' 's/ -k "\$kill_after"//'
	mutant 'phase without a retry' 's/if \[ "\$n" -ge "\$attempts" \]; then/if true; then/'
	mutant 'download and install in one call' 's/--download-only//'
	mutant 'failure of the last attempt ignored' 's/^\t\t\treturn 1$/\t\t\treturn 0/'
	echo "ci_apt_install: selftest ok"
	exit 0
}

if [ "$#" -eq 0 ]; then
	usage
fi
if [ "$1" = --selftest ]; then
	selftest
fi
case $attempts:$seconds:$kill_after in
*[!0-9:]* | :* | *: | *::* | 0:* | *:0:* | *:0) usage ;;
esac
bound=$(bound_command)
if [ -z "$bound" ]; then
	echo "ci_apt_install: timeout(1) from coreutils is required" >&2
	exit 2
fi

phase update update
phase download install -y --no-install-recommends --download-only "$@"
# shellcheck disable=SC2086
$priv "$apt_get" install -y --no-install-recommends "$@"
