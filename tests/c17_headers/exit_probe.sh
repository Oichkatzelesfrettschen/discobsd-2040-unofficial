#!/bin/sh
# Run an _Exit probe and require the given status with no output.
#   exit_probe.sh PROBE EXPECTED_STATUS
set -eu

probe=$1
expected=$2
output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

status=0
"$probe" >"$output" 2>&1 || status=$?
if [ "$status" -ne "$expected" ]; then
	echo "exit probe: status $status, expected $expected" >&2
	exit 1
fi
if [ -s "$output" ]; then
	echo "exit probe: the stream was flushed" >&2
	exit 1
fi
