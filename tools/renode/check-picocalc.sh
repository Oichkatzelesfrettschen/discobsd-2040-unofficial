#!/bin/sh
# Run the PicoCalc Renode gate, or say exactly what is missing and stop.
#
# The gate boots the PICOCALC kernel under Renode with the ST7365P panel and
# the keyboard controller modeled (tools/renode/picocalc/), advances virtual
# time in slices, decodes the panel's frame back to console text and asserts
# the boot banner, the panel registers the driver programs, the login prompt
# and a keyboard-driven login. It drives Renode the way check-boot.sh does,
# through the console-mode monitor on stdin (renode_monitor.py), so no
# virtual environment and no network listener are
# involved. It needs a PICOCALC kernel and a flash image in place of the
# PICO_UART kernel.
#
# Usage: sh tools/renode/check-picocalc.sh

set -eu

PYTHON=${PYTHON:-python3}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
RP2040_COMMIT=$(sed -n 's/^RP2040_COMMIT=//p' "$SCRIPT_DIR/fetch-renode-rp2040.sh")
KERNEL="$TOP/sys/arch/rp2040/compile/PICOCALC/unix.bin"
IMAGE="$TOP/distrib/rp2040/flash.bin"
MODELS="$VENDOR/emulation/bin/Release/net9.0/Peripherals.dll"
OUTPUT="$SCRIPT_DIR/vendor/results/picocalc"

missing() {
	echo "check-picocalc: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"

# check-boot.sh explains why the build is named before anything runs.
VERIFIED_RENODE="1.17.0+20260907gitf1dd1b4af"
RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-picocalc: Renode ${RUNNING_RENODE:-unknown}, models pinned at $RP2040_COMMIT"
if [ -n "$RUNNING_RENODE" ] && [ "$RUNNING_RENODE" != "$VERIFIED_RENODE" ]; then
	echo "check-picocalc: last verified against $VERIFIED_RENODE" >&2
fi

[ -d "$VENDOR/cores" ] ||
	missing "no RP2040 models; run sh tools/renode/fetch-renode-rp2040.sh"

# The panel model is an SPI child of PL022, which hands it frames only after
# the 0008 patch; a models tree fetched before that patch ran the series
# without it and would print a banner nowhere.
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"
grep -aq TransferFrame "$MODELS" ||
	missing "the built models predate patch 0008 (pl022 frames to an SPI child); rerun sh tools/renode/fetch-renode-rp2040.sh"

[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"

[ -r "$IMAGE" ] || missing "no $IMAGE; run bmake MACHINE=rp2040 flash"

mkdir -p "$OUTPUT"

# A decoder that cannot tell a good frame from a bad one reports nothing.
"$PYTHON" -I "$SCRIPT_DIR/picocalc_fb.py" --self-test

# The same holds for the Status-line assertions: a sleeping panel must fail.
"$PYTHON" -I "$SCRIPT_DIR/check-picocalc.py" --self-test

# The driver exits 1 on a failed assertion and 2 when the emulator stops
# answering; both end the gate, but the log is worth reading either way.
status=0
"$PYTHON" -I "$SCRIPT_DIR/check-picocalc.py" renode "$KERNEL" "$IMAGE" "$OUTPUT" ||
	status=$?

# The assertions say the console works through the models; the warning
# classes say the models behaved the way they did when those ceilings were
# measured. A class this board adds is in picocalc-warning-classes.txt, which
# the checker reads before the UART0 boot's list.
if [ -r "$OUTPUT/renode.log" ]; then
	echo "check-picocalc: renode.log"
	"$PYTHON" -I "$SCRIPT_DIR/check-warnings.py" \
		"$SCRIPT_DIR/picocalc-warning-classes.txt" \
		"$SCRIPT_DIR/warning-classes.txt" "$OUTPUT/renode.log" || status=1
fi
exit "$status"
