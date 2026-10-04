#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-standard-requests.XXXXXX")
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
mkdir "$work/mutant"

compile_test()
{
	output=$1
	include_dir=$2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$include_dir" \
	    -o "$output" \
	    "$topsrc/tests/rp2040/usb_standard_requests/usb_standard_requests_test.c"
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
compile_test "$work/usb-standard-requests-test" \
	"$topsrc/sys/arch/rp2040/dev"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-standard-requests-test"

sed 's/length != 2U/length < 2U/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
compile_test "$work/usb-standard-length-mutant" "$work/mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-standard-length-mutant" >"$work/length-mutant.log" 2>&1; then
	echo "usb_standard_requests: FAIL: oversized GET_STATUS length accepted" >&2
	exit 1
fi
echo "usb_standard_requests: PASS (oversized-length mutation rejected)"

sed -e 's/index > 0xffU || length != 0U/length != 0U/' \
	-e 's/return index == 0x81U || index == 0x02U || index == 0x82U;/return (index \& 0xffU) == 0x81U || (index \& 0xffU) == 0x02U || (index \& 0xffU) == 0x82U;/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
compile_test "$work/usb-standard-index-mutant" "$work/mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-standard-index-mutant" >"$work/index-mutant.log" 2>&1; then
	echo "usb_standard_requests: FAIL: reserved endpoint index byte accepted" >&2
	exit 1
fi
echo "usb_standard_requests: PASS (reserved-index mutation rejected)"
