#!/bin/sh
# Run the Renode table-exhaustion gate, or say exactly what is missing and stop.
#
# The gate adds tests/rp2040/capacity_exhaustion/filetab, inotab and proctab
# to a copy of the root disk image under /tmp, wraps the copy as a flash
# region image the way the flash target does, boots a UART0-console kernel
# on it under Renode and runs the three programs from a root shell
# (check-capacity.py).
# The shipped images are read and never written: the programs are regression
# tools outside the root manifest, and the composed image lives under
# tools/renode/vendor/results/capacity, which git ignores.
#
# Usage: sh tools/renode/check-capacity.sh [DISK_IMAGE [CONFIG [refuse]]]
#
# DISK_IMAGE is the partitioned root disk image whose first partition is the
# root file system. It defaults to distrib/rp2040/sdcard.img; the root
# Makefile passes ${FSIMG}, the selected board's image.
#
# CONFIG names the kernel configuration and defaults to PICO_DIAG, the lean
# PICO kernel plus CAPACITY_STATS and SYSTRACE. Its unix.bin carries the
# bare-Pico USB console descriptor, which Renode cannot serve, so the gate
# boots a copy whose descriptor distrib/rp2040/mkboarduf2.py rewrote to the
# UART0 console, as check-boot.sh does for the union kernel. PICO takes the
# same patch; it omits CAPACITY_STATS, so the gate must fail on it at the
# first kern.capacity read, which calibrates the gate. PICO_UART boots as
# built.
#
# The word refuse inverts the verdict: the run must exit 1 and its transcript
# must carry the missing-record line ("FAIL kern.capacity rows before the
# runs: []"), and any other outcome exits 1 here. check-renode-capacity runs
# that way on PICO after the PICO_DIAG run, so a gate that accepts a kernel
# without CAPACITY_STATS turns the target red.

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
DISK=${1:-$TOP/distrib/rp2040/sdcard.img}
CONFIG=${2:-PICO_DIAG}
EXPECT=${3:-pass}
case "$EXPECT" in
pass | refuse) ;;
*)
	echo "check-capacity: the third argument must be refuse, not $EXPECT" >&2
	exit 2
	;;
esac
BUILT_KERNEL="$TOP/sys/arch/rp2040/compile/$CONFIG/unix.bin"
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
FIXTURES="$TOP/tests/rp2040/capacity_exhaustion"
FSUTIL="$TOP/tools/bin/rp2040/fsutil"
FLASHIMG="$TOP/tools/bin/rp2040/flashimg"
case "$CONFIG" in
PICO_DIAG)
	OUTPUT="$SCRIPT_DIR/vendor/results/capacity"
	KERNEL="$OUTPUT/unix-uart.bin"
	;;
PICO)
	OUTPUT="$SCRIPT_DIR/vendor/results/capacity-pico"
	KERNEL="$OUTPUT/unix-uart.bin"
	;;
PICO_UART)
	OUTPUT="$SCRIPT_DIR/vendor/results/capacity-pico-uart"
	KERNEL=$BUILT_KERNEL
	;;
*)
	echo "check-capacity: CONFIG must be PICO_DIAG, PICO or PICO_UART, not $CONFIG" >&2
	exit 2
	;;
esac

