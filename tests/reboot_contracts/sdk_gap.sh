#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The harness build must not depend on what one C library puts in its
# headers, and must not shadow a header the C library's own headers include:
# an include directory holding sys/signal.h or signal.h replaces the SDK's
# version inside <signal.h> and <sys/wait.h> and breaks them (macOS).
#
#  1. The shim directory holds exactly sys/reboot.h and utmp.h. The check is
#     calibrated by a copy with an added sys/signal.h, which it must reject.
#  2. Two probes model a C library without _PATH_WTMP and without kill() in
#     the shim's reach, with no header shadowed; each must compile with the
#     shim and fail with the matching fallback removed.
#  3. reboot_unit.c includes every header reboot.c does before it renames a
#     system call, so no C library prototype is renamed with its asm-label
#     alias (macOS: syslog$DARWIN_EXTSN). A rename placed before the includes
#     is calibrated on a stand-in alias header: it must fail to link, and
#     the order the unit uses must link.
# usage: sdk_gap.sh HOSTCC HOST_CFLAGS DIR REBOOT_C
set -eu

cc=$1
cflags=$2
dir=$3
reboot_c=$4
out=$dir/sdk_gap.o
fail=0

shim_allowlist() {	# shim_allowlist SHIMDIR: print offenders, status 1 if any
	bad=0
	for f in $(cd "$1" && find . -type f | sort); do
		case $f in
		./sys/reboot.h | ./utmp.h) ;;
		*) echo "$f"; bad=1 ;;
		esac
	done
	return $bad
}

if shim_allowlist "$dir/shim" > "$dir/sdk_gap.out"; then
	echo "sdk_gap: shim directory exposes only sys/reboot.h and utmp.h"
else
	echo "sdk_gap: shim directory exposes extra headers:" >&2
	cat "$dir/sdk_gap.out" >&2
	fail=1
fi

tmp=$dir/sdk_gap.tmp
rm -rf "$tmp"
mkdir -p "$tmp/sys"
cp "$dir/shim/utmp.h" "$tmp/utmp.h"
cp "$dir/shim/sys/reboot.h" "$tmp/sys/reboot.h"
echo "/* shadow */" > "$tmp/sys/signal.h"
if shim_allowlist "$tmp" > /dev/null; then
	echo "sdk_gap: a shim copy holding sys/signal.h was not rejected" >&2
	fail=1
else
	echo "sdk_gap: a shim copy holding sys/signal.h is rejected"
fi
rm -rf "$tmp"

try() {	# try NAME WANT(0|1) SOURCE EXTRA...
	name=$1
	want=$2
	probe=$3
	shift 3
	set +e
	# shellcheck disable=SC2086
	"$cc" $cflags "$@" -c -o "$out" "$dir/$probe" > "$dir/sdk_gap.out" 2>&1
	got=$?
	set -e
	if [ "$want" -eq 0 ] && [ "$got" -ne 0 ]; then
		echo "sdk_gap: $name failed to compile" >&2
		cat "$dir/sdk_gap.out" >&2
		fail=1
	elif [ "$want" -ne 0 ] && [ "$got" -eq 0 ]; then
		echo "sdk_gap: $name compiled but must not" >&2
		fail=1
	else
		echo "sdk_gap: $name ok"
	fi
}

try wtmp-fallback 0 probe_wtmp.c
try no-wtmp-fallback 1 probe_wtmp.c -DREBOOT_SHIM_NO_WTMP_FALLBACK
try signal-fallback 0 probe_kill.c
try no-signal-fallback 1 probe_kill.c -DREBOOT_SHIM_NO_SIGNAL_H

# 3a. every include of reboot.c appears in reboot_unit.c
grep '^#include <' "$reboot_c" | while read -r line; do
	grep -Fxq "$line" "$dir/reboot_unit.c" || { echo "$line"; }
done > "$dir/sdk_gap.out"
if [ -s "$dir/sdk_gap.out" ]; then
	echo "sdk_gap: reboot_unit.c lacks includes of reboot.c:" >&2
	cat "$dir/sdk_gap.out" >&2
	fail=1
else
	echo "sdk_gap: reboot_unit.c covers every include of reboot.c"
fi
# calibration: a reboot.c with one more include must be reported
{ cat "$reboot_c"; echo '#include <math.h>'; } > "$dir/sdk_gap.src"
grep '^#include <' "$dir/sdk_gap.src" | while read -r line; do
	grep -Fxq "$line" "$dir/reboot_unit.c" || { echo "$line"; }
done > "$dir/sdk_gap.out"
if [ -s "$dir/sdk_gap.out" ]; then
	echo "sdk_gap: an include missing from reboot_unit.c is reported"
else
	echo "sdk_gap: an include missing from reboot_unit.c was not reported" >&2
	fail=1
fi

# 3b. the alias probe: rename first must fail to link, rename after must link
alias_link() {	# alias_link NAME WANT(0|1) EXTRA...
	name=$1
	want=$2
	shift 2
	set +e
	# shellcheck disable=SC2086
	"$cc" $cflags "$@" -o "$dir/sdk_gap.bin" "$dir/alias_probe.c" "$dir/alias_main.c" > "$dir/sdk_gap.out" 2>&1
	got=$?
	set -e
	if [ "$want" -eq 0 ] && [ "$got" -ne 0 ]; then
		echo "sdk_gap: $name failed to link" >&2
		cat "$dir/sdk_gap.out" >&2
		fail=1
	elif [ "$want" -ne 0 ] && [ "$got" -eq 0 ]; then
		echo "sdk_gap: $name linked but must not" >&2
		fail=1
	else
		echo "sdk_gap: $name ok"
	fi
}
alias_link rename-after-includes 0
alias_link rename-before-includes 1 -DPROBE_RENAME_FIRST -Dsyslog=test_syslog
rm -f "$out" "$dir/sdk_gap.out" "$dir/sdk_gap.src" "$dir/sdk_gap.bin"
[ "$fail" -eq 0 ] || exit 1
