#!/bin/sh
set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-cdc-requests.XXXXXX")
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
	-o "$work/usb-cdc-requests-test" \
	"$topsrc/tests/rp2040/usb_cdc_requests/usb_cdc_requests_test.c"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-cdc-requests-test"

sed 's/value <= 0x0003U/value <= 0xffffU/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/usb-cdc-requests-mutant" \
	"$topsrc/tests/rp2040/usb_cdc_requests/usb_cdc_requests_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-cdc-requests-mutant" >"$work/mutant.log" 2>&1; then
	echo "usb_cdc_requests: FAIL: reserved control-line bits were accepted" >&2
	exit 1
fi
echo "usb_cdc_requests: PASS (reserved-bit mutation rejected)"

sed 's/request_type == 0x21U/(request_type == 0x20U || request_type == 0x21U)/' \
	"$topsrc/sys/arch/rp2040/dev/usb.h" >"$work/mutant/usb.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/usb-cdc-request-type-mutant" \
	"$topsrc/tests/rp2040/usb_cdc_requests/usb_cdc_requests_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/usb-cdc-request-type-mutant" >"$work/request-type-mutant.log" 2>&1; then
	echo "usb_cdc_requests: FAIL: class request-type mutation was accepted" >&2
	exit 1
fi
echo "usb_cdc_requests: PASS (request-type mutation rejected)"
