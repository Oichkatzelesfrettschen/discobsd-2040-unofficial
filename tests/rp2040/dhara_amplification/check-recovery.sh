#!/bin/sh
set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
dhara=$topsrc/sys/arch/rp2040/dhara
work=$(mktemp -d "${TMPDIR:-/tmp}/dhara-recovery.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

build()
{
    output=$1
    shift
    ${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
        -Wstrict-prototypes -Wold-style-definition \
        -fsanitize=address,undefined -I"$topsrc/sys/arch" -I"$dhara" \
        "$@" "$topsrc/tests/rp2040/dhara_amplification/recovery_test.c" \
        "$dhara/error.c" "$dhara/journal.c" "$dhara/map.c" -o "$output"
}

rejects()
{
    status=0
    "$1" >"$work/rejected.log" 2>&1 || status=$?
    expected=${3:-1}
    if [ "$status" -ne "$expected" ]; then
        cat "$work/rejected.log" >&2
        echo "recovery: expected status $expected, received $status" >&2
        exit 1
    fi
    cat "$work/rejected.log"
    grep -q "$2" "$work/rejected.log"
}

build "$work/atomic"
"$work/atomic"
build "$work/early" -DRECOVERY_ACK_EARLY
rejects "$work/early" 'acknowledged sector'
build "$work/corrupt" -DRECOVERY_CORRUPT
rejects "$work/corrupt" 'recovery: '
build "$work/skip-meta" -DRECOVERY_SKIP_META
rejects "$work/skip-meta" '^FAIL io: write of sector 1 failed, dhara error 7$' 2
echo 'PASS Dhara complete-operation recovery; early ack, corruption and missing metadata rejected'

# Characterization, not a passing torn-write durability claim.
build "$work/torn" -DRECOVERY_TORN=1
rejects "$work/torn" 'mixed sector'
echo 'LIMIT Dhara partial-prefix recovery violates the old/new sector contract'
