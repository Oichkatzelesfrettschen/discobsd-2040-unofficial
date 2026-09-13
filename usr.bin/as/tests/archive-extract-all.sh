#!/bin/sh
#
# Verify that extracting a whole archive walks every member.
#
set -eu

AR=${AR:?set AR to the tree's archive tool}
RANLIB=${RANLIB:?set RANLIB to the tree's archive index tool}
work_root=$(cd "${WORK:-.}" && pwd)
test_root=$work_root/archive-extract-all
build_root=$test_root/build
extract_root=$test_root/extract
archive=$test_root/libextract.a

rm -rf "$test_root"
mkdir -p "$build_root" "$extract_root"
trap 'rm -rf "$test_root"' 0 1 2 3 15

# A member occupies its extended name plus its payload, so the pad byte that
# follows depends on the sum.  An odd name with an even payload is the case
# that a parity test on the payload alone misses; every such member is
# followed by another member so a missed pad corrupts the next header.
# odd_name_even.bin is 17 bytes, even_name_even.bin is 18.
add_member()
{
	printf '%s' "$2" > "$build_root/$1"
}

add_member odd_name_even.bin AB
add_member first.bin A
add_member even_name_even.bin ABCD
add_member odd_name_odd.bin ABC
add_member even_name_odd.bin ABCDE
add_member short.bin AB
add_member last.bin ZZZ

members='odd_name_even.bin first.bin even_name_even.bin odd_name_odd.bin
    even_name_odd.bin short.bin last.bin'

(
	cd "$build_root"
	# shellcheck disable=SC2086
	"$AR" rc "$archive" $members
	"$RANLIB" "$archive"
)

for member in $members; do
	printf 'stale' > "$extract_root/$member"
done

(
	cd "$extract_root"
	"$AR" t "$archive" > members.actual
	{ echo __.SYMDEF; for member in $members; do echo "$member"; done; } \
	    > members.expected
	diff -u members.expected members.actual
	"$AR" x "$archive"
	for member in $members; do
		cmp -s "$build_root/$member" "$member"
	done
)

echo "archive: extracting all members walks every member"
