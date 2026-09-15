#!/bin/sh
# Verify the linked RP2040 flash-swap SRAM and allocation contract.
set -eu

elf=
flash_object=
vm_swap_object=
exec_object=
swap_object=
swapram_object=
while [ "$#" -gt 0 ]; do
	case $1 in
	--elf) elf=$2; shift 2 ;;
	--flash-object) flash_object=$2; shift 2 ;;
	--vm-swap-object) vm_swap_object=$2; shift 2 ;;
	--exec-object) exec_object=$2; shift 2 ;;
	--swap-object) swap_object=$2; shift 2 ;;
	--swapram-object) swapram_object=$2; shift 2 ;;
	*) echo "usage: $0 --elf ELF --flash-object OBJ --vm-swap-object OBJ --exec-object OBJ --swap-object OBJ [--swapram-object OBJ]" >&2; exit 2 ;;
	esac
done

for required_path in "$elf" "$flash_object" "$vm_swap_object" \
    "$exec_object" "$swap_object"; do
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

check_symbol_bss()
{
	symbol_name=$1
	symbol_type=$(printf '%s\n' "$symbols" |
	    awk -v name="$symbol_name" '$1 == name { print $2 }')
	case $symbol_type in
	b|B) ;;
	*)
		echo "$elf: $symbol_name is type ${symbol_type:-absent}, expected bss" >&2
		fail=1
		;;
	esac
}

check_symbol_address()
{
	symbol_name=$1
	expected_address=$2
	actual_address=$(printf '%s\n' "$symbols" |
	    awk -v name="$symbol_name" '$1 == name { print $3 }')
	if [ -z "$actual_address" ]; then
		echo "$elf: $symbol_name is absent" >&2
		fail=1
	elif [ "$actual_address" != "$expected_address" ]; then
		echo "$elf: $symbol_name is at 0x$actual_address, expected 0x$expected_address" >&2
		fail=1
	fi
}

if printf '%s\n' "$symbols" |
    awk '$1 == "usb_tx_ring" { found = 1 } END { exit !found }'
then
	check_symbol_address flpage 50100240
	check_symbol_address flpage_end 50100640
	# DPSRAM is execute-never; the boot2 copy that flash_enter_xip
	# calls stays in ordinary bss.
	check_symbol_size boot2_copy 100
	check_symbol_bss boot2_copy
	check_symbol_address flscratch 50100f00
	check_symbol_address flscratch_end 50101000
	scratch_description="USB DPSRAM 0x50100f00..0x50100fff"
else
	check_symbol_address flpage 20040000
	check_symbol_address flpage_end 20040400
	check_symbol_address boot2_copy 20040400
	check_symbol_address boot2_copy_end 20040500
	check_symbol_address flscratch 20041f00
	check_symbol_address flscratch_end 20042000
	scratch_description="SRAM5 0x20041f00..0x20041fff"
fi
check_symbol flash_swap_append
check_symbol flash_swap_rewrite
check_symbol malloc3_contiguous_next
check_symbol_size swapent 74
check_relocation "$flash_object" flash_swap_append
check_relocation "$flash_object" flash_swap_rewrite
check_relocation "$vm_swap_object" malloc3_contiguous_next
check_relocation "$vm_swap_object" swap_cursor_publish
check_relocation "$exec_object" malloc3_contiguous_next
check_relocation "$exec_object" swap_cursor_publish
check_relocation "$swap_object" malloc3_contiguous_next
check_relocation "$swap_object" swap_cursor_publish
if [ -n "$swapram_object" ]; then
	check_relocation "$swapram_object" malloc3_contiguous_next
	check_relocation "$swapram_object" swap_cursor_publish
	check_relocation "$swapram_object" swap_with_buf
fi

if [ "$fail" -eq 0 ]; then
	echo "$elf: flash work buffers use $scratch_description; image, temporary-device, and allocator paths linked"
fi
exit "$fail"
