#!/bin/sh
set -eu

fail()
{
	echo "portable utilities test: $1" >&2
	exit 1
}

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

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
compare_getopt 'a' - tail
compare_getopt 'a-' -a tail
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
if output=$(./getopt_host 'a' --a tail 2>&1); then
	fail "getopt accepts an unsupported long option"
fi
case "$output" in
*"option --a is invalid"*" -- tail"*) ;;
*) fail "getopt long-option result" ;;
esac
case "$output" in
*" -a"*) fail "getopt partially parses an unsupported long option" ;;
esac

status=0
./getopt_host 'a' --bad -a tail >"$temporary_directory/getopt.out" \
    2>"$temporary_directory/getopt.err" || status=$?
[ "$status" -eq 1 ] || fail "getopt accepts an unsupported long option"
[ "$(cat "$temporary_directory/getopt.out")" = " -a -- tail" ] ||
	fail "getopt does not resume after an unsupported long option"
grep -Fq "option --bad is invalid" "$temporary_directory/getopt.err" ||
	fail "getopt long-option diagnostic"

output=$(./getopt_host 'b:' -b --bad tail)
[ "$output" = " -b --bad -- tail" ] ||
	fail "getopt rejects a double-dash option argument"

status=0
./getopt_host 'a' --- tail >"$temporary_directory/getopt.out" \
    2>"$temporary_directory/getopt.err" || status=$?
[ "$status" -eq 1 ] || fail "getopt accepts a triple-dash token"
[ "$(cat "$temporary_directory/getopt.out")" = " -- tail" ] ||
	fail "getopt partially parses a triple-dash token"

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
