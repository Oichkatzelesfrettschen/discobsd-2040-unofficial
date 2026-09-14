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

# RP2040 uses 32-bit pointers. These sizes prove NBUF=10, BUFHSZ=4,
# NNAMECACHE=4, and NCHHASH=4 against the linked target structures.
check_symbol buf 1b8
check_symbol bufdata 2800
check_symbol bufhash 30
check_symbol namecache d0
check_symbol nchash 20

if [ "$fail" -eq 0 ]; then
	echo "$elf: cache_index_bytes=288 previous_index_bytes=1672 saved_bytes=1384 nbuf=10"
fi
exit "$fail"
