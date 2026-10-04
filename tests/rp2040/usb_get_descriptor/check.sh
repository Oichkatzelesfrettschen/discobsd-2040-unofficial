#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-get-descriptor.XXXXXX")
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
mkdir "$work/type-mutant" "$work/index-mutant" "$work/langid-mutant"

compile_test()
{
	include_directory=$1
	output=$2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$include_directory" \
	    -o "$output" \
	    "$topsrc/tests/rp2040/usb_get_descriptor/usb_get_descriptor_test.c"
}

compile_test "$topsrc/sys/arch/rp2040/dev" "$work/usb-get-descriptor-test"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-get-descriptor-test"

sed 's/request_type != 0x80U/request_type != 0x80U \&\& request_type != 0x00U/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/type-mutant/usb.h"
compile_test "$work/type-mutant" "$work/usb-get-descriptor-type-mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-get-descriptor-type-mutant" >"$work/type-mutant.log" 2>&1; then
	echo "usb_get_descriptor: FAIL: invalid request-type mutation was accepted" >&2
	exit 1
fi
echo "usb_get_descriptor: PASS (invalid request-type mutation rejected)"

sed 's/descriptor_index < USB_STRING_DESCRIPTOR_COUNT/descriptor_index <= USB_STRING_DESCRIPTOR_COUNT/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/index-mutant/usb.h"
compile_test "$work/index-mutant" "$work/usb-get-descriptor-index-mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-get-descriptor-index-mutant" >"$work/index-mutant.log" 2>&1; then
	echo "usb_get_descriptor: FAIL: nonexistent string index mutation was accepted" >&2
	exit 1
fi
echo "usb_get_descriptor: PASS (nonexistent string index mutation rejected)"

sed 's/index == USB_STRING_LANGID_EN_US)/index == USB_STRING_LANGID_EN_US || index == 0xffffU)/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/langid-mutant/usb.h"
compile_test "$work/langid-mutant" "$work/usb-get-descriptor-langid-mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-get-descriptor-langid-mutant" >"$work/langid-mutant.log" 2>&1; then
	echo "usb_get_descriptor: FAIL: unsupported LANGID mutation was accepted" >&2
	exit 1
fi
echo "usb_get_descriptor: PASS (unsupported LANGID mutation rejected)"
