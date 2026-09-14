#!/bin/sh
# Exercise regular empty and nonempty pattern sources through fgrep's command
# path. The capacity oracle separately measures the allocation boundaries.
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 fgrep" >&2
	exit 2
fi

fgrep_program=$1
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM
empty_patterns=$temporary_directory/empty.patterns
patterns=$temporary_directory/patterns
input=$temporary_directory/input
actual=$temporary_directory/actual
expected=$temporary_directory/expected
fifo_patterns=$temporary_directory/patterns.fifo

: >"$empty_patterns"
printf '%s\n' alpha alphabet beta >"$input"

status=0
"$fgrep_program" -f "$empty_patterns" "$input" >"$actual" || status=$?
if [ "$status" -ne 1 ] || [ -s "$actual" ]; then
	echo "empty regular pattern source did not report zero matches" >&2
	exit 1
fi

status=0
"$fgrep_program" -x -f "$empty_patterns" "$input" >"$actual" || status=$?
if [ "$status" -ne 1 ] || [ -s "$actual" ]; then
	echo "empty regular pattern source did not report zero matches" >&2
	exit 1
fi

"$fgrep_program" -v -f "$empty_patterns" "$input" >"$actual"
cmp "$input" "$actual"

printf '%s\n' alpha >"$patterns"
printf '%s\n' alpha >"$expected"
"$fgrep_program" -x -f "$patterns" "$input" >"$actual"
cmp "$expected" "$actual"

printf '%s\n' alpha alphabet >"$expected"
"$fgrep_program" -f "$patterns" "$input" >"$actual"
cmp "$expected" "$actual"

status=0
"$fgrep_program" -x -f /dev/null "$input" >"$actual" || status=$?
if [ "$status" -ne 1 ] || [ -s "$actual" ]; then
	echo "non-regular pattern source did not report zero matches" >&2
	exit 1
fi

mkfifo "$fifo_patterns"
printf '%s\n' alpha >"$fifo_patterns" &
writer_pid=$!
status=0
timeout 5 "$fgrep_program" -x -f "$fifo_patterns" "$input" >"$actual" || \
    status=$?
wait "$writer_pid"
printf '%s\n' alpha >"$expected"
if [ "$status" -ne 0 ]; then
	echo "FIFO pattern source exited with status $status" >&2
	exit 1
fi
cmp "$expected" "$actual"

echo "fgrep functional tests passed"
