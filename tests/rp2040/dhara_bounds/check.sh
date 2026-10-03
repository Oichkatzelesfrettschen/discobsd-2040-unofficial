#!/bin/sh

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dhara-bounds.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
	echo "dhara_bounds: FAIL: $*" >&2
	exit 1
}

compile_test()
{
	journal_source=$1
	binary=$2
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined -ffunction-sections -fdata-sections \
	    -I"$topsrc/sys/arch" -I"$topsrc/sys/arch/rp2040/dhara" \
	    -Wl,--gc-sections \
	    -o "$binary" "$topsrc/tests/rp2040/dhara_bounds/dhara_bounds_test.c" \
	    "$journal_source" || fail "strict C17 sanitizer build"
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

compile_test "$topsrc/sys/arch/rp2040/dhara/journal.c" \
	"$work/dhara-bounds"
"$work/dhara-bounds" || fail "valid and malformed-page contracts"

"$PYTHON" - "$topsrc/sys/arch/rp2040/dhara/journal.c" \
	"$work/journal-without-metadata-bound.c" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
mutated_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
old = "if (!valid_page) {"
new = "if (0 && !valid_page) {"
if source.count(old) != 1:
    raise SystemExit("metadata-bound mutation anchor count differs")
mutated_path.write_text(source.replace(old, new), encoding="utf-8")
PY

compile_test "$work/journal-without-metadata-bound.c" \
	"$work/dhara-bounds-mutant"
if ("$work/dhara-bounds-mutant" corrupt >"$work/mutant.log" 2>&1) \
	2>/dev/null; then
	fail "metadata-bound mutation was accepted"
fi
if ! grep -q 'AddressSanitizer: stack-buffer-overflow' "$work/mutant.log"; then
	cat "$work/mutant.log" >&2
	fail "metadata-bound mutation did not reproduce the buffer overread"
fi

echo "dhara_bounds: PASS (valid pages, malformed metadata pages, calibrated overread mutation)"
