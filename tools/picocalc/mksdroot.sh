#!/bin/sh
# Build the root filesystem a PicoCalc kernel mounts from the SD card when
# the PicoCalc UF2 loader started it (sdrootattach() in
# sys/arch/rp2040/dev/sd.c takes the card's first MBR partition of type b7).
#
# Usage: sh tools/picocalc/mksdroot.sh FSUTIL DESTDIR MANIFEST KBYTES PART OUT
#
# FSUTIL is the host fsutil, DESTDIR the installed tree and MANIFEST the
# composed manifest of the board's image rule (distrib/rp2040/_manifest),
# so the files are the ones the flash root carries. KBYTES is the
# partition's size in kbytes and PART its letter (a to d), which is the
# MBR slot the kernel finds and so the device /etc/fstab names. OUT
# receives the bare filesystem, with no partition table, ready to be
# written at the partition's first sector.
#
# Three things differ from the flash root: /etc/fstab names /dev/sd0PART,
# because fsck -p and mount -a in /etc/rc read it and the fl0 region under
# the loader holds other applications' bytes; /etc/ttys runs a getty on
# tty0, the USB-C serial port; and the image carries the block and raw
# nodes of all four MBR partitions, which the flash root leaves to mknod
# for want of inodes. The inode count is fsutil's default, one per 16
# kbytes.

set -eu

if [ $# -ne 6 ]; then
	echo "usage: mksdroot.sh FSUTIL DESTDIR MANIFEST KBYTES PART OUT" >&2
	exit 2
fi
FSUTIL=$1
DESTDIR=$2
MANIFEST=$3
KBYTES=$4
PART=$5
OUT=$6

[ -x "$FSUTIL" ] || { echo "mksdroot: no fsutil at $FSUTIL" >&2; exit 2; }
[ -d "$DESTDIR" ] || { echo "mksdroot: no tree at $DESTDIR" >&2; exit 2; }
[ -r "$MANIFEST" ] || { echo "mksdroot: no manifest at $MANIFEST" >&2; exit 2; }
case $KBYTES in
''|*[!0-9]*) echo "mksdroot: KBYTES must be a decimal count" >&2; exit 2 ;;
esac
# fsutil's own minimum is ten 1-kbyte blocks; a root below 1 MiB holds
# less than the flash root does.
[ "$KBYTES" -ge 1024 ] || { echo "mksdroot: KBYTES below 1024" >&2; exit 2; }
case $PART in
a) MINOR=1 ;; b) MINOR=2 ;; c) MINOR=3 ;; d) MINOR=4 ;;
*) echo "mksdroot: PART must be one of a b c d" >&2; exit 2 ;;
esac

# The image is built beside OUT and renamed over it once both checks pass,
# so a failed run leaves the previous OUT intact and no truncated target
# for make to treat as current. The scratch tree copies read-only
# directories, which chmod reopens before the plain recursive remove.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/mksdroot.XXXXXX")
NEW=$OUT.new.$$
trap 'chmod -R u+w "$WORK" 2>/dev/null; rm -r "$WORK"; rm -f "$NEW"' EXIT HUP INT TERM

cp -pR "$DESTDIR" "$WORK/tree"
chmod u+w "$WORK/tree/etc" "$WORK/tree/etc/fstab" 2>/dev/null || true
printf '/dev/sd0%s\t/\tufs\trw,async,noaccesstime\t1\t1\n' "$PART" \
    > "$WORK/tree/etc/fstab"

# A login on tty0 as well as the panel: UART0 reaches the USB-C port
# through the board's CH340C, so a host can watch and drive the system
# while the panel is the kernel's console. Like the console it is marked
# insecure, so root logs in through operator and su.
chmod u+w "$WORK/tree/etc/ttys"
sed 's/^\(tty0[[:space:]].*[[:space:]]\)off[[:space:]][[:space:]]*secure$/\1on insecure/' \
    "$WORK/tree/etc/ttys" > "$WORK/ttys"
[ "$(grep -c '^tty0[[:space:]].*[[:space:]]on insecure$' "$WORK/ttys")" -eq 1 ] || {
	echo "mksdroot: /etc/ttys has no tty0 line of the form ... off secure" >&2
	exit 1
}
cp "$WORK/ttys" "$WORK/tree/etc/ttys"

# The manifest keeps every entry of the flash root; the partition nodes it
# lacks follow, each named once.
cp "$MANIFEST" "$WORK/manifest"
for n in 1 2 3 4; do
	letter=$(printf '%s' "abcd" | cut -c "$n")
	for kind in b c; do
		if [ "$kind" = b ]; then
			name=/dev/sd0$letter major=5
		else
			name=/dev/rsd0$letter major=11
		fi
		grep -qx "${kind}dev $name" "$WORK/manifest" ||
			printf '%sdev %s\nmajor %s\nminor %s\n' \
			    "$kind" "$name" "$major" "$n" >> "$WORK/manifest"
	done
done

rm -f "$NEW"
"$FSUTIL" --new --size="$KBYTES" --manifest="$WORK/manifest" "$NEW" \
    "$WORK/tree" >/dev/null
"$FSUTIL" --check "$NEW"
size=$(wc -c < "$NEW")
[ "$size" -eq $((KBYTES * 1024)) ] || {
	echo "mksdroot: $OUT holds $size bytes, not $((KBYTES * 1024))" >&2
	exit 1
}
mv "$NEW" "$OUT"
echo "mksdroot: $OUT, $KBYTES kbytes, root /dev/sd0$PART (minor $MINOR)"
