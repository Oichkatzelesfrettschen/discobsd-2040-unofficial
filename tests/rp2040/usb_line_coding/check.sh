#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-line-coding.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	rm -f "$work/usb-line-coding-test" \
	    "$work/usb-line-coding-mutant" "$work/mutant.log" \
	    "$work/mutant/usb.h"
	rmdir "$work/mutant" "$work"
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir "$work/mutant"

${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$topsrc/sys/arch/rp2040/dev" \
	-o "$work/usb-line-coding-test" \
	"$topsrc/tests/rp2040/usb_line_coding/usb_line_coding_test.c"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-line-coding-test"

sed 's/received_length == USB_CDC_LINE_CODING_SIZE/received_length <= USB_CDC_LINE_CODING_SIZE/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/usb-line-coding-mutant" \
	"$topsrc/tests/rp2040/usb_line_coding/usb_line_coding_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-line-coding-mutant" >"$work/mutant.log" 2>&1; then
	echo "usb_line_coding: FAIL: short-length mutation was accepted" >&2
	exit 1
fi
echo "usb_line_coding: PASS (short-length mutation rejected)"
