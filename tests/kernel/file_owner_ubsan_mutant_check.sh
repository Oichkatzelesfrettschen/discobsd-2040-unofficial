#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 MUTANT" >&2
	exit 2
fi

mutant=$1
output=file_owner_unchecked-negation.mutant.out

if ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 \
    "$mutant" >"$output" 2>&1; then
	status=0
else
	status=$?
fi

expected='runtime error: negation of -2147483648'
# GCC's runtime exits with status 1. Darwin's Clang runtime raises SIGABRT,
# which Darwin's /bin/sh reports as 128 plus signal 6.
case $status in
1|134)
	accepted_status=yes
	;;
*)
	accepted_status=no
	;;
esac

if [ "$accepted_status" != yes ] || \
    ! grep -F "$expected" "$output" >/dev/null; then
	cat "$output" >&2
	echo "FAIL file-owner unchecked-negation mutation rejection" >&2
	exit 1
fi

echo "PASS file-owner unchecked-negation mutation rejected"
