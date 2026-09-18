#!/bin/sh
# Run the Renode boot gate, or say exactly what is missing and stop.
#
# The gate boots the UART0-console kernel under Renode and asserts the console
# lines through the end of the device probe. It needs four things this tree
# does not carry: the renode-test harness, the third-party RP2040 peripheral
# models fetch-renode-rp2040.sh clones, a PICO_UART kernel, and a flash image.
# Each is reported by name rather than by a failed command deep inside the
# harness.
#
# renode-test runs the Robot suite through whatever "python3" resolves to,
# and Renode's own suite needs robotframework and four other packages that no
# distribution installs with the emulator. A virtual environment beside the
# vendored models supplies them and goes first on PATH for this run only.
#
# Usage: sh tools/renode/check-boot.sh

set -eu

PYTHON=${PYTHON:-python3}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
RP2040_COMMIT=$(sed -n 's/^RP2040_COMMIT=//p' "$SCRIPT_DIR/fetch-renode-rp2040.sh")
VENV="$SCRIPT_DIR/vendor/robotvenv"
KERNEL="$TOP/sys/arch/rp2040/compile/PICO_UART/unix.bin"
IMAGE="$TOP/distrib/rp2040/flash.bin"

missing() {
	echo "check-renode: $1" >&2
	exit 2
}

command -v renode-test >/dev/null 2>&1 ||
	missing "no renode-test in PATH; install Renode 1.17 or later"

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

# sys/arch/rp2040/compile/Makefile lists PICO and PICO_UART, so the ordinary
# kernel target builds both; a tree that has one and not the other has a
# PICO_UART directory config(8) has not been run in.
[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"

[ -r "$IMAGE" ] || missing "no $IMAGE; run bmake MACHINE=rp2040 flash"

# Renode ships the package list its own suites need; reuse it rather than
# naming versions here that would drift from the installed emulator.
REQUIREMENTS=/opt/renode/tests/requirements.txt
if [ ! -d "$VENV" ]; then
	[ -r "$REQUIREMENTS" ] ||
		missing "no $REQUIREMENTS; this Renode install ships no test harness"
	"$PYTHON" -m venv "$VENV"
	"$VENV/bin/pip" install --quiet --requirement "$REQUIREMENTS"
fi

# renode-test writes its report beside the suite; keep that out of the tree.
OUTPUT="$SCRIPT_DIR/vendor/results"
mkdir -p "$OUTPUT"

PATH="$VENV/bin:$PATH"
export PATH
cd "$OUTPUT"
exec renode-test "$SCRIPT_DIR/boot.robot"
