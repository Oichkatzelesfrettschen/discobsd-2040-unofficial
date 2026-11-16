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
# Calibration of tools/check_packed_margin.sh: the known-good sizes must pass
# and print their margin, the known-bad ones (over budget, margin under the
# requirement, one byte under it, malformed operands) must fail. A packed
# size of 22,872 bytes at 25 blocks is the measured adminbox after the sysctl
# trim; 24,607 at 25 blocks is the measured overflow that stopped the build.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
tool=$here/../../tools/check_packed_margin.sh
fail=0

expect() {	# expect STATUS PACKED BLOCKS MARGIN [TEXT]
	want=$1
	shift
	set +e
	out=$(sh "$tool" t "$@" 2>&1)
	got=$?
	set -e
	if [ "$got" -ne "$want" ]; then
		echo "packed_margin: t $* exited $got, want $want: $out" >&2
		fail=1
	fi
}

expect 0 22872 25 128
expect 0 24448 25 128	# margin exactly 128
expect 1 24449 25 128	# margin 127
expect 1 24576 25 128	# fits, no margin
expect 1 24607 25 128	# over the budget
expect 1 24607 25 0	# over the budget with no margin demanded
expect 0 24576 25 0	# exact fit with margin 0 is allowed
expect 2 abc 25 128
expect 2 22872 5 128
expect 2 22872 25

out=$(sh "$tool" adminbox 22872 25 128)
case $out in
*"margin 1704 bytes"*) ;;
*) echo "packed_margin: report lacks the margin: $out" >&2; fail=1 ;;
esac

[ "$fail" -eq 0 ] || exit 1
echo "packed_margin: all cases passed"
