#!/bin/sh
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
"$PYTHON" check.py ../.. --compiler "${HOSTCC:-cc}"
