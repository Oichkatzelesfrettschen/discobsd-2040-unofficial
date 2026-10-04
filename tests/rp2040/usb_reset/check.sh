#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-reset.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

compile_test()
{
	output=$1
	include_dir=$2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$include_dir" \
	    -o "$output" "$topsrc/tests/rp2040/usb_reset/usb_reset_test.c"
}

expect_rejected()
{
	binary=$1
	label=$2
	if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	    "$binary" >"$work/$label.log" 2>&1; then
		echo "usb_reset: FAIL: $label mutation was accepted" >&2
		exit 1
	fi
	echo "usb_reset: PASS ($label mutation rejected)"
}

check_dispatch_contract()
{
	source_file=$1
	awk '
	/else if \(\(kind == 0x20 \|\| kind == 0x40\)/ {
		candidate = 1
		found = 1
	}
	candidate && /usb_reset_request_valid\(type, request, wvalue, windex,/ {
		validated = NR
	}
	candidate && /usb_ep0_stall\(\);/ && stalled == 0 {
		stalled = NR
	}
	candidate && /switch \(request\)/ {
		switch_line = NR
	}
	candidate && /} else if \(kind == 0x20\)/ {
		candidate = 0
	}
	END {
		exit !(found && validated && stalled && switch_line &&
		    validated < stalled && stalled < switch_line)
	}' "$source_file" || return 1
	grep -Fq 'rom_reset(gpio_mask, wvalue & USB_RESET_INTERFACE_DISABLE_MASK);' \
	    "$source_file"
}

compile_test "$work/usb-reset-test" "$topsrc/sys/arch/rp2040/dev"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-reset-test"

check_dispatch_contract "$topsrc/sys/arch/rp2040/dev/usb.c"
echo "usb_reset: PASS (dispatch validates and stalls before reset action)"
sed 's/usb_reset_request_valid(type, request, wvalue, windex,/usb_reset_request_valid_mutant(type, request, wvalue, windex,/' \
	"$topsrc/sys/arch/rp2040/dev/usb.c" >"$work/dispatch-mutant.c"
if check_dispatch_contract "$work/dispatch-mutant.c"; then
	echo "usb_reset: FAIL: unguarded dispatch mutation was accepted" >&2
	exit 1
fi
echo "usb_reset: PASS (unguarded dispatch mutation rejected)"
sed 's/rom_reset(gpio_mask, wvalue & USB_RESET_INTERFACE_DISABLE_MASK);/rom_reset(gpio_mask, wvalue \& 0x007fU);/' \
	"$topsrc/sys/arch/rp2040/dev/usb.c" >"$work/mask-mutant.c"
if check_dispatch_contract "$work/mask-mutant.c"; then
	echo "usb_reset: FAIL: widened ROM mask mutation was accepted" >&2
	exit 1
fi
echo "usb_reset: PASS (widened ROM mask mutation rejected)"

mkdir "$work/mutant-gpio" "$work/mutant-type" "$work/mutant-length" \
	"$work/mutant-value" "$work/mutant-activity-enable"
sed 's/pin >= USB_RESET_GPIO_COUNT/pin >= 128U/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" \
	>"$work/mutant-gpio/usb_reset.h"
compile_test "$work/usb-reset-mutant-gpio" "$work/mutant-gpio"
expect_rejected "$work/usb-reset-mutant-gpio" gpio-count

sed 's/request_type != USB_RESET_REQUEST_TYPE/request_type > 0xffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" \
	>"$work/mutant-type/usb_reset.h"
compile_test "$work/usb-reset-mutant-type" "$work/mutant-type"
expect_rejected "$work/usb-reset-mutant-type" request-type

sed 's/length != 0U/length > 0xffffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" \
	>"$work/mutant-length/usb_reset.h"
compile_test "$work/usb-reset-mutant-length" "$work/mutant-length"
expect_rejected "$work/usb-reset-mutant-length" transfer-length

sed 's/~USB_RESET_BOOTSEL_VALUE_MASK/~0xffffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" \
	>"$work/mutant-value/usb_reset.h"
compile_test "$work/usb-reset-mutant-value" "$work/mutant-value"
expect_rejected "$work/usb-reset-mutant-value" reserved-value-bits

sed 's/~USB_RESET_BOOTSEL_VALUE_MASK/~0xfe83U/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" \
	>"$work/mutant-activity-enable/usb_reset.h"
compile_test "$work/usb-reset-mutant-activity-enable" \
	"$work/mutant-activity-enable"
expect_rejected "$work/usb-reset-mutant-activity-enable" \
	activity-enable-bit
