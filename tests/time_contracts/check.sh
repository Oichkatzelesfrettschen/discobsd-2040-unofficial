#!/bin/sh
# Contract for the time utility as built by build.sh.
# Usage: check.sh DIR   (DIR holds utilbox, time and probe)
set -eu

dir=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
shim_mode=
mode=direct

fail()
{
	echo "time contract: $1" >&2
	exit 1
}

# tm ARGS...: invoke time as its own name or through "utilbox time".
tm()
{
	if [ "$mode" = direct ]; then
		env TIME_TEST_WAIT3="$shim_mode" "$dir/time" "$@"
	else
		env TIME_TEST_WAIT3="$shim_mode" "$dir/utilbox" time "$@"
	fi
}

# run STATUS NAME ARGS...: run tm, record stdout and stderr in
# $scratch/out and $scratch/err, and require the exit status.
run()
{
	expected=$1
	name=$2
	shift 2
	set +e
	tm "$@" >"$scratch/out" 2>"$scratch/err"
	actual=$?
	set -e
	[ "$actual" -eq "$expected" ] ||
		fail "$mode: $name: expected status $expected, got $actual"
}

report_lines()
{
	grep -Ec '^ *[0-9]+\.[0-9]{2} real +[0-9]+\.[0-9]{2} user +[0-9]+\.[0-9]{2} sys *$' \
		"$scratch/err" || true
}

want_report()
{
	[ "$(report_lines)" -eq 1 ] || fail "$mode: $1: want one report line"
}

for mode in direct box; do
	for status in 0 1 37; do
		run "$status" "exit $status" "$dir/probe" exit "$status"
		[ "$(cat "$scratch/out")" = "child output" ] ||
			fail "$mode: child output lost"
		want_report "exit $status"
	done

	run 127 "missing program" "$dir/missing-program"
	grep -q 'missing-program' "$scratch/err" ||
		fail "$mode: missing program not named"
	want_report "missing program"

	for pair in 2:130 3:131 9:137 15:143; do
		signal=${pair%:*}
		run "${pair#*:}" "signal $signal" "$dir/probe" signal "$signal"
		grep -q 'Command terminated abnormally' "$scratch/err" ||
			fail "$mode: signal $signal: no abnormal-end message"
	done

	run 0 "no command"
	[ ! -s "$scratch/err" ] || fail "$mode: no command wrote a report"

	shim_mode=eintr
	run 37 "interrupted wait3" "$dir/probe" exit 37
	want_report "interrupted wait3"

	shim_mode=echild
	run 125 "failed wait3" "$dir/probe" exit 37
	grep -q '^time: ' "$scratch/err" ||
		fail "$mode: failed wait3 not reported"
	if grep -q 'wait3 retried' "$scratch/err"; then
		fail "$mode: failed wait3 retried"
	fi
	shim_mode=
done

echo "time contract: ok"
