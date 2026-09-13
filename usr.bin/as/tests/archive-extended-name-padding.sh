#!/bin/sh
#
# Verify archive padding across extended-name and payload parity combinations.
#
set -eu

AR=${AR:?set AR to the tree's archive tool}
RANLIB=${RANLIB:?set RANLIB to the tree's archive index tool}
work_root=$(cd "${WORK:-.}" && pwd)
test_root=$work_root/archive-extended-name-padding

rm -rf "$test_root"
mkdir -p "$test_root"
trap 'rm -rf "$test_root"' 0 1 2 3 15

check_archive()
{
	case_name=$1
	member_name=$2
	payload=$3
	case_root=$test_root/$case_name
	archive=$case_root/libpadding.a

	mkdir -p "$case_root"
	printf '%s' "$payload" > "$case_root/$member_name"
	printf 'tail' > "$case_root/tail.bin"
	(
		cd "$case_root"
		"$AR" rc "$archive" "$member_name" tail.bin
		"$RANLIB" "$archive"
		"$AR" t "$archive" > members.actual
		printf '%s\n%s\n%s\n' __.SYMDEF "$member_name" tail.bin \
		    > members.expected
		diff -u members.expected members.actual
		"$AR" p "$archive" "$member_name" > payload.actual
		cmp -s "$member_name" payload.actual
		"$AR" p "$archive" tail.bin > tail.actual
		cmp -s tail.bin tail.actual
	)
}

# Names longer than 16 bytes use BSD extended format 1.  The archive pad byte
# depends on the combined name and payload size, not either component alone.
check_archive odd-name-even-payload odd_name_even.bin AB
check_archive odd-name-odd-payload odd_name_even.bin ABC
check_archive even-name-even-payload even_name_even.bin AB
check_archive even-name-odd-payload even_name_even.bin ABC
check_archive odd-name-zero-payload odd_name_empty.bin ''
check_archive even-name-zero-payload even_name_empty.bin ''

echo "archive: extended-name padding preserves all parity combinations"
