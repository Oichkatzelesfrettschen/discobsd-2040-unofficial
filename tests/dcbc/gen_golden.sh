#!/bin/sh
# Regenerate golden.json from the pinned OpenBSD sources: dc against the host
# libcrypto, bc against the host libedit and libcrypto, both with the
# compatibility header of tests/libbn/dc-differential. The sources are the
# files of docs/research/openbsd-dc-bn-port.md, fetched and hash-checked
# there; this script runs them, so it is an owner-approved manual step and
# not a CI gate.
#
# usage: gen_golden.sh DC_SRC BC_SRC OUTPUT
set -eu
dcsrc=$1
bcsrc=$2
output=$3
here=$(cd "$(dirname "$0")" && pwd)
differential=$here/../libbn/dc-differential
: "${PYTHON:?set PYTHON to the intended interpreter}"
work=$(mktemp -d "${TMPDIR:-/tmp}/dcbc-golden.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/dc" "$work/bc" "$work/src"

dcfiles="$dcsrc/main.c $dcsrc/dc.c $dcsrc/bcode.c $dcsrc/inout.c $dcsrc/mem.c $dcsrc/stack.c"
# shellcheck disable=SC2086
cc -O1 -w -include "$differential/obsd_compat.h" -I"$dcsrc" \
    -o "$work/dc/dc" $dcfiles -lcrypto

# bc keeps the real dup(); only bcode.c needs obsd_compat.h's rename of it.
printf '#include "%s/obsd_compat.h"\n#undef dup\n' "$differential" > "$work/bc_compat.h"
cp "$bcsrc"/bc.y "$bcsrc"/scan.l "$bcsrc"/tty.c "$bcsrc"/extern.h \
    "$bcsrc"/bc.library "$work/src/"
sed "s|\"/usr/share/misc/bc.library\"|\"$work/src/bc.library\"|" \
    "$bcsrc/pathnames.h" > "$work/src/pathnames.h"
(cd "$work/src" && bison -y -o bc.c -d bc.y 2>/dev/null && flex -o scan.c scan.l)
dcobjs=
for f in dc bcode inout mem stack; do
	cc -O1 -w -include "$differential/obsd_compat.h" -I"$dcsrc" \
	    -c -o "$work/dc_$f.o" "$dcsrc/$f.c"
	dcobjs="$dcobjs $work/dc_$f.o"
done
# shellcheck disable=SC2086
cc -O1 -w -include "$work/bc_compat.h" -I"$work/src" -I"$dcsrc" \
    -o "$work/bc/bc" "$work/src/bc.c" "$work/src/scan.c" "$work/src/tty.c" \
    $dcobjs -ledit -lcrypto

"$PYTHON" -I "$here/run_cases.py" --dc "$work/dc/dc" --bc "$work/bc/bc" \
    --write-golden "$output" --timeout 10
