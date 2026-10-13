#!/bin/sh
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
exec "$PYTHON" ./check_arithmetic.py ../.. "${HOSTCC:-cc}"
