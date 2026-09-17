#!/bin/sh
# Fetch the Raspberry Pi Pico SDK at a pinned commit.
#
# The SDK is BSD 3-Clause and this tree already carries a piece of it,
# sys/arch/rp2040/boot2/boot2_w25q080.S, attributed in NOTICE section 4.
# The rest is fetched rather than vendored: it is a several-hundred-file
# CMake project whose only consumer here is the flash-id probe, and it
# carries submodules under their own licenses.
#
# The pin is a commit, not the 2.3.0 tag, because a tag can be moved and
# because the commit also fixes which TinyUSB the SDK's submodule points
# at. lib/tinyusb is the only submodule initialized: btstack, cyw43-driver,
# lwip and mbedtls are never linked by anything here and dominate the
# clone otherwise.
#
# Usage: sh tools/pico-sdk/fetch-pico-sdk.sh [dest-dir]
# Default dest-dir is tools/pico-sdk/vendor/pico-sdk, ignored by git.

set -eu

PICO_SDK_REPO=https://github.com/raspberrypi/pico-sdk.git
# Tag 2.3.0.
PICO_SDK_COMMIT=98a542c1a62fb549ffb5d66a3e5892b06276b670
# The submodule commit that pin carries, recorded so a mismatch is visible.
TINYUSB_COMMIT=86ad6e56c1700e85f1c5678607a762cfe3aa2f47

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-"$SCRIPT_DIR/vendor/pico-sdk"}

command -v git >/dev/null 2>&1 || {
	echo "fetch-pico-sdk: git absent; not run" >&2
	exit 1
}

if [ -d "$DEST/.git" ]; then
	git -C "$DEST" fetch --depth 1 origin "$PICO_SDK_COMMIT"
	git -C "$DEST" checkout --detach "$PICO_SDK_COMMIT"
else
	mkdir -p "$(dirname "$DEST")"
	git init -q "$DEST"
	git -C "$DEST" remote add origin "$PICO_SDK_REPO" 2>/dev/null ||
		git -C "$DEST" remote set-url origin "$PICO_SDK_REPO"
	git -C "$DEST" fetch --depth 1 origin "$PICO_SDK_COMMIT"
	git -C "$DEST" checkout --detach FETCH_HEAD
fi

# Only the USB stack flash-id links.
git -C "$DEST" submodule update --init --depth 1 lib/tinyusb

got=$(git -C "$DEST/lib/tinyusb" rev-parse HEAD)
if [ "$got" != "$TINYUSB_COMMIT" ]; then
	printf 'fetch-pico-sdk: lib/tinyusb is %s, the pin records %s\n' \
		"$got" "$TINYUSB_COMMIT" >&2
fi

[ -f "$DEST/lib/tinyusb/src/tusb.h" ] || {
	echo "fetch-pico-sdk: lib/tinyusb/src/tusb.h absent after fetch" >&2
	exit 1
}

printf 'pico-sdk at %s\n' "$DEST"
printf '  sdk      %s\n' "$PICO_SDK_COMMIT"
printf '  tinyusb  %s\n' "$got"
