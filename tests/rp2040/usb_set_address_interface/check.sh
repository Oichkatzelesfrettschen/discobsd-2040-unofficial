#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-set-address-interface.XXXXXX")
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
mkdir "$work/address-mutant" "$work/interface-mutant"

compile_test()
{
	include_directory=$1
	output=$2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$include_directory" \
	    -o "$output" \
	    "$topsrc/tests/rp2040/usb_set_address_interface/usb_set_address_interface_test.c"
}

compile_test "$topsrc/sys/arch/rp2040/dev" "$work/usb-set-requests-test"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-set-requests-test"

sed 's/value <= 0x7fU/value <= 0xffffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/address-mutant/usb.h"
compile_test "$work/address-mutant" "$work/usb-set-address-mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-set-address-mutant" >"$work/address-mutant.log" 2>&1; then
	echo "usb_set_address_interface: FAIL: over-range address mutation was accepted" >&2
	exit 1
fi
echo "usb_set_address_interface: PASS (over-range address mutation rejected)"

sed 's/interface_number < USB_INTERFACE_COUNT/interface_number <= 0xffffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/interface-mutant/usb.h"
compile_test "$work/interface-mutant" "$work/usb-set-interface-mutant"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-set-interface-mutant" >"$work/interface-mutant.log" 2>&1; then
	echo "usb_set_address_interface: FAIL: nonexistent interface mutation was accepted" >&2
	exit 1
fi
echo "usb_set_address_interface: PASS (nonexistent interface mutation rejected)"
