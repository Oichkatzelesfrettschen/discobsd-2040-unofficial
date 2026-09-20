#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 tee" >&2
	exit 2
fi

tee_program=$(CDPATH= cd "$(dirname "$1")" && pwd)/$(basename "$1")
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

cd "$temporary_directory"
printf 'alpha\nbeta\n' > input
"$tee_program" output < input > standard
cmp -s input output
cmp -s input standard

printf 'prefix\n' > appended
"$tee_program" -a appended < input > /dev/null
printf 'prefix\nalpha\nbeta\n' > expected
cmp -s expected appended

"$tee_program" -- -a - < input > /dev/null
cmp -s input ./-a
cmp -s input ./-

if "$tee_program" -z < input > /dev/null 2> error; then
	echo "tee options: an unknown option returned success" >&2
	exit 1
fi
grep -q '^usage: tee ' error

echo "tee filesystem option tests passed"
