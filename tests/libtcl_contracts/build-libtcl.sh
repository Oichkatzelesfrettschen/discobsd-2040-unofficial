#!/bin/sh
# Build lib/libtcl for the host into a private directory, optionally after
# mutating its source. Each mutation is a file name and a sed expression;
# one that leaves its file unchanged is an error, so a negative control can
# never test the unmodified library under a mutated name.
#
# usage: build-libtcl.sh OUTDIR TOPSRC CC "CFLAGS" [FILE SED-EXPRESSION]...
set -eu

if [ $# -lt 4 ] || [ $(( ($# - 4) % 2 )) -ne 0 ]; then
	echo "usage: $0 outdir topsrc cc cflags [file sed-expression]..." >&2
	exit 2
fi
out=$1
topsrc=$2
cc=$3
cflags=$4
shift 4

rm -rf "$out"
mkdir -p "$out/src" "$out/include/tcl" "$out/obj"
cp "$topsrc"/lib/libtcl/*.c "$topsrc"/lib/libtcl/*.h "$out/src/"
# <tcl/tcl.h> alone: the tree's include directory also carries a libc whose
# headers would displace the host's.
cp "$topsrc/include/tcl/tcl.h" "$out/include/tcl/"

while [ $# -gt 0 ]; do
	file=$out/src/$1
	sed -e "$2" "$file" > "$file.mutated"
	if cmp -s "$file" "$file.mutated"; then
		echo "build-libtcl: '$2' does not change $1" >&2
		exit 1
	fi
	mv "$file.mutated" "$file"
	shift 2
done

for source in "$out"/src/*.c; do
	object=$out/obj/$(basename "$source" .c).o
	# cflags is the Makefile's fixed option list, split into words here.
	# shellcheck disable=SC2086
	"$cc" $cflags -I"$out/include" -c -o "$object" "$source"
done
ar cr "$out/libtcl.a" "$out"/obj/*.o
