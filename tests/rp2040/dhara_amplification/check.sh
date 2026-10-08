#!/bin/sh

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
dhara=$topsrc/sys/arch/rp2040/dhara
table=$topsrc/sys/arch/rp2040/doc/dhara-geometry.txt
work=$(mktemp -d "${TMPDIR:-/tmp}/dhara-amplification.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
	echo "dhara_amplification: FAIL: $*" >&2
	exit 1
}

# build JOURNAL MAP BINARY: the oracle linked against the given journal and
# map sources, as strict C17 under the address and undefined-behavior
# sanitizers.
build()
{
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined \
	    -I"$topsrc/sys/arch" -I"$dhara" -o "$3" \
	    "$topsrc/tests/rp2040/dhara_amplification/dhara_amplification_test.c" \
	    "$dhara/error.c" "$1" "$2" || fail "strict C17 sanitizer build"
}

# geometry_matches BINARY TABLE: the derived geometry equals the table's
# "key value" lines once comments and blank lines are removed.
geometry_matches()
{
	"$1" geometry >"$work/geometry.out" || fail "$1 geometry did not run"
	sed -e 's/#.*//' -e 's/[[:space:]][[:space:]]*/ /g' \
	    -e 's/^ //' -e 's/ $//' -e '/^$/d' "$2" >"$work/geometry.want"
	cmp -s "$work/geometry.want" "$work/geometry.out"
}

# mutate SOURCE OUTPUT OLD NEW: OUTPUT is SOURCE with its one OLD replaced.
mutate()
{
	"$PYTHON" - "$1" "$2" "$3" "$4" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old, new = sys.argv[3], sys.argv[4]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count differs: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
}

# rejects BINARY LABEL...: the traces exit 1, the assertion status, and
# report a failure for every LABEL. A sanitizer abort or another status is
# a crash, which does not count as calibration.
rejects()
{
	binary=$1
	shift
	status=0
	"$binary" >"$work/mutant.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ]; then
		cat "$work/mutant.log" >&2
		fail "$binary exited $status, not the assertion status 1"
	fi
	for label in "$@"; do
		grep -q "^FAIL $label:" "$work/mutant.log" ||
			fail "$binary did not fail $label"
	done
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

build "$dhara/journal.c" "$dhara/map.c" "$work/oracle"
if ! geometry_matches "$work/oracle" "$table"; then
	diff -u "$work/geometry.want" "$work/geometry.out" >&2 || true
	fail "derived geometry differs from $table"
fi
"$work/oracle" >"$work/oracle.log" 2>&1 || {
	cat "$work/oracle.log" >&2
	fail "trace expectations"
}
cat "$work/oracle.log"

# A table entry disagreeing with flash.h must fail the comparison.
sed 's/^unit_bytes 1024$/unit_bytes 256/' "$table" >"$work/bad-table.txt"
if cmp -s "$table" "$work/bad-table.txt"; then
	fail "bad-table calibration anchor not found"
fi
if geometry_matches "$work/oracle" "$work/bad-table.txt"; then
	fail "a disagreeing geometry table was accepted"
fi

# A journal whose checkpoint group is two pages must fail both predictions
# and the geometry table.
mutate "$dhara/journal.c" "$work/journal-ppc1.c" \
	'	return ppc;' '	return 1;'
build "$work/journal-ppc1.c" "$dhara/map.c" "$work/oracle-ppc1"
rejects "$work/oracle-ppc1" trace-a trace-b
if geometry_matches "$work/oracle-ppc1" "$table"; then
	fail "log2_ppc = 1 passed the geometry table"
fi

# A sync that leaves the open group unpadded must fail the isolated trace.
mutate "$dhara/map.c" "$work/map-nosync.c" \
	'while (!dhara_journal_is_clean(&m->journal)) {' \
	'while (0 && !dhara_journal_is_clean(&m->journal)) {'
build "$dhara/journal.c" "$work/map-nosync.c" "$work/oracle-nosync"
rejects "$work/oracle-nosync" trace-a

echo "dhara_amplification: PASS (geometry table, isolated and batched" \
	"predictions, calibrated table, group-size and sync-loop mutations)"
