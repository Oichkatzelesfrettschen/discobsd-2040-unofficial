#!/bin/sh
# Host check of the ADC conversions in sys/arch/rp2040/dev/adc.h against
# the datasheet formula, and of three mutants that must fail it.

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/adc-conversion.XXXXXX")
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

build()
{
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-Wstrict-prototypes -Wold-style-definition \
		-fsanitize=address,undefined -I"$1" -o "$2" \
		"$topsrc/tests/rp2040/adc_conversion/adc_conversion_test.c"
}
run()
{
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$@"
}

build "$topsrc/sys/arch/rp2040/dev" "$work/adc-conversion-test"
run "$work/adc-conversion-test"

mutant()
{
	name=$1
	expression=$2
	mkdir "$work/$name"
	sed "$expression" "$topsrc/sys/arch/rp2040/dev/adc.h" >"$work/$name/adc.h"
	if cmp -s "$topsrc/sys/arch/rp2040/dev/adc.h" "$work/$name/adc.h"; then
		echo "adc_conversion: FAIL: $name mutation changed nothing" >&2
		exit 1
	fi
	build "$work/$name" "$work/adc-conversion-$name"
	if run "$work/adc-conversion-$name" >"$work/$name.log" 2>&1; then
		echo "adc_conversion: FAIL: $name mutation was accepted" >&2
		exit 1
	fi
	echo "adc_conversion: PASS ($name mutation rejected)"
}
mutant slope 's/1721/1720/g'
mutant offset 's/- 706000/- 700000/'
mutant divider 's/raw \* 9900U/raw * 6600U/'
