#!/bin/sh
# Build lcdterm_test against one mutated PicoCalc console source and
# require the test to fail.
#
# usage: mutant.sh CC CFLAGS DEVDIR FILE SED-EXPRESSION
set -eu

cc=$1
cflags=$2
dev=$3
file=$4
expr=$5

# Each invocation owns its directory, so make -j runs mutants in parallel.
work=$(mktemp -d "${TMPDIR:-/tmp}/picocalc-mutant.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$dev/lcdterm.c" "$dev/lcdterm.h" "$dev/lcdfont.c" "$dev/lcdfont.h" \
    "$dev/picokbd_map.c" "$dev/picokbd_map.h" "$work/"
sed "$expr" "$dev/$file" >"$work/$file"
if cmp -s "$dev/$file" "$work/$file"; then
	echo "mutant.sh: '$expr' changed nothing in $file" >&2
	exit 1
fi
# shellcheck disable=SC2086
"$cc" $cflags -I"$work" -o "$work/lcdterm_test" lcdterm_test.c \
    "$work/lcdterm.c" "$work/lcdfont.c" "$work/picokbd_map.c"
if "$work/lcdterm_test" >/dev/null 2>&1; then
	echo "mutant.sh: test passed the mutant '$expr' of $file" >&2
	exit 1
fi
echo "mutant rejected: $file '$expr'"
