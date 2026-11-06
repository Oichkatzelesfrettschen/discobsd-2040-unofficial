#!/bin/sh
# Differential run of OpenBSD dc linked against libbn and against the host
# libcrypto. Needs the OpenBSD dc sources (DC_SRC, the pinned commit in
# docs/research/openbsd-dc-bn-port.md) and a host libcrypto; it runs the
# downloaded dc, so it is an owner-approved manual check, not a CI gate.
#
# usage: run.sh DC_SRC [PROGRAMS]
set -eu
src=$1
count=${2:-3000}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dc-differential.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
: "${PYTHON:?set PYTHON to the intended interpreter}"
mkdir "$work/a" "$work/b"
files="$src/main.c $src/dc.c $src/bcode.c $src/inout.c $src/mem.c $src/stack.c"
flags="-O1 -w -include $here/obsd_compat.h -I$src"
# shellcheck disable=SC2086
cc $flags -o "$work/a/dc" $files -lcrypto
# shellcheck disable=SC2086
cc $flags -I"$here/bnshim" -o "$work/b/dc" $files "$root/lib/libbn/bn.c"
"$PYTHON" -I "$here/gen_corpus.py" "$count" > "$work/corpus"
n=0
diffs=0
while IFS= read -r prog; do
	n=$((n + 1))
	printf '%s\n' "$prog" > "$work/in"
	ra=0; (cd "$work/a" && timeout 60 ./dc < "$work/in" > "$work/oa" 2>&1) || ra=$?
	rb=0; (cd "$work/b" && timeout 60 ./dc < "$work/in" > "$work/ob" 2>&1) || rb=$?
	if [ "$ra" != "$rb" ] || ! cmp -s "$work/oa" "$work/ob"; then
		diffs=$((diffs + 1))
		printf 'DIFF %d rc=%s/%s: %s\n' "$n" "$ra" "$rb" "$prog"
	fi
done < "$work/corpus"
echo "programs=$n diffs=$diffs"
