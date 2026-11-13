#!/bin/sh
# Calibration for "tools/report-nightly-failure.sh --failed": each case
# writes a jobs document in the GitHub REST shape and compares the printed
# list. A passing run must print nothing, since the caller closes the issue
# on an empty list, and a failed, timed-out or cancelled job must print.
set -eu

here=$(dirname "$0")
report="$here/report-nightly-failure.sh"
work=$(mktemp -d)
trap 'rm -f "$work/jobs.json"; rmdir "$work"' EXIT
failures=0

expect() {
	name=$1
	want=$2
	printf '%s' "$3" >"$work/jobs.json"
	got=$(sh "$report" --failed "$work/jobs.json")
	if [ "$got" = "$want" ]; then
		echo "PASS $name"
	else
		echo "FAIL $name: want [$want] got [$got]"
		failures=$((failures + 1))
	fi
}

expect "a passing run prints nothing" "" \
	'{"jobs":[{"name":"a","conclusion":"success","steps":[{"name":"s","conclusion":"success"}]},
	{"name":"report","conclusion":null,"steps":[{"name":"t","conclusion":null}]}]}'
expect "a skipped job is not a failure" "" \
	'{"jobs":[{"name":"a","conclusion":"skipped","steps":[]}]}'
expect "a failed step names its job" "- a (failure): s2" \
	'{"jobs":[{"name":"a","conclusion":"failure","steps":[{"name":"s1","conclusion":"success"},{"name":"s2","conclusion":"failure"},{"name":"s3","conclusion":"skipped"}]}]}'
expect "a timed-out job prints" "- b (timed_out): no step recorded" \
	'{"jobs":[{"name":"b","conclusion":"timed_out","steps":[{"name":"s","conclusion":"success"}]}]}'
expect "a cancelled job prints its cancelled step" "- c (cancelled): s" \
	'{"jobs":[{"name":"c","conclusion":"cancelled","steps":[{"name":"s","conclusion":"cancelled"}]}]}'
expect "two pages of jobs are both read" "- a (failure): s
- d (failure): t" \
	'{"jobs":[{"name":"a","conclusion":"failure","steps":[{"name":"s","conclusion":"failure"}]}]}
	{"jobs":[{"name":"d","conclusion":"failure","steps":[{"name":"t","conclusion":"failure"}]}]}'

if [ "$failures" -ne 0 ]; then
	echo "nightly report: $failures failures"
	exit 1
fi
echo "nightly report: all cases passed"
