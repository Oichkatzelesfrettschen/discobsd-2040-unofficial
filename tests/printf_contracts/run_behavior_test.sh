#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
test_program=${1:?usage: run_behavior_test.sh /path/to/printf}
script_directory=${0%/*}
exec "$PYTHON" "$script_directory/behavior_test.py" "$test_program"
