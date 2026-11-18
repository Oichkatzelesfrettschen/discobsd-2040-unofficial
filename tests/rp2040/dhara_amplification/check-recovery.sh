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
set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
dhara=$topsrc/sys/arch/rp2040/dhara
work=$(mktemp -d "${TMPDIR:-/tmp}/dhara-recovery.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	find "$work" -depth -type f -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

fail()
{
	echo "dhara_recovery: FAIL: $*" >&2
	exit 1
}

# build BINARY JOURNAL [-D...]: link the oracle with Dhara as strict C17
# under the address and undefined-behavior sanitizers.
build()
{
	output=$1
	journal=$2
	shift 2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$topsrc/sys/arch" -I"$dhara" \
	    "$@" "$topsrc/tests/rp2040/dhara_amplification/recovery_test.c" \
	    "$dhara/error.c" "$journal" "$dhara/map.c" -o "$output" ||
		fail "strict C17 sanitizer build"
}

# run BINARY: its status and log land in $status and $work/run.log.
run()
{
	status=0
	"$1" >"$work/run.log" 2>&1 || status=$?
}

# rejects BINARY PATTERN [STATUS]: the mutant exits with STATUS (default 1,
# the oracle's assertion status) and its log carries PATTERN. A sanitizer
# abort or any other status is a crash, which is no calibration.
rejects()
{
	run "$1"
	if [ "$status" -ne "${3:-1}" ]; then
		cat "$work/run.log" >&2
		fail "$1 exited $status, expected ${3:-1}"
	fi
	cat "$work/run.log"
	grep -q "$2" "$work/run.log" || fail "$1 did not report $2"
}

build "$work/atomic" "$dhara/journal.c"
run "$work/atomic"
cat "$work/run.log"
[ "$status" -eq 0 ] || fail "complete-operation cut matrix"

build "$work/early" "$dhara/journal.c" -DRECOVERY_ACK_EARLY
rejects "$work/early" 'acknowledged sector'
build "$work/corrupt" "$dhara/journal.c" -DRECOVERY_CORRUPT
rejects "$work/corrupt" 'recovery: '
build "$work/skip-meta" "$dhara/journal.c" -DRECOVERY_SKIP_META
rejects "$work/skip-meta" '^FAIL io: write of sector 1 failed, dhara error 7$' 2
echo "PASS Dhara complete-operation recovery; early ack, corruption and missing metadata rejected"

# A completed half-page program or half-block erase is not assumed here.
# Recovery must keep the same acknowledged and old/new sector contract.
build "$work/torn" "$dhara/journal.c" -DRECOVERY_TORN=1
run "$work/torn"
cat "$work/run.log"
[ "$status" -eq 0 ] || fail "partial-prefix cut matrix"

# Remove only the checkpoint tree check. The production workload must find
# a mixed sector when the torn checkpoint is accepted.
awk '
	/^[[:space:]]+cp_tree_whole\(j, p - 1\)\) \{$/ {
		sub(/cp_tree_whole\(j, p - 1\)/, "(cp_tree_whole(j, p - 1) || 1)")
		matches++
	}
	{ print }
	END { if (matches != 1) exit 1 }
' "$dhara/journal.c" >"$work/no-tree-check.c" ||
	fail "checkpoint tree mutation anchor changed"
build "$work/no-tree-check" "$work/no-tree-check.c" -DRECOVERY_TORN=1
rejects "$work/no-tree-check" '^recovery: mixed sector [0-9][0-9]*$'
grep -q '^FAIL recovery cut ' "$work/run.log" ||
	fail "torn checkpoint control did not fail at a cut"
echo "PASS Dhara partial-prefix recovery; unchecked checkpoint rejected"
