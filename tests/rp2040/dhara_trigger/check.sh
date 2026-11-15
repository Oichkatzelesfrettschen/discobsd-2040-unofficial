#!/bin/sh
#
# Gate for Dhara's collector trigger (auto_gc() in sys/arch/rp2040/dhara/
# map.c, sys/arch/rp2040/doc/research/dhara-gc-trigger.md). The production
# map.c, compiled unmodified, must survive the adversarial fill-then-rewrite
# run without a journal-full error and without growing into the safety
# margin, keep every sector across a reset with resume after each rewrite,
# and hold uniform random rewrites at 912 live sectors under 5.20 pages per
# write. Two mutated copies calibrate the verdict: the stock trigger
# (dhara_map_capacity()) must exceed the ceiling, and a fixed trigger of
# 1,150 pages must fill the journal.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
here=$topsrc/tests/rp2040/dhara_trigger
dhara=$topsrc/sys/arch/rp2040/dhara
work=$(mktemp -d "${TMPDIR:-/tmp}/dhara-trigger.XXXXXX")
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

CEILING=520

fail()
{
	echo "dhara_trigger: FAIL: $*" >&2
	exit 1
}

# build MAP BINARY: the model linked against MAP and the vendored journal,
# as strict C17 under the address and undefined-behavior sanitizers.
build()
{
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -fsanitize=address,undefined \
	    -I"$dhara" -I"$topsrc/sys/arch" -o "$2" "$here/trigger_model.c" \
	    "$dhara/error.c" "$dhara/journal.c" "$1" ||
		fail "strict C17 sanitizer build with $1"
}

# mutate OUTPUT OLD NEW: OUTPUT is map.c with its one OLD replaced. The copy
# lives beside map.c's headers only through -I, so it includes them by name.
mutate()
{
	"$PYTHON" - "$dhara/map.c" "$1" "$2" "$3" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old, new = sys.argv[3], sys.argv[4]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count differs: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
}

# expect_fail BINARY LABEL ARGS...: the run exits 1, the assertion status,
# and prints a FAIL line beginning with LABEL.
expect_fail()
{
	binary=$1
	label=$2
	shift 2
	status=0
	"$binary" "$@" >"$work/mutant.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ] || ! grep -q "^FAIL $label" "$work/mutant.log"; then
		cat "$work/mutant.log" >&2
		fail "$binary $* exited $status without FAIL $label"
	fi
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

build "$dhara/map.c" "$work/model"
"$work/model" adversary 30000 0 || fail "adversarial rewrites"
"$work/model" adversary 2000 1 || fail "adversarial rewrites with resets"
"$work/model" random 912 20000 "$CEILING" || fail "uniform rewrite ceiling"

mutate "$work/map-stock.c" \
	'	if (dhara_journal_size(&m->journal) < gc_trigger(m))' \
	'	if ((void)gc_trigger, dhara_journal_size(&m->journal) <
	    dhara_map_capacity(m))'
build "$work/map-stock.c" "$work/model-stock"
expect_fail "$work/model-stock" ceiling random 912 20000 "$CEILING"

mutate "$work/map-late.c" \
	'	return cap - reserve - safety_margin;
}

static int auto_gc' \
	'	return (void)reserve, (void)safety_margin, 1150;
}

static int auto_gc'
build "$work/map-late.c" "$work/model-late"
expect_fail "$work/model-late" write adversary 30000 0

echo "dhara_trigger: PASS (adversary within the margin, resets keep every" \
	"sector, uniform rewrites under $CEILING/100 pages; stock-trigger and" \
	"fixed-1150 mutations rejected)"
