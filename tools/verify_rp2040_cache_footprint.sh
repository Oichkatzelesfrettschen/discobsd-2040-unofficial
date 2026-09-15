#!/bin/sh
# Verify the RP2040 buffer and pathname cache allocation contract in a linked
# kernel. The exact symbol sizes bind the configured counts to the ARM ABI;
# source constants alone cannot prove that the intended SRAM was recovered.
set -eu

elf=
while [ $# -gt 0 ]; do
	case $1 in
	--elf) elf=$2; shift 2 ;;
	*) echo "usage: $0 --elf ELF" >&2; exit 2 ;;
	esac
done

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
# NMOUNT=1, linear cache layouts, and compact inode fields against the linked
# target structures.
check_symbol buf 90
check_symbol bfreelist 6c
check_symbol bufdata 1000
check_symbol namecache 90
check_symbol nchclock 4
check_symbol mount 40c
check_symbol inode 7e0

check_absent bufhash
check_absent nchash
check_absent nchhead
check_absent nchtail
check_absent ihead

if [ "$fail" -eq 0 ]; then
	echo "$elf: cache_bytes=400 buffer_pool_bytes=4240 inode_bytes=2016 mount_bytes=1036 allocation_saved_bytes=8720 nbuf=4 nmount=1"
fi
exit "$fail"
