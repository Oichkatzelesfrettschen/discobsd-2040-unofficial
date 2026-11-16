#!/bin/sh
# Verify the RP2040 buffer and pathname cache allocation contract in a linked
# kernel. The exact symbol sizes bind the configured counts to the ARM ABI;
# source constants alone cannot prove that the intended SRAM was recovered.
set -eu

elf=
layout=single
while [ $# -gt 0 ]; do
	case $1 in
	--elf) elf=$2; shift 2 ;;
	--layout) layout=$2; shift 2 ;;
	*) echo "usage: $0 --elf ELF [--layout single|multi]" >&2; exit 2 ;;
	esac
done
case $layout in
single | multi) ;;
*) echo "$0: --layout is single or multi" >&2; exit 2 ;;
esac

[ -n "$elf" ] || { echo "$0: --elf is required" >&2; exit 2; }
[ -r "$elf" ] || { echo "$0: $elf: unreadable" >&2; exit 2; }

NM=${NM:-arm-none-eabi-nm}
symbols=$("$NM" -S --format=posix "$elf")
fail=0

check_symbol()
{
	symbol_name=$1
	expected_size=$2
	actual_size=$(printf '%s\n' "$symbols" |
	    awk -v name="$symbol_name" '$1 == name { print $4 }')
	if [ -z "$actual_size" ]; then
		echo "$elf: $symbol_name is absent" >&2
		fail=1
	elif [ "$actual_size" != "$expected_size" ]; then
		echo "$elf: $symbol_name is 0x$actual_size bytes, expected 0x$expected_size" >&2
		fail=1
	fi
}

check_absent()
{
	symbol_name=$1
	if printf '%s\n' "$symbols" |
	    awk -v name="$symbol_name" '$1 == name { found = 1 } END { exit !found }'; then
		echo "$elf: $symbol_name remains linked" >&2
		fail=1
	fi
}

# RP2040 uses 32-bit pointers. These sizes prove NBUF=4, NNAMECACHE=4,
# the mount count, linear cache layouts, and compact inode fields against the
# linked target structures. The single layout is the PICO kernels' NMOUNT=1
# with SINGLE_UFS_ROOT, which drops each inode's device and filesystem fields
# and each mount's name strings. The multi layout is PICOCALC's NMOUNT=2
# without it: two 0x4c8-byte mounts and 24 inodes of 0x5c bytes. Each mount
# includes the four-byte m_device_lock retained through device-node release.
check_symbol buf 90
check_symbol bfreelist 6c
check_symbol bufdata 1000
check_symbol namecache 90
check_symbol nchclock 4
if [ "$layout" = single ]; then
	check_symbol mount 410
	check_symbol inode 7e0
else
	check_symbol mount 990
	check_symbol inode 8a0
fi

check_absent bufhash
check_absent nchash
check_absent nchhead
check_absent nchtail
check_absent ihead

if [ "$fail" -eq 0 ]; then
	if [ "$layout" = single ]; then
		echo "$elf: cache_bytes=400 buffer_pool_bytes=4240 inode_bytes=2016 mount_bytes=1040 mount_guard_bytes=4 nbuf=4 nmount=1"
	else
		echo "$elf: cache_bytes=400 buffer_pool_bytes=4240 inode_bytes=2208 mount_bytes=2448 mount_guard_bytes=8 nbuf=4 nmount=2"
	fi
fi
exit "$fail"
