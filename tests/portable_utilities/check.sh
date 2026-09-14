#!/bin/sh
set -eu

fail()
{
	echo "portable utilities test: $1" >&2
	exit 1
}

compare_getopt()
{
	actual=$(./getopt_host "$@")
	expected=$(/usr/bin/getopt "$@")
	[ "$actual" = "$expected" ] || fail "getopt differential: $*"
}

compare_getopt 'ab:c' -a -b value operand
compare_getopt 'ab:' -abvalue tail
compare_getopt 'a' -- -a operand
compare_getopt 'ab:' operand
if output=$(./getopt_host 'ab:' -z 2>&1); then
	fail "getopt accepts an unknown option"
fi
case "$output" in
*"option z is invalid"*" --"*) ;;
*) fail "getopt unknown-option result" ;;
esac
if output=$(./getopt_host 'ab:' -b 2>&1); then
	fail "getopt accepts a missing option argument"
fi
case "$output" in
*"option b requires an argument"*" --"*) ;;
*) fail "getopt missing-argument result" ;;
esac

output=$(./yes_host one two | head -n 3)
expected='one two
one two
one two'
[ "$output" = "$expected" ] || fail "yes output"

./fixture_writer
printf '\000abc\000hello world\000' > regular.bin
output=$(./strings_host regular.bin)
[ "$output" = "hello world" ] || fail "strings default minimum"
output=$(./strings_host -n 3 regular.bin)
expected='abc
hello world'
[ "$output" = "$expected" ] || fail "strings selected minimum"
output=$(./strings_host raw.aout)
expected='TEXT
DATA'
[ "$output" = "$expected" ] || fail "strings raw a.out range"
dd if=raw.aout of=raw-truncated.aout bs=1 count=35 2>/dev/null
if output=$(./strings_host raw-truncated.aout 2>&1); then
	fail "strings accepts a truncated raw a.out"
fi
case "$output" in
*"truncated a.out"*) ;;
*) fail "strings truncated a.out diagnostic" ;;
esac
if output=$(./strings_host packed.aout 2>&1); then
	fail "strings accepts packed a.out as expanded data"
fi
case "$output" in
*"packed a.out requires -a"*) ;;
*) fail "strings packed a.out diagnostic" ;;
esac
output=$(./strings_host -a packed.aout)
case "$output" in
*PACKEDBYTES*) ;;
*) fail "strings packed container scan" ;;
esac

output=$(./users_host users.utmp)
[ "$output" = "alice bob" ] || fail "users sorting and deduplication"
if output=$(./users_host users-overflow.utmp 2>&1); then
	fail "users accepts an over-capacity input"
fi
case "$output" in
*"too many distinct users"*) ;;
*) fail "users capacity diagnostic" ;;
esac

echo "portable utility tests passed"
