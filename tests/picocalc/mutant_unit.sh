#!/bin/sh
# Build one host test against a mutated driver source and require the
# test to fail.
#
# usage: mutant_unit.sh CC CFLAGS DEVDIR TEST.c FILE SED-EXPRESSION SOURCE...
#
# Every SOURCE (a path relative to DEVDIR) is copied by its base name to a
# private directory, FILE (one of them) is rewritten there by the sed
# expression, and the C sources among the copies are compiled with TEST.c.
# A SOURCE given as an absolute path is compiled where it stands,
# unmutated. The expression must change FILE.
#
# When MUTANT_EXPECT is set, the failing test's output must also contain
# that fixed string, so a mutant counts as rejected only for the failure it
# targets and not for a sanitizer report, a signal or a load-dependent
# timeout that any build of the test could hit.
set -eu

cc=$1
cflags=$2
dev=$3
test_c=$4
file=$5
expr=$6
shift 6

work=$(mktemp -d "${TMPDIR:-/tmp}/picocalc-mutant.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
csrcs=
for f in "$@"; do
	case $f in
	/*.c)
		csrcs="$csrcs $f"
		continue
		;;
	esac
	cp "$dev/$f" "$work/"
	case $f in
	*.c) csrcs="$csrcs $work/${f##*/}" ;;
	esac
done
sed "$expr" "$dev/$file" >"$work/${file##*/}"
if cmp -s "$dev/$file" "$work/${file##*/}"; then
	echo "mutant_unit.sh: '$expr' changed nothing in $file" >&2
	exit 1
fi
# shellcheck disable=SC2086
"$cc" $cflags -I"$work" -o "$work/test" "$test_c" $csrcs
if "$work/test" >"$work/output" 2>&1; then
	echo "mutant_unit.sh: $test_c passed the mutant '$expr' of $file" >&2
	exit 1
fi
if [ -n "${MUTANT_EXPECT:-}" ] &&
    ! grep -F -q -- "$MUTANT_EXPECT" "$work/output"; then
	echo "mutant_unit.sh: $test_c failed the mutant '$expr' of $file" \
	    "without '$MUTANT_EXPECT':" >&2
	cat "$work/output" >&2
	exit 1
fi
echo "mutant rejected: $file '$expr'"
