#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-set-configuration.XXXXXX")
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

${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$topsrc/sys/arch/rp2040/dev" \
	-o "$work/usb-set-configuration-test" \
	"$topsrc/tests/rp2040/usb_set_configuration/usb_set_configuration_test.c"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-set-configuration-test"

sed 's/value == USB_CONFIGURATION_VALUE/value != 2U/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/usb-set-configuration-mutant" \
	"$topsrc/tests/rp2040/usb_set_configuration/usb_set_configuration_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-set-configuration-mutant" >"$work/mutant.log" 2>&1; then
	echo "usb_set_configuration: FAIL: arbitrary configuration value was accepted" >&2
	exit 1
fi
echo "usb_set_configuration: PASS (invalid-value mutation rejected)"
