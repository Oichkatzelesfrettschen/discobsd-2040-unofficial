#!/bin/sh
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON

if [ "$#" -ne 3 ]; then
	echo "usage: $0 variant compiler source-root" >&2
	exit 2
fi

exec "$PYTHON" ./check.py --variant "$1" --cc "$2" --source-root "$3"
