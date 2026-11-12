#!/bin/sh
# Run the Renode table-exhaustion gate, or say exactly what is missing and stop.
#
# The gate adds tests/rp2040/capacity_exhaustion/filetab and proctab to a
# copy of the root disk image under /tmp, wraps the copy as a flash region
# image the way the flash target does, boots the UART0-console kernel on it
# under Renode and runs both programs from a root shell (check-capacity.py).
# The shipped images are read and never written: the programs are regression
# tools outside the root manifest, and the composed image lives under
# tools/renode/vendor/results/capacity, which git ignores.
#
# Usage: sh tools/renode/check-capacity.sh [DISK_IMAGE]
#
# DISK_IMAGE is the partitioned root disk image whose first partition is the
# root file system. It defaults to distrib/rp2040/sdcard.img; the root
# Makefile passes ${FSIMG}, the selected board's image.

set -eu

# The caller selects the interpreter (the root Makefile exports PYTHON, empty
# when unset); an unset or empty value is refused instead of defaulted.
if [ -z "${PYTHON:-}" ]; then
	echo "check-capacity: PYTHON must name the interpreter" >&2
	exit 2
fi
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
KERNEL="$TOP/sys/arch/rp2040/compile/PICO_UART/unix.bin"
DISK=${1:-$TOP/distrib/rp2040/sdcard.img}
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
FIXTURES="$TOP/tests/rp2040/capacity_exhaustion"
FSUTIL="$TOP/tools/bin/rp2040/fsutil"
FLASHIMG="$TOP/tools/bin/rp2040/flashimg"
OUTPUT="$SCRIPT_DIR/vendor/results/capacity"

missing() {
	echo "check-capacity: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"
[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"
[ -r "$DISK" ] ||
	missing "no $DISK; run bmake MACHINE=rp2040 fs"
[ -x "$FSUTIL" ] && [ -x "$FLASHIMG" ] ||
	missing "no fsutil or flashimg under tools/bin/rp2040; run bmake MACHINE=rp2040 tools"
for program in filetab proctab; do
	[ -r "$FIXTURES/$program" ] ||
		missing "no $FIXTURES/$program; run bmake -C tests/rp2040/capacity_exhaustion MACHINE=rp2040"
done

RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-capacity: Renode ${RUNNING_RENODE:-unknown}"

# The staging tree mirrors the target paths because fsutil --add stores each
# file under the name it is given, relative to the working directory.
mkdir -p "$OUTPUT"
STAGE=$(mktemp -d "$OUTPUT/stage.XXXXXX")
trap 'rm -f "$STAGE/tmp/filetab" "$STAGE/tmp/proctab"
rmdir "$STAGE/tmp" "$STAGE" 2>/dev/null || :' EXIT HUP INT TERM
mkdir "$STAGE/tmp"
for program in filetab proctab; do
	cp "$FIXTURES/$program" "$STAGE/tmp/$program"
	chmod 755 "$STAGE/tmp/$program"
done
cp "$DISK" "$OUTPUT/disk.img"
(cd "$STAGE" && "$FSUTIL" --add --partition=1 "$OUTPUT/disk.img" tmp/filetab tmp/proctab)
"$FSUTIL" --check --partition=1 "$OUTPUT/disk.img" >"$OUTPUT/fsck.txt" 2>&1 ||
	missing "the composed image fails fsutil --check; see $OUTPUT/fsck.txt"
"$FLASHIMG" "$OUTPUT/disk.img" "$OUTPUT/flash.bin" >"$OUTPUT/flashimg.txt"

"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test
"$PYTHON" -I "$SCRIPT_DIR/check-capacity.py" renode "$KERNEL" "$OUTPUT/flash.bin" "$OUTPUT"
