#!/bin/sh
# When the spool file cannot be made, a pipe longer than the memory
# buffer must still answer from the newest bytes and say so. Runs the
# host tail inside bwrap with a read-only /tmp; skips when bwrap is
# missing.
set -eu
TAIL=$1
if ! command -v bwrap >/dev/null 2>&1; then
	echo "ringcheck: bwrap not found, skipped"
	exit 0
fi
work=$(mktemp -d "$PWD/ring.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/ro"
awk 'BEGIN { for (i = 0; i < 500; i++) printf "line%05d %s\n", i, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" }' > "$work/big"
run() {
	bwrap --ro-bind / / --bind "$work" "$work" --dev /dev --proc /proc --ro-bind "$work/ro" /tmp -- \
	    sh -c "cat '$work/big' | '$TAIL' $1 2>'$work/err'"
}
[ "$(run -2 | tail -1)" = "line00499 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" ]
[ "$(run -3r | wc -l)" -eq 3 ]
[ "$(run -r | wc -l)" -eq 100 ]
grep -q "cannot spool standard input" "$work/err"
grep -q "only the last 4096 of 20500 bytes" "$work/err"
echo "ringcheck: ok"
