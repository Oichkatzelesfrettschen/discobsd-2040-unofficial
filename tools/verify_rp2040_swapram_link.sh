#!/bin/sh
# Verify that a linked RP2040 kernel carries exactly the SwapRAM tier its
# PARAM selects. With -DSWAPRAM the kernel must define the six entry points
# vm_swap.c calls, vm_swap.o must reference each of them, and the pool
# swapram_pool_mem must measure SWAPRAM_KB kilobytes (64 when the option is
# absent, matching machine/swapram.h), follow the ordinary kernel bss, stay
# inside kernel RAM, and retain its eight-byte alignment. A SwapRAM kernel
# must also carry one 1,040-byte codec workspace, with packed-text restoration
# referencing its owner API, the three former private workspaces absent, and
# every retired variable-window epoch symbol absent. Without -DSWAPRAM none
# of the tier symbols may appear. An object compiled under an earlier PARAM is
# the fault this catches: it links cleanly and runs the wrong tier.
#
# usage: verify_rp2040_swapram_link.sh --elf unix.elf --object vm_swap.o \
#            --exec-object exec_hsaout.o \
#            --param "-DPICO -DSWAPRAM -DSWAPRAM_KB=64 ..."
set -eu

elf=
object=
exec_object=
sysctl_object=
param=
while [ $# -gt 0 ]; do
	case $1 in
	--elf) elf=$2; shift 2 ;;
	--object) object=$2; shift 2 ;;
	--exec-object) exec_object=$2; shift 2 ;;
	--sysctl-object) sysctl_object=$2; shift 2 ;;
	--param) param=$2; shift 2 ;;
	*) echo "usage: $0 --elf ELF --object OBJ --exec-object OBJ --sysctl-object OBJ --param PARAM" >&2; exit 2 ;;
	esac
done
[ -n "$elf" ] && [ -n "$object" ] && [ -n "$exec_object" ] && \
    [ -n "$sysctl_object" ] || {
	echo "$0: --elf, --object, --exec-object, and --sysctl-object are required" >&2
	exit 2
}
[ -r "$elf" ] || { echo "$0: $elf: unreadable" >&2; exit 2; }
[ -r "$object" ] || { echo "$0: $object: unreadable" >&2; exit 2; }
[ -r "$exec_object" ] || { echo "$0: $exec_object: unreadable" >&2; exit 2; }
[ -r "$sysctl_object" ] || { echo "$0: $sysctl_object: unreadable" >&2; exit 2; }

NM=${NM:-arm-none-eabi-nm}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
entries="swapram_reserve swapram_put swapram_commit swapram_cancel swapram_present swapram_in"
retired_epoch_symbols="swapram_epoch swapram_nlarge swapram_ceiling \
swapram_enter_large swapram_leave_large swapram_inherit swapram_set_epoch \
swapram_init user_top sr_nlarge sr_large_want sr_small_want sr_large_seq \
sr_poke"

