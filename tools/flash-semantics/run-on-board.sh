#!/bin/sh
# The flash semantics probe erases a fixed sector. Keep board execution
# closed until the runner binds the selected board and verifies the sector.
set -eu

echo "flash-semantics: board execution disabled pending target and scratch-sector verification" >&2
exit 2
