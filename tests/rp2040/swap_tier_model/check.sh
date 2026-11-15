#!/bin/sh
#
# Gate for the swap tier model (README.md). The shipped swapram_pool.c,
# swapram.c, flash_swap.c and subr_rmap.c, compiled unmodified into one host
# shared object, must pass the property, calibration, document-oracle and
# variant tests, and every mutant mutants.py names must fail them. A host
# without cffi, hypothesis or pytest reports a skip naming the
# missing module; a skip is not a pass.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

here=$(cd "$(dirname "$0")" && pwd)
for module in cffi hypothesis pytest; do
	if ! "$PYTHON" -c "import $module" >/dev/null 2>&1; then
		echo "SKIP check-swap-tier-model: Python module $module is" \
		    "absent for $PYTHON (install the distribution's" \
		    "python-$module package)"
		exit 0
	fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/swap-tier-model.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	find "$work" -depth \( -type f -o -type l \) -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

SWAP_TIER_MODEL_BUILD=$work
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
export SWAP_TIER_MODEL_BUILD PYTEST_DISABLE_PLUGIN_AUTOLOAD
unset SWAP_TIER_MODEL_OVERRIDES

cd "$here"
"$PYTHON" -m pytest -q -p no:cacheprovider -rxX "$here"
"$PYTHON" "$here/mutants.py"
