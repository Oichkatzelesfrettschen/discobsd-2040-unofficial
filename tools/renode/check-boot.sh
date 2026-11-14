#!/bin/sh
# Run the Renode boot gate, or say exactly what is missing and stop.
#
# The gate boots the UART0-console kernel under Renode from the real boot
# ROM, asserts the console lines through the device probe, logs in and runs
# uname, sysctl kern.capacity and the MPU fault and exec residue probes
# (check-boot.py). It needs three things this tree does not carry: the
# third-party RP2040 peripheral models fetch-renode-rp2040.sh clones and
# builds, a PICO_UART kernel, and a flash image. Each is reported by name
# rather than by a failed command deep inside the emulator.
#
# The console-mode Renode monitor on stdin drives the run
# (renode_monitor.py), so no Robot harness, no virtual environment and no
# listening socket are involved, the same way check-picocalc.sh runs.
#
# Usage: sh tools/renode/check-boot.sh [FLASH_IMAGE [CONFIG]]
#
# FLASH_IMAGE is the root flash image to load at 0x10020000. It defaults to
# distrib/rp2040/flash.bin, the PICO board's output; the root Makefile passes
# ${FLASHBIN}, which names flash-${BOARD}.bin for every other board. Every
# kernel boots every board's root, so any board's image is valid here.
#
# CONFIG names the kernel configuration and defaults to PICO_UART, whose
# compiled descriptor selects the UART0 console. PICOCALC_PSRAM is the union
# kernel: its unix.bin carries the bare-Pico USB console descriptor, which
# Renode cannot serve, so the gate boots a copy whose descriptor
# distrib/rp2040/mkboarduf2.py rewrote to a bare Pico with the UART0 console.

set -eu

# The caller selects the interpreter (the root Makefile exports PYTHON, empty
# when unset); an unset or empty value is refused instead of defaulted.
if [ -z "${PYTHON:-}" ]; then
	echo "check-boot: PYTHON must name the interpreter" >&2
	exit 2
fi
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
RP2040_COMMIT=$(sed -n 's/^RP2040_COMMIT=//p' "$SCRIPT_DIR/fetch-renode-rp2040.sh")
IMAGE=${1:-$TOP/distrib/rp2040/flash.bin}
CONFIG=${2:-PICO_UART}
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
BUILT_KERNEL="$TOP/sys/arch/rp2040/compile/$CONFIG/unix.bin"
case "$CONFIG" in
PICO_UART)
	OUTPUT="$SCRIPT_DIR/vendor/results/boot"
	KERNEL=$BUILT_KERNEL
	BOARD_CLASSES=
	;;
PICOCALC_PSRAM)
	OUTPUT="$SCRIPT_DIR/vendor/results/boot-union"
	KERNEL="$OUTPUT/unix-uart.bin"
	BOARD_CLASSES="$SCRIPT_DIR/union-warning-classes.txt"
	;;
*)
	echo "check-renode: CONFIG must be PICO_UART or PICOCALC_PSRAM, not $CONFIG" >&2
	exit 2
	;;
esac

missing() {
	echo "check-renode: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"

# Name what is about to run. The models are built against whichever Renode
# is installed, so a gate that starts failing after a package upgrade should
# say which emulator it used before anyone goes looking in the kernel.
# VERIFIED_RENODE is the build this gate was last checked against; a
# mismatch is a note, not a refusal, because a newer Renode is the ordinary
# case and usually works. Firmware CI installs one pinned release and
# fails on a mismatch through tools/renode/check-renode-pin.sh.
VERIFIED_RENODE="1.17.0+20260907gitf1dd1b4af"
RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-renode: Renode ${RUNNING_RENODE:-unknown}, models pinned at $RP2040_COMMIT"
if [ -n "$RUNNING_RENODE" ] && [ "$RUNNING_RENODE" != "$VERIFIED_RENODE" ]; then
	echo "check-renode: last verified against $VERIFIED_RENODE" >&2
fi

[ -d "$VENDOR/cores" ] ||
	missing "no RP2040 models; run sh tools/renode/fetch-renode-rp2040.sh"
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"

# sys/arch/rp2040/compile/Makefile builds every MACHINE_KERNEL_CONFIGS entry,
# so a tree without the selected image has a directory config(8) has not
# been run in.
[ -r "$BUILT_KERNEL" ] ||
	missing "no $BUILT_KERNEL; run bmake MACHINE=rp2040 kernel"

[ -r "$IMAGE" ] ||
	missing "no $IMAGE; run bmake MACHINE=rp2040 flash (BOARD=<board> builds flash-<board>.bin)"

mkdir -p "$OUTPUT"

# The patcher refuses an image without exactly one descriptor block, so a
# union kernel that lost its .board section stops here by name.
if [ "$KERNEL" != "$BUILT_KERNEL" ]; then
	"$PYTHON" -I "$TOP/distrib/rp2040/mkboarduf2.py" --flat --board=PICO_UART \
		"$BUILT_KERNEL" "$KERNEL"
fi

# An exited Renode must read as RenodeError, not as a traceback.
"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test

# The driver exits 1 on a failed assertion and 2 when the emulator stops
# answering; both end the gate, but the log is worth reading either way.
status=0
# -u: tools/run_logged_gate.py ends a run that prints nothing for its idle
# limit, and Python block-buffers a piped stdout until exit, so every
# stage line must reach the log as it is printed.
"$PYTHON" -I -u "$SCRIPT_DIR/check-boot.py" renode "$KERNEL" "$IMAGE" "$OUTPUT" \
	"$CONFIG" ||
	status=$?

# The console assertions say the kernel reached a shell; the warning classes
# say the models it reached it through behaved the way they did when those
# ceilings were measured. A new class of infidelity fails here even when
# every console line still arrives.
if [ -r "$OUTPUT/renode.log" ]; then
	echo "check-renode: renode.log"
	set -- "$SCRIPT_DIR/boot-warning-classes.txt" \
		"$SCRIPT_DIR/warning-classes.txt"
	[ -z "$BOARD_CLASSES" ] || set -- "$@" "$BOARD_CLASSES"
	"$PYTHON" -I "$SCRIPT_DIR/check-warnings.py" "$@" "$OUTPUT/renode.log" ||
		status=1
fi
exit "$status"
