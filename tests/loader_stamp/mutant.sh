#!/bin/sh
# Copy the flash sources, apply one edit, and require the gate to fail on the
# result. Usage: mutant.sh CC SRCDIR FILE SED-SCRIPT
# SRCDIR is sys/arch/rp2040/dev. An edit that changes nothing, or a mutant
# that does not compile, is an error (exit 2), not a kill; a mutant the gate
# accepts fails this script (exit 1).
set -eu

cc=$1
src=$2
file=$3
script=$4
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/loader-stamp-mutant.XXXXXX")
cleanup()
{
	trap - EXIT HUP INT TERM
	find "$work" -depth -type f -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

mkdir -p "$work/rp2040/dev"
cp "$src"/flash.h "$src"/flash.c "$src"/flash_swap.h "$src"/flash_swap.c \
    "$src"/loader_stamp.h "$work/rp2040/dev/"
sed "$script" "$work/rp2040/dev/$file" > "$work/edited"
if cmp -s "$work/edited" "$work/rp2040/dev/$file"; then
	echo "mutant edit did not change $file: $script" >&2
	exit 2
fi
mv "$work/edited" "$work/rp2040/dev/$file"

if ! "$cc" -std=gnu17 -O1 -w -I"$work" -o "$work/test" \
    "$here/loader_stamp_test.c" "$work/rp2040/dev/flash_swap.c" \
    > "$work/cc.out" 2>&1; then
	cat "$work/cc.out" >&2
	echo "mutant does not compile: $file: $script" >&2
	exit 2
fi
if "$work/test" > "$work/out" 2>&1 &&
    sh "$here/check_choke.sh" "$work/rp2040/dev/flash.c" >> "$work/out" 2>&1
then
	echo "mutant survived: $file: $script" >&2
	exit 1
fi
echo "mutant killed: $file: $script ($(grep -c 'FAIL' "$work/out" || true) failing lines)"
