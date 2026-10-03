#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-reset.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/mutant"

${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$topsrc/sys/arch/rp2040/dev" \
	-o "$work/usb-reset-test" "$topsrc/tests/rp2040/usb_reset/usb_reset_test.c"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-reset-test"

sed 's/pin >= USB_RESET_GPIO_COUNT/pin >= 128U/' \
	"$topsrc/sys/arch/rp2040/dev/usb_reset.h" >"$work/mutant/usb_reset.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/usb-reset-mutant" "$topsrc/tests/rp2040/usb_reset/usb_reset_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-reset-mutant" >"$work/mutant.log" 2>&1; then
	echo "usb_reset: FAIL: GPIO-count mutation was accepted" >&2
	exit 1
fi
echo "usb_reset: PASS (GPIO-count mutation rejected)"
