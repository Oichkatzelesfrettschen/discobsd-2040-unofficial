#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"

exec "${PYTHON}" "$(dirname "$0")/behavior_test.py" "$@"
