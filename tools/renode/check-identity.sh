#!/bin/sh
# Run the Renode image identity gate, or say exactly what is missing and stop.
#
# The gate boots the UART0-console kernel three times on the same root flash
# image: as built, with one bit of its stamped CRC32 word inverted, and with
# one byte of a reserved vector entry set. check-identity.py writes the two
# altered kernels under tools/renode/vendor/results/identity, which git
# ignores, and requires the boot-time image line of each. The built kernel
# is read and never written.
#
# Usage: sh tools/renode/check-identity.sh [FLASH_IMAGE]
#
# FLASH_IMAGE is the root flash image to load at 0x10020000. It defaults to
# distrib/rp2040/flash.bin; the root Makefile passes ${FLASHBIN}, the
# selected board's image.

set -eu

# The caller selects the interpreter (the root Makefile exports PYTHON, empty
# when unset); an unset or empty value is refused instead of defaulted.
if [ -z "${PYTHON:-}" ]; then
	echo "check-identity: PYTHON must name the interpreter" >&2
	exit 2
fi
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
KERNEL="$TOP/sys/arch/rp2040/compile/PICO_UART/unix.bin"
IMAGE=${1:-$TOP/distrib/rp2040/flash.bin}
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
OUTPUT="$SCRIPT_DIR/vendor/results/identity"

missing() {
	echo "check-identity: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"
[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"
[ -r "$IMAGE" ] ||
	missing "no $IMAGE; run bmake MACHINE=rp2040 flash (BOARD=<board> builds flash-<board>.bin)"

RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-identity: Renode ${RUNNING_RENODE:-unknown}"

mkdir -p "$OUTPUT"
"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test
# -u: tools/run_logged_gate.py ends a run that prints nothing for its idle
# limit, and Python block-buffers a piped stdout until exit, so every
# stage line must reach the log as it is printed.
"$PYTHON" -I -u "$SCRIPT_DIR/check-identity.py" renode "$KERNEL" "$IMAGE" "$OUTPUT"
