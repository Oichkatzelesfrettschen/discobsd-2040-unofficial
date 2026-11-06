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
# Usage: sh tools/renode/check-boot.sh

set -eu

PYTHON=${PYTHON:-python3}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
RP2040_COMMIT=$(sed -n 's/^RP2040_COMMIT=//p' "$SCRIPT_DIR/fetch-renode-rp2040.sh")
KERNEL="$TOP/sys/arch/rp2040/compile/PICO_UART/unix.bin"
IMAGE="$TOP/distrib/rp2040/flash.bin"
MODELS="$VENDOR/emulation/bin/Release/net9.0/Peripherals.dll"
OUTPUT="$SCRIPT_DIR/vendor/results/boot"

missing() {
	echo "check-renode: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"

# Name what is about to run. The models are built against whichever Renode
# is installed, and nothing in this tree pins that, so a gate that starts
# failing after a package upgrade should say which emulator it used before
# anyone goes looking in the kernel. VERIFIED_RENODE is the build this gate
# was last checked against; a mismatch is a note, not a refusal, because a
# newer Renode is the ordinary case and usually works.
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
# PICO_UART among them, so a tree without PICO_UART's image
# has a directory config(8) has not been run in.
[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"

[ -r "$IMAGE" ] || missing "no $IMAGE; run bmake MACHINE=rp2040 flash"

mkdir -p "$OUTPUT"

# The driver exits 1 on a failed assertion and 2 when the emulator stops
# answering; both end the gate, but the log is worth reading either way.
status=0
"$PYTHON" -I "$SCRIPT_DIR/check-boot.py" renode "$KERNEL" "$IMAGE" "$OUTPUT" ||
	status=$?

# The console assertions say the kernel reached a shell; the warning classes
# say the models it reached it through behaved the way they did when those
# ceilings were measured. A new class of infidelity fails here even when
# every console line still arrives.
if [ -r "$OUTPUT/renode.log" ]; then
	echo "check-renode: renode.log"
	"$PYTHON" -I "$SCRIPT_DIR/check-warnings.py" \
		"$SCRIPT_DIR/warning-classes.txt" "$OUTPUT/renode.log" || status=1
fi
exit "$status"
