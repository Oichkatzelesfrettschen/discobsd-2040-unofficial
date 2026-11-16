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
# The harness build must not depend on what one C library happens to put in
# its headers. gap/ shadows <paths.h> (no _PATH_WTMP) and <sys/signal.h> (no
# kill) the way the macOS SDK lacks them. With the shim, reboot.c must compile
# in that environment; with either shim fallback removed it must not, which
# is the calibration that the fallbacks are what makes it compile.
# usage: sdk_gap.sh HOSTCC HOST_CFLAGS RENAMES REBOOT_C DIR
set -eu

cc=$1
cflags=$2
renames=$3
src=$4
dir=$5
out=$dir/sdk_gap.o
fail=0

try() {	# try NAME WANT(0|1) EXTRA...
	name=$1
	want=$2
	shift 2
	set +e
	# shellcheck disable=SC2086
	"$cc" $cflags -Wno-error=unused-parameter -I"$dir/gap" "$@" $renames -c -o "$out" "$src" > "$dir/sdk_gap.out" 2>&1
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

try shim-with-fallbacks 0
try no-signal-fallback 1 -DREBOOT_SHIM_NO_SIGNAL_H
try no-wtmp-fallback 1 -DREBOOT_SHIM_NO_WTMP_FALLBACK
rm -f "$out" "$dir/sdk_gap.out"
[ "$fail" -eq 0 ] || exit 1
