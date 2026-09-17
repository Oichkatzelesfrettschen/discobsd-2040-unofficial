#!/bin/sh
# Resolve a usable Raspberry Pi Pico SDK and print its path, or say which
# candidates were checked and exit nonzero.
#
# Three candidates, in order: PICO_SDK_PATH, the tree's own fetched copy
# under tools/pico-sdk/vendor/pico-sdk, and a pico-sdk checked out beside
# this repository in the same workspace. No candidate is an absolute path
# written into the tree, so a checkout moves without editing anything.
#
# A directory alone is not an SDK. A distribution package materializes the
# submodules as plain directories, while a fresh clone leaves lib/tinyusb
# empty until the submodule is initialized, and that half-state fails
# inside CMake rather than here. The check is therefore for the header
# flash-id's USB stack actually compiles against.
#
# Usage: sh tools/pico-sdk/sdk-path.sh
#        PICO_SDK_PATH=$(sh tools/pico-sdk/sdk-path.sh) || exit 1

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOPSRC=$(cd "$SCRIPT_DIR/../.." && pwd)
WORKSPACE=$(cd "$TOPSRC/.." && pwd)

# The file that proves an SDK is complete rather than merely present.
SENTINEL=lib/tinyusb/src/tusb.h

usable() {
	[ -n "${1:-}" ] && [ -f "$1/pico_sdk_init.cmake" ] && [ -f "$1/$SENTINEL" ]
}

CANDIDATES="${PICO_SDK_PATH:-} $SCRIPT_DIR/vendor/pico-sdk $WORKSPACE/pico-sdk"

for c in $CANDIDATES; do
	if usable "$c"; then
		(cd "$c" && pwd)
		exit 0
	fi
done

{
	printf 'pico-sdk: no complete SDK found. Checked:\n'
	for c in $CANDIDATES; do
		if [ ! -d "$c" ]; then
			printf '  %s (absent)\n' "$c"
		elif [ ! -f "$c/pico_sdk_init.cmake" ]; then
			printf '  %s (no pico_sdk_init.cmake)\n' "$c"
		else
			printf '  %s (no %s: submodule not initialized)\n' "$c" "$SENTINEL"
		fi
	done
	printf 'Set PICO_SDK_PATH, check a pico-sdk out beside this tree, or run\n'
	printf '  sh tools/pico-sdk/fetch-pico-sdk.sh\n'
} >&2
exit 1
