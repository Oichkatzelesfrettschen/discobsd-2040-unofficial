#!/bin/sh
# One mutant of the termios library: apply a sed script to one source file
# of the tree, rebuild the gate against the mutated copy and require that the
# gate fails. A mutation that changes nothing, or that stops the gate from
# compiling, is itself an error, so a mutant counts only when it builds and
# a check rejects it.
#
# usage: mutant.sh LABEL FILE SED-SCRIPT
# environment: HOSTCC TOPSRC BUILD_FLAGS (compile flags, tree include path
# and renames, as the Makefile passes them to the real build)
set -eu

label=$1
file=$2
script=$3
: "${HOSTCC:?}" "${TOPSRC:?}" "${BUILD_FLAGS:?}"
members="${MEMBERS:?}"

work=$(mktemp -d "${TMPDIR:-/tmp}/libc_termios_mutant.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/inc" "$work/src"

sed -e "$script" "$TOPSRC/$file" > "$work/mutated"
if cmp -s "$work/mutated" "$TOPSRC/$file"; then
	echo "FAIL mutant $label: the script changed nothing in $file" >&2
	exit 1
fi

include_flags=
member_dir=$TOPSRC/lib/libc/gen
case $file in
include/termios.h)
	cp "$work/mutated" "$work/inc/termios.h"
	include_flags="-I$work/inc"
	;;
lib/libc/gen/*)
	;;
*)
	echo "FAIL mutant $label: $file is not a termios source" >&2
	exit 1
	;;
esac

objects=
for member in $members; do
	name=${member%.o}
	source=$member_dir/$name.c
	if [ "lib/libc/gen/$name.c" = "$file" ]; then
		cp "$work/mutated" "$work/src/$name.c"
		source=$work/src/$name.c
	fi
	# shellcheck disable=SC2086
	if ! $HOSTCC $include_flags $BUILD_FLAGS -c -o "$work/$name.o" "$source" \
	    2> "$work/compile.log"; then
		cat "$work/compile.log" >&2
		echo "FAIL mutant $label: the mutated source does not compile" >&2
		exit 1
	fi
	objects="$objects $work/$name.o"
done
# shellcheck disable=SC2086
if ! $HOSTCC $include_flags $BUILD_FLAGS -c -o "$work/gate.o" libc_termios_test.c \
    2> "$work/compile.log"; then
	cat "$work/compile.log" >&2
	echo "FAIL mutant $label: the gate does not compile against it" >&2
	exit 1
fi
for extra in ${EXTRA_OBJECTS:-}; do
	objects="$objects $extra"
done
# shellcheck disable=SC2086
$HOSTCC -m32 -o "$work/gate" "$work/gate.o" $objects
if "$work/gate" > "$work/run.log" 2>&1; then
	echo "FAIL mutant $label survived: the gate passed" >&2
	exit 1
fi
echo "PASS mutant $label rejected"