enabled=0
kb=64
for word in $param; do
	case $word in
	-DSWAPRAM) enabled=1 ;;
	-DSWAPRAM_KB=*) kb=${word#-DSWAPRAM_KB=} ;;
	esac
done

fail=0
syms=$("$NM" -S "$elf")
relocs=$("$OBJDUMP" -r "$object")
exec_relocs=$("$OBJDUMP" -r "$exec_object")
sysctl_relocs=$("$OBJDUMP" -r "$sysctl_object")

symbol_address()
{
	printf '%s\n' "$syms" |
	    awk -v symbol_name="$1" '$NF == symbol_name { print $1 }'
}

if [ "$enabled" -eq 1 ]; then
	for e in $entries; do
		printf '%s\n' "$syms" | grep " T $e\$" >/dev/null ||
		    { echo "$elf: $e is not linked" >&2; fail=1; }
		printf '%s\n' "$relocs" | grep "[[:space:]]$e\$" >/dev/null ||
		    { echo "$object: no relocation to $e" >&2; fail=1; }
	done
	size=$(printf '%s\n' "$syms" | awk '$4 == "swapram_pool_mem" { print $2 }')
	want=$(printf '%x' $((kb * 1024)))
	[ -n "$size" ] || { echo "$elf: swapram_pool_mem is not linked" >&2; fail=1; }
	if [ -n "$size" ] && [ "$((0x$size))" -ne "$((0x$want))" ]; then
		echo "$elf: swapram_pool_mem is 0x$size bytes, PARAM asks for ${kb} KB" >&2
		fail=1
	fi
	pool_address=$(symbol_address swapram_pool_mem)
	bss_end_address=$(symbol_address _ebss)
	kernel_start_address=$(symbol_address __kernel_data_start)
	kernel_end_address=$(symbol_address __kernel_data_end)
	[ -n "$pool_address" ] || {
		echo "$elf: swapram_pool_mem address is absent" >&2
		fail=1
	}
	[ -n "$bss_end_address" ] || {
		echo "$elf: _ebss address is absent" >&2
		fail=1
	}
	[ -n "$kernel_start_address" ] || {
		echo "$elf: __kernel_data_start address is absent" >&2
		fail=1
	}
	[ -n "$kernel_end_address" ] || {
		echo "$elf: __kernel_data_end address is absent" >&2
		fail=1
	}
	if [ -n "$pool_address" ] && [ -n "$size" ]; then
		pool_start_value=$((0x$pool_address))
		pool_end_value=$((pool_start_value + 0x$size))
		[ $((pool_start_value % 8)) -eq 0 ] || {
			echo "$elf: swapram_pool_mem starts at unaligned 0x$pool_address" >&2
			fail=1
		}
		if [ -n "$bss_end_address" ] &&
		    [ "$pool_start_value" -lt "$((0x$bss_end_address))" ]; then
			echo "$elf: swapram_pool_mem at 0x$pool_address precedes _ebss at 0x$bss_end_address" >&2
			fail=1
		fi
		if [ -n "$kernel_start_address" ] &&
		    [ -n "$kernel_end_address" ] &&
		    { [ "$pool_start_value" -lt "$((0x$kernel_start_address))" ] ||
		      [ "$pool_end_value" -gt "$((0x$kernel_end_address))" ]; }; then
			echo "$elf: swapram_pool_mem 0x$pool_address..$(printf '%x' "$pool_end_value") is outside kernel RAM 0x$kernel_start_address..0x$kernel_end_address" >&2
			fail=1
		fi
	fi
	# The linker script assigns the workspace an absolute address, which
	# nm -S prints without a size column: address, type A, name.
	codec_address=$(printf '%s\n' "$syms" |
	    awk '$2 == "A" && $3 == "swapram_codec_work" { print $1 }')
	[ "$codec_address" = 50100740 ] || {
		echo "$elf: swapram_codec_work is ${codec_address:-absent}, expected absolute 50100740" >&2
		fail=1
	}
	for entry in swapram_codec_acquire swapram_codec_release; do
		printf '%s\n' "$syms" | grep " T $entry\$" >/dev/null ||
		    { echo "$elf: $entry is not linked" >&2; fail=1; }
		printf '%s\n' "$exec_relocs" | grep "[[:space:]]$entry\$" >/dev/null ||
		    { echo "$exec_object: no relocation to $entry" >&2; fail=1; }
	done
	for entry in swapram_present swapram_uarea_prefix; do
		printf '%s\n' "$syms" | grep " T $entry\$" >/dev/null ||
		    { echo "$elf: $entry is not linked" >&2; fail=1; }
		printf '%s\n' "$sysctl_relocs" | grep "[[:space:]]$entry\$" >/dev/null ||
		    { echo "$sysctl_object: no relocation to $entry" >&2; fail=1; }
	done
	for retired in sr_enc sr_dec swapwork.0; do
		printf '%s\n' "$syms" | grep "[[:space:]]$retired\$" >/dev/null &&
		    { echo "$elf: retired codec workspace $retired remains linked" >&2; fail=1; }
	done
	printf '%s\n' "$syms" | grep '[[:space:]]sr_stage$' >/dev/null &&
	    { echo "$elf: retired SwapRAM evacuation stage remains linked" >&2; fail=1; }
	if [ "$kb" -le 63 ]; then
		table_size=$(printf '%s\n' "$syms" |
		    awk '$4 == "sr_tab" { print $2 }')
		[ "$table_size" = 00000180 ] || {
			echo "$elf: sr_tab is 0x${table_size:-0} bytes, expected 16 compact 24-byte records" >&2
			fail=1
		}
	fi
else
	for e in $entries swapram_pool_mem swapram_codec_work \
	    swapram_codec_acquire swapram_codec_release swapram_uarea_prefix; do
		printf '%s\n' "$syms" | grep "[[:space:]]$e\$" >/dev/null &&
		    { echo "$elf: $e is linked without -DSWAPRAM" >&2; fail=1; }
		printf '%s\n' "$relocs" | grep "[[:space:]]$e\$" >/dev/null &&
		    { echo "$object: relocation to $e without -DSWAPRAM" >&2; fail=1; }
	done
	for entry in swapram_present swapram_uarea_prefix; do
		printf '%s\n' "$sysctl_relocs" | grep "[[:space:]]$entry\$" >/dev/null &&
		    { echo "$sysctl_object: relocation to $entry without -DSWAPRAM" >&2; fail=1; }
	done
fi
for retired_epoch_symbol in $retired_epoch_symbols; do
	printf '%s\n' "$syms" |
	    grep "[[:space:]]$retired_epoch_symbol\$" >/dev/null && {
		echo "$elf: retired variable-window symbol $retired_epoch_symbol remains linked" >&2
		fail=1
	}
done
if [ "$fail" -eq 0 ]; then
	if [ "$enabled" -eq 1 ]; then
		echo "$elf: SwapRAM tier linked, pool ${kb} KB"
	else
		echo "$elf: SwapRAM tier absent"
	fi
fi
exit "$fail"
