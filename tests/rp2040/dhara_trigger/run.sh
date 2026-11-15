#!/bin/sh
#
# Measurement, not a gate: Dhara's collector trigger in the host model
# (docs/research/dhara-gc-trigger.md). The vendored map.c is copied with
# auto_gc()'s trigger replaced by trigger_model.c's dhara_gc_trigger(); the
# vendored sources stay unchanged. Prints the random-rewrite matrix and the
# adversarial runs for each trigger.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
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

"$PYTHON" - "$dhara/map.c" "$work/map_trigger.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = "\tif (dhara_journal_size(&m->journal) < dhara_map_capacity(m))\n\t\treturn 0;"
if source.count(old) != 1:
    raise SystemExit("auto_gc() trigger anchor not found once")
source = source.replace(old, old.replace("dhara_map_capacity(m)", "dhara_gc_trigger(m)"))
source = source.replace(
    '#include "map.h"',
    '#include "map.h"\n\ndhara_page_t dhara_gc_trigger(const struct dhara_map *m);',
    1,
)
Path(sys.argv[2]).write_text(source, encoding="utf-8")
PY
${CC:-cc} -std=c17 -O2 -Wall -Wextra -Werror -I"$dhara" \
    -I"$topsrc/sys/arch" -o "$work/trigger_model" \
    "$topsrc/tests/rp2040/dhara_trigger/trigger_model.c" \
    "$dhara/error.c" "$dhara/journal.c" "$work/map_trigger.c"

for trigger in stock fixed:1043 prop:5 prop:4 fixed:1150; do
	for live in 758 850 912 989; do
		printf '%s ' "$trigger"
		"$work/trigger_model" random "$trigger" "$live" 20000
	done
done
for trigger in stock fixed:1043 prop:5 prop:4 fixed:1150 fixed:1250; do
	printf '%s ' "$trigger"
	"$work/trigger_model" adversary "$trigger" 30000 0 || true
	printf '%s ' "$trigger"
	"$work/trigger_model" adversary "$trigger" 3000 1 || true
done
