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
mkdir "$work/mutant" "$work/zeros" "$work/capacity" "$work/serial"

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

# Each identity mutant must fail the same test: an all-zero reply accepted,
# any capacity byte accepted, and the serial suffix losing its hyphen.
identity_mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$topsrc/sys/arch/rp2040/dev/flash.h" \
	    >"$work/$name/flash.h"
	if cmp -s "$topsrc/sys/arch/rp2040/dev/flash.h" "$work/$name/flash.h"; then
		echo "flash_device_bounds: FAIL: $name mutation changed nothing" >&2
		exit 1
	fi
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-Wstrict-prototypes -Wold-style-definition \
		-fsanitize=address,undefined -I"$work/$name" \
		-o "$work/flash-identity-$name" \
		"$topsrc/tests/rp2040/flash_device_bounds/flash_device_bounds_test.c"
	if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$work/flash-identity-$name" >"$work/$name.log" 2>&1; then
		echo "flash_device_bounds: FAIL: $name mutation was accepted" >&2
		exit 1
	fi
	echo "flash_device_bounds: PASS ($name mutation rejected)"
}
identity_mutant zeros 's/return zeros != n && ones != n;/return zeros != n + 1 \&\& ones != n;/'
identity_mutant capacity 's/code < 32 && (1UL << code) == FLASH_TOTAL_BYTES/code < 32/'
identity_mutant serial 's/i = 0;$/i = 1;/'
