#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/clock-catchup.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	find "$work" -depth \( -type f -o -type l \) -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# Include roots that name the production header and the two kernel headers
# the test needs without exposing the kernel's <sys/types.h> to the host.
# Only machparam.h enters machine/: a host libc such as macOS's includes
# <machine/signal.h> from <stdlib.h>, which must resolve to the host's.
# The header under test is reached through "$1"/rp2040/rp2040.
make_include_root()
{
	root=$1
	header_dir=$2
	mkdir -p "$root/sys" "$root/rp2040" "$root/machine"
	ln -s "$topsrc/sys/arch/rp2040/include/machparam.h" "$root/machine/machparam.h"
	ln -s "$topsrc/sys/sys/callout.h" "$root/sys/callout.h"
	ln -s "$header_dir" "$root/rp2040/rp2040"
}

compile_test()
{
	output=$1
	include_root=$2
	${CC:-cc} -std=gnu17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -I"$include_root" \
	    -o "$output" "$topsrc/tests/rp2040/clock_catchup/clock_catchup_test.c"
}

run_test()
{
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$@"
}

# Mutate the header with sed, require the text to change, and require the
# test to reject the result.
expect_mutant_rejected()
{
	label=$1
	expression=$2
	mkdir -p "$work/$label/headers"
	sed "$expression" "$topsrc/sys/arch/rp2040/rp2040/clock_catchup.h" \
	    >"$work/$label/headers/clock_catchup.h"
	if cmp -s "$topsrc/sys/arch/rp2040/rp2040/clock_catchup.h" \
	    "$work/$label/headers/clock_catchup.h"; then
		echo "clock_catchup: FAIL: $label mutation did not apply" >&2
		exit 1
	fi
	make_include_root "$work/$label/inc" "$work/$label/headers"
	compile_test "$work/$label/test" "$work/$label/inc"
	if run_test "$work/$label/test" >"$work/$label/log" 2>&1; then
		echo "clock_catchup: FAIL: $label mutation was accepted" >&2
		exit 1
	fi
	echo "clock_catchup: PASS ($label mutation rejected)"
}

# In the named function of file, every splx(s) is preceded by the call.
# usage: check_call file function call
check_call()
{
	awk -v fn="$2" -v call="$3" '
	$0 ~ "^" fn "\\(" { inside = 1; next }
	inside && /^}/ { inside = 0 }
	inside && $0 ~ "^[ \t]*" call ";$" { armed = 1; next }
	inside && /^[ \t]*splx\(s\);$/ {
		seen++
		if (!armed)
			bad = 1
		armed = 0
		next
	}
	inside && /^[ \t]*return/ { armed = 0 }
	END { exit !(seen > 0 && !bad) }' "$1"
}

# The named function of file contains a line that is exactly the call.
# usage: check_contains file function call
check_contains()
{
	awk -v fn="$2" -v call="$3" '
	$0 ~ "^" fn "\\(" { inside = 1; next }
	inside && /^}/ { inside = 0 }
	inside && $0 ~ "^[ \t]*" call ";$" { found = 1 }
	END { exit !found }' "$1"
}

mkdir -p "$work/real"
make_include_root "$work/real/inc" "$topsrc/sys/arch/rp2040/rp2040"
compile_test "$work/real/test" "$work/real/inc"
run_test "$work/real/test"

expect_mutant_rejected no-catch-up \
    's/owed = due > held ? due - held : 0U;/owed = due > held ? 0U : 0U;/'
expect_mutant_rejected pending-tick-double-count \
    's/held = hw_pending ? 1U : 0U;/held = hw_pending ? 0U : 0U;/'
expect_mutant_rejected stamp-not-advanced \
    's/c->stamp_us += owed \* tick_us;/c->stamp_us += owed * 0U * tick_us;/'
expect_mutant_rejected timer-wrap-ignored \
    's/due = (unsigned int)(now_us - c->stamp_us) \/ tick_us;/due = (now_us > c->stamp_us ? now_us - c->stamp_us : 0U) \/ tick_us;/'
expect_mutant_rejected seconds-not-carried \
    's/\*seconds += \*tick_counter \/ ticks_per_second;/*seconds += 0;/'
expect_mutant_rejected head-only-callout-decrement \
    's/p->c_time = t - (int)(owed - ahead);/p->c_time = t - (int)owed;/'
expect_mutant_rejected unarmed-clock-counted \
    's/if (!c->armed)/if (0)/'
expect_mutant_rejected pending-read-before-timer \
    's/^[[:space:]]*held = read_pending();$//;s/now = read_timer();/held = read_pending(); now = read_timer();/'

# Source wiring: the production paths reach the catch-up before the mask
# lifts, and the tick handler feeds the record.
flash_c=$topsrc/sys/arch/rp2040/dev/flash.c
for fn in flash_erase flash_program fl_raw flstrategy; do
	if ! check_call "$flash_c" "$fn" 'clock_catchup\\(\\)'; then
		echo "clock_catchup: FAIL: $fn lifts the mask without catch-up" >&2
		exit 1
	fi
done
grep -Fq 'clock_tick_stamp();' "$topsrc/sys/arch/rp2040/rp2040/systick.c"
grep -Fq 'clkcatch.armed = 1;' "$topsrc/sys/arch/rp2040/rp2040/clock.c"

mkdir -p "$work/wiring"
sed '/^\tclock_catchup();$/d' "$flash_c" >"$work/wiring/flash.c"
for fn in flash_erase flash_program fl_raw flstrategy; do
	if check_call "$work/wiring/flash.c" "$fn" 'clock_catchup\\(\\)'; then
		echo "clock_catchup: FAIL: wiring check accepted $fn without catch-up" >&2
		exit 1
	fi
done
echo "clock_catchup: PASS (flash paths call catch-up; wiring check rejects removal)"

# The ticks the catch-up restores also advance the monotonic clock that
# orders ITIMER_REAL deadlines.
clock_c=$topsrc/sys/arch/rp2040/rp2040/clock.c
if ! check_contains "$clock_c" clock_catchup 'monoclock_advance\\(owed\\)'; then
	echo "clock_catchup: FAIL: clock_catchup() leaves the monotonic clock behind" >&2
	exit 1
fi
sed '/^[[:space:]]*monoclock_advance(owed);$/d' "$clock_c" >"$work/wiring/clock.c"
if check_contains "$work/wiring/clock.c" clock_catchup 'monoclock_advance\\(owed\\)'; then
	echo "clock_catchup: FAIL: wiring check accepted catch-up without monoclock_advance" >&2
	exit 1
fi
echo "clock_catchup: PASS (catch-up advances the monotonic clock; wiring check rejects removal)"
echo "clock_catchup: PASS (all mutations rejected)"

# The decision logic of the two board scripts that read the host-board clock
# and the dc and bc console, on synthetic input and a stub console.
"${PYTHON:?set PYTHON to the intended interpreter}" \
    "$topsrc/tests/rp2040/clock_catchup/test_board_scripts.py"
echo "clock_catchup: PASS (board script logic: date parsing, offset bounds, verdicts, workload, framing)"
