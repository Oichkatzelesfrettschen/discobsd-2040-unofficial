#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/flash-device-bounds.XXXXXX")
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
	-o "$work/flash-device-bounds-test" \
	"$topsrc/tests/rp2040/flash_device_bounds/flash_device_bounds_test.c"
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/flash-device-bounds-test"

sed 's/unit < drive_count/unit <= drive_count/' \
	"$topsrc/sys/arch/rp2040/dev/flash.h" >"$work/mutant/flash.h"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wstrict-prototypes -Wold-style-definition \
	-fsanitize=address,undefined -I"$work/mutant" \
	-o "$work/flash-device-bounds-mutant" \
	"$topsrc/tests/rp2040/flash_device_bounds/flash_device_bounds_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/flash-device-bounds-mutant" >"$work/mutant.log" 2>&1; then
	echo "flash_device_bounds: FAIL: invalid-drive mutation was accepted" >&2
	exit 1
fi
echo "flash_device_bounds: PASS (invalid-drive mutation rejected)"
