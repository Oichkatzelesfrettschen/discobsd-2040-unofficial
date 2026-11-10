#!/bin/sh
# Worst-case stack bound for usr.sbin/v7fs on the RP2040 cross build.
#
# Usage: stack-depth.sh [OUTDIR]
#
# Run from a tree where `bmake MACHINE=rp2040 build` has linked
# usr.sbin/v7fs (its v7fs.dis listing supplies the libc frames). The script
# recompiles the program's thirteen sources with the cross compiler and
# the Makefile's own flags plus -fstack-usage -fcallgraph-info=su into
# OUTDIR ($STACK_OUTDIR, or a fresh mktemp directory; it must be absent or
# empty), collects the address-taken functions from the R_ARM_ABS32
# relocations, bounds each libc callee from the linked listing, and prints
# the deepest chain from main. stack_depth.py documents both analyses.
set -eu

PYTHON=${PYTHON:?set PYTHON to the intended interpreter}
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
src=$top/usr.sbin/v7fs
listing=$src/v7fs.dis
[ -f "$listing" ] || { echo "stack-depth.sh: build usr.sbin/v7fs first ($listing)" >&2; exit 2; }

out=${1:-${STACK_OUTDIR:-}}
if [ -z "$out" ]; then
	out=$(mktemp -d "${TMPDIR:-/tmp}/v7fs-stack.XXXXXX")
elif [ -e "$out" ] && [ -n "$(ls -A "$out")" ]; then
	echo "stack-depth.sh: $out is not empty" >&2
	exit 2
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)

var() {
	(cd "$src" && bmake MACHINE=rp2040 PYTHON="$PYTHON" -V "\${$1}")
}
cc=$(var CC)
cflags="$(var CFLAGS) -fstack-usage -fcallgraph-info=su"
defs="$(var V7FS_DEFS) $(var DISCOBSD_DEFS)"
netbsd_flags=$(var NETBSD_FLAGS)
local_flags=$(var LOCAL_FLAGS)

# shellcheck disable=SC2086 # the flag variables are word lists
for s in $(var NETBSD_SRCS); do
	case $s in
	main.c) f=$top/sbin/newfs_v7fs/main.c ;;
	*) f=$top/sys/fs/v7fs/$s ;;
	esac
	$cc $cflags $defs $netbsd_flags -c -o "$out/${s%.c}.o" -dumpdir "$out/" "$f"
done
# shellcheck disable=SC2086
for s in $(var LOCAL_SRCS); do
	(cd "$src" && $cc $cflags $defs $local_flags -c -o "$out/${s%.c}.o" \
	    -dumpdir "$out/" "$s")
done

for o in "$out"/*.o; do
	arm-none-eabi-readelf -rW "$o" | awk '$3 ~ /ABS32/ { print $5 }'
done | sort -u >"$out/abs32.txt"
arm-none-eabi-nm "$out"/*.o | awk '$2 ~ /^[Tt]$/ { print $3 }' | sort -u \
    >"$out/functions.txt"
comm -12 "$out/abs32.txt" "$out/functions.txt" >"$out/taken.txt"

# Every callee the call-graph files name without a node of their own.
sed -n 's/.*targetname: "\([^"]*\)".*/\1/p' "$out"/*.ci | sort -u \
    >"$out/targets.txt"
sed -n 's/.*node: { title: "\([^"]*\)" label: "[^"]*bytes.*/\1/p' \
    "$out"/*.ci | sort -u >"$out/nodes.txt"
comm -23 "$out/targets.txt" "$out/nodes.txt" | grep -v '^__indirect_call$' \
    >"$out/libc.txt" || true
# shellcheck disable=SC2046 # one argument per libc callee
"$PYTHON" "$here/stack_depth.py" frames --callback _fwalk=fflush \
    "$listing" $(cat "$out/libc.txt") >"$out/extern.txt"

(cd "$src" && arm-none-eabi-gcc --version | head -n 1)
"$PYTHON" "$here/stack_depth.py" chain "$src" main "$out/taken.txt" \
    "$out/extern.txt" "$out"/*.ci
