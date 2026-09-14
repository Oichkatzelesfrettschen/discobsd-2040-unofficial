#!/bin/sh
set -eu

driver=$1
probe=$2
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

fail()
{
	echo "tiny utility multicall: $1" >&2
	exit 1
}

expect_status()
{
	expected_status=$1
	shift
	set +e
	"$@"
	actual_status=$?
	set -e
	[ "$actual_status" -eq "$expected_status" ] ||
		fail "expected status $expected_status, got $actual_status: $*"
}

expect_status 0 "$driver" true ignored
expect_status 1 "$driver" false ignored

expect_status 1 "$driver" nohup >"$temporary_directory/usage.out" \
	2>"$temporary_directory/usage.err"
grep -q '^usage: nohup ' "$temporary_directory/usage.err" ||
	fail "nohup usage mismatch"

"$driver" nohup "$probe" arguments "space value" "" 'literal*value' \
	>"$temporary_directory/arguments.out" \
	2>"$temporary_directory/arguments.err"
printf '%s\n' '0:11:space value' '1:0:' '2:13:literal*value' \
	>"$temporary_directory/arguments.expected"
cmp "$temporary_directory/arguments.expected" \
	"$temporary_directory/arguments.out" >/dev/null ||
	fail "argument boundaries changed"
[ ! -s "$temporary_directory/arguments.err" ] ||
	fail "stderr did not join stdout"

expect_status 37 "$driver" nohup "$probe" exit 37
signal_output=$("$driver" nohup "$probe" signals)
[ "$signal_output" = "signals survived" ] || fail "ignored signal mismatch"

expected_priority=$("$probe" priority-plus-five)
nohup_priority=$("$driver" nohup "$probe" priority)
[ "$nohup_priority" -eq "$expected_priority" ] ||
	fail "priority increment mismatch"

"$driver" nohup "$probe" streams >"$temporary_directory/streams.out" \
	2>"$temporary_directory/streams.err"
printf 'stdout\nstderr\n' >"$temporary_directory/streams.expected"
cmp "$temporary_directory/streams.expected" \
	"$temporary_directory/streams.out" >/dev/null ||
	fail "stream join mismatch"
[ ! -s "$temporary_directory/streams.err" ] ||
	fail "stream stderr stayed separate"

expect_status 1 "$driver" nohup missing-nohup-command \
	>"$temporary_directory/missing.out" \
	2>"$temporary_directory/missing.err"
grep -q 'missing-nohup-command' "$temporary_directory/missing.out" ||
	fail "failed exec diagnostic absent"
[ ! -s "$temporary_directory/missing.err" ] ||
	fail "failed exec diagnostic used the old stderr"

(
	cd "$temporary_directory"
	ln -s "$driver" driver
	ln -s "$probe" probe
	script -q -e -c './driver nohup ./probe streams' /dev/null \
		>terminal.out
)
tr -d '\r' <"$temporary_directory/terminal.out" \
	>"$temporary_directory/terminal.clean"
grep -q "Sending output to 'nohup.out'" \
	"$temporary_directory/terminal.clean" ||
	fail "terminal redirect notice is absent"
cmp "$temporary_directory/streams.expected" \
	"$temporary_directory/nohup.out" >/dev/null ||
	fail "nohup.out mismatch"

echo "tiny utility multicall: ok"
