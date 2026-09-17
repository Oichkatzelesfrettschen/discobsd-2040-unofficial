#!/bin/sh
# Build the flash-id probe against a resolved Pico SDK and assert it links.
#
# flash-id reads the QSPI chip's JEDEC identity and Winbond unique id and
# reports them over USB CDC. It lives in tools/flash-id and links no_flash,
# so the image runs entirely from SRAM. The root manifest does not carry
# it: the gate proves the SDK route still compiles and links for this
# board, and that the resulting image still fits the part.
#
# The SDK comes from tools/pico-sdk/sdk-path.sh, so this gate and
# tools/bench-flash-id-build.sh measure and build against the same SDK.
#
# Usage: sh tools/pico-sdk/check-flash-id.sh
# FLASH_ID_SRC names the probe source when it is not tools/flash-id.

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOPSRC=$(cd "$SCRIPT_DIR/../.." && pwd)
FLASH_ID_SRC=${FLASH_ID_SRC:-$TOPSRC/tools/flash-id}

# The RP2040 carries 264 KB of SRAM, and a no_flash image occupies it all
# at once: text, data and bss together must fit with room for the stack.
# Both are overridable so the budget branch can be calibrated against an
# image known to be too large.
SRAM_BYTES=${SRAM_BYTES:-270336}
STACK_MARGIN_BYTES=${STACK_MARGIN_BYTES:-8192}

skip() {
	printf 'check-flash-id: not run: %s\n' "$1" >&2
	exit 0
}

for t in cmake arm-none-eabi-gcc arm-none-eabi-size; do
	command -v "$t" >/dev/null 2>&1 || skip "$t absent"
done
if command -v ninja >/dev/null 2>&1; then
	GEN=Ninja
else
	GEN="Unix Makefiles"
fi

[ -f "$FLASH_ID_SRC/CMakeLists.txt" ] ||
	skip "no flash-id source at $FLASH_ID_SRC (set FLASH_ID_SRC)"

SDK=$(sh "$SCRIPT_DIR/sdk-path.sh") ||
	skip "no complete Pico SDK; sdk-path.sh named the candidates it checked"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

cp "$FLASH_ID_SRC/main.c" "$FLASH_ID_SRC/CMakeLists.txt" \
	"$FLASH_ID_SRC/pico_sdk_import.cmake" "$WORK/"

printf 'check-flash-id: SDK %s, generator %s\n' "$SDK" "$GEN"
PICO_SDK_PATH=$SDK cmake -S "$WORK" -B "$WORK/build" -G "$GEN" \
	-DCMAKE_BUILD_TYPE=Release >"$WORK/configure.log" 2>&1 || {
	tail -20 "$WORK/configure.log" >&2
	echo "check-flash-id: configure failed" >&2
	exit 1
}
cmake --build "$WORK/build" >"$WORK/build.log" 2>&1 || {
	tail -20 "$WORK/build.log" >&2
	echo "check-flash-id: build failed" >&2
	exit 1
}

ELF=$WORK/build/flash_id.elf
[ -f "$ELF" ] || { echo "check-flash-id: no flash_id.elf after a clean build" >&2; exit 1; }

set -- $(arm-none-eabi-size "$ELF" | awk 'NR==2{print $1, $2, $3}')
text=$1
data=$2
bss=$3
total=$((text + data + bss))
budget=$((SRAM_BYTES - STACK_MARGIN_BYTES))

printf 'check-flash-id: text %s  data %s  bss %s  total %s of %s usable SRAM\n' \
	"$text" "$data" "$bss" "$total" "$budget"

if [ "$total" -gt "$budget" ]; then
	printf 'check-flash-id: the no_flash image wants %s bytes, %s available\n' \
		"$total" "$budget" >&2
	exit 1
fi

echo "check-flash-id: ok"
