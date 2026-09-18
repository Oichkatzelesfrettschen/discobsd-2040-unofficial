#!/bin/sh
# Build the SRAM-resident board probes against a resolved Pico SDK and
# assert each one links and fits.
#
# flash-id reads the QSPI chip's JEDEC identity and Winbond unique id;
# flash-semantics measures the status register, erase and program timing,
# the erased state, program-only-clears and page wrap on one scratch sector.
# Both live under tools/, link no_flash so the image runs entirely from
# SRAM, and report over USB CDC. The root manifest carries neither: the
# gate proves the SDK route still compiles and links for this board, and
# that each image still fits the part.
#
# The SDK comes from tools/pico-sdk/sdk-path.sh, so this gate and
# tools/bench-flash-id-build.sh measure and build against the same SDK.
#
# Usage: sh tools/pico-sdk/check-flash-id.sh
# PROBE_DIRS lists the probe source directories, space separated, when they
# are not tools/flash-id and tools/flash-semantics. Each directory's CMake
# project is named after the directory with - replaced by _.

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOPSRC=$(cd "$SCRIPT_DIR/../.." && pwd)
PROBE_DIRS=${PROBE_DIRS:-"$TOPSRC/tools/flash-id $TOPSRC/tools/flash-semantics"}

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

SDK=$(sh "$SCRIPT_DIR/sdk-path.sh") ||
	skip "no complete Pico SDK; sdk-path.sh named the candidates it checked"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

budget=$((SRAM_BYTES - STACK_MARGIN_BYTES))
printf 'check-flash-id: SDK %s, generator %s\n' "$SDK" "$GEN"

for src in $PROBE_DIRS; do
	name=$(basename "$src" | tr - _)
	[ -f "$src/CMakeLists.txt" ] ||
		skip "no probe source at $src (set PROBE_DIRS)"

	build="$WORK/$name"
	mkdir -p "$build"
	cp "$src/main.c" "$src/CMakeLists.txt" "$src/pico_sdk_import.cmake" "$build/"

	PICO_SDK_PATH=$SDK cmake -S "$build" -B "$build/out" -G "$GEN" \
		-DCMAKE_BUILD_TYPE=Release >"$build/configure.log" 2>&1 || {
		tail -20 "$build/configure.log" >&2
		echo "check-flash-id: $name: configure failed" >&2
		exit 1
	}
	cmake --build "$build/out" >"$build/build.log" 2>&1 || {
		tail -20 "$build/build.log" >&2
		echo "check-flash-id: $name: build failed" >&2
		exit 1
	}

	ELF=$build/out/$name.elf
	[ -f "$ELF" ] || { echo "check-flash-id: no $name.elf after a clean build" >&2; exit 1; }

	set -- $(arm-none-eabi-size "$ELF" | awk 'NR==2{print $1, $2, $3}')
	text=$1
	data=$2
	bss=$3
	total=$((text + data + bss))

	printf 'check-flash-id: %s: text %s  data %s  bss %s  total %s of %s usable SRAM\n' \
		"$name" "$text" "$data" "$bss" "$total" "$budget"

	if [ "$total" -gt "$budget" ]; then
		printf 'check-flash-id: %s: the no_flash image wants %s bytes, %s available\n' \
			"$name" "$total" "$budget" >&2
		exit 1
	fi
done

echo "check-flash-id: ok"
