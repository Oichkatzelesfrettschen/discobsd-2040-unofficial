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
# Usage: sh tools/renode/check-picocalc.sh [--dump] [FLASH_IMAGE]
#
# --dump adds Renode's SD card model on SPI0 with an image fsutil makes (a
# UFS partition and a crash-dump partition) and the crash-dump stages of
# picocalc_dump.py: reboot -d, savecore and a panic. FSUTIL names the host
# fsutil, and the flash image must carry savecore, which only the picocalc
# profile's image does (BOARD=PICOCALC, flash-PICOCALC.bin).
#
# FLASH_IMAGE is the root flash image to load at 0x10020000. It defaults to
# distrib/rp2040/flash.bin, the PICO board's output; the root Makefile passes
# ${FLASHBIN}, so BOARD=PICOCALC boots the PICOCALC kernel with the PICOCALC
# profile's root (flash-PICOCALC.bin). Every kernel boots every board's root,
# so any board's image is valid here.

set -eu

# The caller selects the interpreter (the root Makefile exports PYTHON, empty
# when unset); an unset or empty value is refused instead of defaulted.
if [ -z "${PYTHON:-}" ]; then
	echo "check-picocalc: PYTHON must name the interpreter" >&2
	exit 2
fi
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
RP2040_COMMIT=$(sed -n 's/^RP2040_COMMIT=//p' "$SCRIPT_DIR/fetch-renode-rp2040.sh")
KERNEL="$TOP/sys/arch/rp2040/compile/PICOCALC/unix.bin"
DUMP=
if [ "${1:-}" = "--dump" ]; then
	DUMP=yes
	shift
fi
IMAGE=${1:-$TOP/distrib/rp2040/flash.bin}
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
OUTPUT="$SCRIPT_DIR/vendor/results/picocalc${DUMP:+-dump}"

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

[ -r "$IMAGE" ] ||
	missing "no $IMAGE; run bmake MACHINE=rp2040 flash (BOARD=<board> builds flash-<board>.bin)"

if [ -n "$DUMP" ]; then
	[ -x "${FSUTIL:-}" ] ||
		missing "FSUTIL must name the host fsutil; run bmake MACHINE=rp2040 tools"
	[ -r "${KERNEL%.bin}.elf" ] ||
		missing "no ${KERNEL%.bin}.elf; run bmake MACHINE=rp2040 kernel"
	# The validator the dump stages call must reject what it should.
	"$PYTHON" -I "$TOP/tools/check_crash_dump.py" --self-test
fi

mkdir -p "$OUTPUT"

# A decoder that cannot tell a good frame from a bad one reports nothing.
"$PYTHON" -I "$SCRIPT_DIR/picocalc_fb.py" --self-test

# The same holds for the Status-line assertions: a sleeping panel must fail.
"$PYTHON" -I "$SCRIPT_DIR/check-picocalc.py" --self-test

# And for the monitor driver: an exited Renode must read as RenodeError.
"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test

# The driver exits 1 on a failed assertion and 2 when the emulator stops
# answering; both end the gate, but the log is worth reading either way.
status=0
# -u: tools/run_logged_gate.py ends a run that prints nothing for its idle
# limit, and Python block-buffers a piped stdout until exit, so every
# stage line must reach the log as it is printed.
if [ -n "$DUMP" ]; then
	"$PYTHON" -I -u "$SCRIPT_DIR/check-picocalc.py" renode "$KERNEL" "$IMAGE" \
	    "$OUTPUT" "$OUTPUT/card.img" "$FSUTIL" || status=$?
else
	"$PYTHON" -I -u "$SCRIPT_DIR/check-picocalc.py" renode "$KERNEL" "$IMAGE" \
	    "$OUTPUT" || status=$?
fi

# The assertions say the console works through the models; the warning
# classes say the models behaved the way they did when those ceilings were
# measured. A class this board adds is in picocalc-warning-classes.txt, which
# the checker reads before the UART0 boot's list.
if [ -r "$OUTPUT/renode.log" ]; then
	echo "check-picocalc: renode.log"
	"$PYTHON" -I "$SCRIPT_DIR/check-warnings.py" \
		${DUMP:+"$SCRIPT_DIR/picocalc-dump-warning-classes.txt"} \
		"$SCRIPT_DIR/picocalc-warning-classes.txt" \
		"$SCRIPT_DIR/warning-classes.txt" "$OUTPUT/renode.log" || status=1
fi
exit "$status"