missing() {
	echo "check-capacity: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"
sh "$SCRIPT_DIR/models-series.sh" check "$MODELS" ||
	missing "the built models do not match tools/renode/patches; rerun sh tools/renode/fetch-renode-rp2040.sh"
[ -r "$BUILT_KERNEL" ] ||
	missing "no $BUILT_KERNEL; run bmake MACHINE=rp2040 kernel"
[ -r "$DISK" ] ||
	missing "no $DISK; run bmake MACHINE=rp2040 fs"
[ -x "$FSUTIL" ] && [ -x "$FLASHIMG" ] ||
	missing "no fsutil or flashimg under tools/bin/rp2040; run bmake MACHINE=rp2040 tools"
for program in filetab inotab proctab clistbound; do
	[ -r "$FIXTURES/$program" ] ||
		missing "no $FIXTURES/$program; run bmake -C tests/rp2040/capacity_exhaustion MACHINE=rp2040"
done
# The kernel-record readers, which the RP2040 root does not carry, run from
# /tmp beside the fixtures: their build products, as the SD image ships them.
READERS="usr.bin/vmstat/vmstat usr.sbin/iostat/iostat usr.sbin/pstat/pstat"
for reader in $READERS; do
	[ -r "$TOP/$reader" ] ||
		missing "no $TOP/$reader; run bmake MACHINE=rp2040 build"
done

RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-capacity: Renode ${RUNNING_RENODE:-unknown}"

# The staging tree mirrors the target paths because fsutil --add stores each
# file under the name it is given, relative to the working directory.
mkdir -p "$OUTPUT"
# The patcher refuses an image without exactly one descriptor block, so a
# kernel that lost its .board section stops here by name.
if [ "$KERNEL" != "$BUILT_KERNEL" ]; then
	"$PYTHON" -I "$TOP/distrib/rp2040/mkboarduf2.py" --flat --board=PICO_UART \
		"$BUILT_KERNEL" "$KERNEL"
fi
STAGE=$(mktemp -d "$OUTPUT/stage.XXXXXX")
trap 'rm -f "$STAGE/tmp/filetab" "$STAGE/tmp/inotab" "$STAGE/tmp/proctab" "$STAGE/tmp/clistbound"
rm -f "$STAGE/tmp/vmstat" "$STAGE/tmp/iostat" "$STAGE/tmp/pstat"
rmdir "$STAGE/tmp" "$STAGE" 2>/dev/null || :' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$STAGE/tmp"
for program in filetab inotab proctab clistbound; do
	cp "$FIXTURES/$program" "$STAGE/tmp/$program"
	chmod 755 "$STAGE/tmp/$program"
done
for reader in $READERS; do
	cp "$TOP/$reader" "$STAGE/tmp/${reader##*/}"
	chmod 755 "$STAGE/tmp/${reader##*/}"
done
cp "$DISK" "$OUTPUT/disk.img"
(cd "$STAGE" && "$FSUTIL" --add --partition=1 "$OUTPUT/disk.img" tmp/filetab tmp/inotab tmp/proctab tmp/clistbound \
	tmp/vmstat tmp/iostat tmp/pstat)
"$FSUTIL" --check --partition=1 "$OUTPUT/disk.img" >"$OUTPUT/fsck.txt" 2>&1 ||
	missing "the composed image fails fsutil --check; see $OUTPUT/fsck.txt"
"$FLASHIMG" "$OUTPUT/disk.img" "$OUTPUT/flash.bin" >"$OUTPUT/flashimg.txt"

"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test
# -u: tools/run_logged_gate.py ends a run that prints zero bytes for its idle
# limit, and Python block-buffers a piped stdout until exit, so every
# stage line must reach the log as it is printed.
if [ "$EXPECT" = pass ]; then
	"$PYTHON" -I -u "$SCRIPT_DIR/check-capacity.py" renode "$KERNEL" "$OUTPUT/flash.bin" "$OUTPUT" \
		"$CONFIG"
	exit 0
fi
# refuse: the run's own exit status is the first condition, its transcript
# line the second; a boot that failed for another reason matches neither.
status=0
"$PYTHON" -I -u "$SCRIPT_DIR/check-capacity.py" renode "$KERNEL" "$OUTPUT/flash.bin" "$OUTPUT" \
	"$CONFIG" >"$OUTPUT/refusal.txt" 2>&1 || status=$?
cat "$OUTPUT/refusal.txt"
if [ "$status" -ne 1 ]; then
	echo "check-capacity: $CONFIG exited $status; a kernel without kern.capacity must exit 1" >&2
	exit 1
fi
if ! grep -F 'FAIL kern.capacity rows before the runs: []' "$OUTPUT/refusal.txt" >/dev/null; then
	echo "check-capacity: $CONFIG failed, but not at the missing kern.capacity record" >&2
	exit 1
fi
echo "check-capacity: $CONFIG refused as required: kern.capacity is absent"
