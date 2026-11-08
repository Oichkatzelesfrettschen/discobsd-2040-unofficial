#!/bin/sh
# Build the dispatcher template and the time source into one host
# executable named utilbox, with a symlink named time, in OUTDIR.
# Usage: build.sh CC TOPSRC TIME_SOURCE OUTDIR
set -eu

cc=$1
top=$2
source_file=$3
out=$4
here=$(cd "$(dirname "$0")" && pwd)
flags="-std=gnu17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror"

mkdir -p "$out"
# The same substitution sbin/utilbox/Makefile performs for one member.
awk '
	$0 == "@DECLS@" { print "int time_main(int, char **);"; next }
	$0 == "@TABLE@" { print "\t{ \"time\", time_main },"; next }
	{ print }
' "$top/sbin/utilbox/utilbox.c.in" >"$out/utilbox.c"

# shellcheck disable=SC2086
"$cc" $flags -c -o "$out/utilbox.o" "$out/utilbox.c"
# shellcheck disable=SC2086
"$cc" $flags -Dmain=time_main -Dwait3=time_test_wait3 \
	-c -o "$out/time.o" "$source_file"
# shellcheck disable=SC2086
"$cc" $flags -c -o "$out/wait3_shim.o" "$here/wait3_shim.c"
# shellcheck disable=SC2086
"$cc" $flags -o "$out/probe" "$here/probe.c"
"$cc" -o "$out/utilbox" "$out/utilbox.o" "$out/time.o" "$out/wait3_shim.o"
ln -sf utilbox "$out/time"
