#!/bin/sh
# Verify the linked RP2040 flash-swap SRAM and allocation contract.
set -eu

elf=
flash_object=
vm_swap_object=
swap_object=
swapram_object=
while [ "$#" -gt 0 ]; do
	case $1 in
	--elf) elf=$2; shift 2 ;;
	--flash-object) flash_object=$2; shift 2 ;;
	--vm-swap-object) vm_swap_object=$2; shift 2 ;;
	--swap-object) swap_object=$2; shift 2 ;;
	--swapram-object) swapram_object=$2; shift 2 ;;
	*) echo "usage: $0 --elf ELF --flash-object OBJ --vm-swap-object OBJ --swap-object OBJ [--swapram-object OBJ]" >&2; exit 2 ;;
	esac
done

for required_path in "$elf" "$flash_object" "$vm_swap_object" "$swap_object"; do
	[ -r "$required_path" ] || {
		echo "$0: required input is unreadable: $required_path" >&2
		exit 2
	}
done
if [ -n "$swapram_object" ] && [ ! -r "$swapram_object" ]; then
	echo "$0: swapram object is unreadable: $swapram_object" >&2
	exit 2
fi

NM=${NM:-arm-none-eabi-nm}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
symbols=$("$NM" -S --format=posix "$elf")
fail=0

check_symbol_size()
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

check_symbol()
{
	symbol_name=$1
	if ! printf '%s\n' "$symbols" |
	    awk -v name="$symbol_name" '$1 == name { found = 1 } END { exit !found }'
	then
		echo "$elf: $symbol_name is absent" >&2
		fail=1
	fi
}

check_relocation()
{
	object_path=$1
	symbol_name=$2
	if ! "$OBJDUMP" -r "$object_path" |
	    awk -v name="$symbol_name" '$NF == name { found = 1 } END { exit !found }'
	then
		echo "$object_path: relocation to $symbol_name is absent" >&2
		fail=1
	fi
}

check_symbol_size flscratch 100
check_symbol flash_swap_append
check_symbol flash_swap_rewrite
check_symbol malloc3_contiguous
check_relocation "$flash_object" flash_swap_append
check_relocation "$flash_object" flash_swap_rewrite
check_relocation "$vm_swap_object" malloc3_contiguous
check_relocation "$swap_object" malloc3_contiguous
if [ -n "$swapram_object" ]; then
	check_relocation "$swapram_object" malloc3_contiguous
fi

if [ "$fail" -eq 0 ]; then
	echo "$elf: flash scratch 256 bytes; image, temporary-device, and allocator paths linked"
fi
exit "$fail"
