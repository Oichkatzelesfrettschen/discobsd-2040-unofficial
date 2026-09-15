#!/bin/sh
# Verify that a linked RP2040 kernel carries exactly the SwapRAM tier its
# PARAM selects. With -DSWAPRAM the kernel must define the five entry points
# vm_swap.c calls, vm_swap.o must reference each of them, and the pool
# swapram_pool_mem must measure SWAPRAM_KB kilobytes (64 when the option is
# absent, matching machine/swapram.h). A SwapRAM kernel must also carry one
# 1,040-byte codec workspace, with packed-text restoration referencing its
# owner API and the three former private workspaces absent. Without -DSWAPRAM
# none of the tier symbols may appear. An object compiled under an earlier
# PARAM is the fault this catches: it links cleanly and runs the wrong tier.
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
entries="swapram_out swapram_put swapram_commit swapram_present swapram_in"

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
if [ "$enabled" -eq 1 ]; then
	for e in $entries; do
		printf '%s\n' "$syms" | grep -q " T $e\$" ||
		    { echo "$elf: $e is not linked" >&2; fail=1; }
		printf '%s\n' "$relocs" | grep -q "[[:space:]]$e\$" ||
		    { echo "$object: no relocation to $e" >&2; fail=1; }
	done
	size=$(printf '%s\n' "$syms" | awk '$4 == "swapram_pool_mem" { print $2 }')
	want=$(printf '%x' $((kb * 1024)))
	[ -n "$size" ] || { echo "$elf: swapram_pool_mem is not linked" >&2; fail=1; }
	if [ -n "$size" ] && [ "$((0x$size))" -ne "$((0x$want))" ]; then
		echo "$elf: swapram_pool_mem is 0x$size bytes, PARAM asks for ${kb} KB" >&2
		fail=1
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
		printf '%s\n' "$syms" | grep -q " T $entry\$" ||
		    { echo "$elf: $entry is not linked" >&2; fail=1; }
		printf '%s\n' "$exec_relocs" | grep -q "[[:space:]]$entry\$" ||
		    { echo "$exec_object: no relocation to $entry" >&2; fail=1; }
	done
	for entry in swapram_present swapram_uarea_prefix; do
		printf '%s\n' "$syms" | grep -q " T $entry\$" ||
		    { echo "$elf: $entry is not linked" >&2; fail=1; }
		printf '%s\n' "$sysctl_relocs" | grep -q "[[:space:]]$entry\$" ||
		    { echo "$sysctl_object: no relocation to $entry" >&2; fail=1; }
	done
	for retired in sr_enc sr_dec swapwork.0; do
		printf '%s\n' "$syms" | grep -q "[[:space:]]$retired\$" &&
		    { echo "$elf: retired codec workspace $retired remains linked" >&2; fail=1; }
	done
	printf '%s\n' "$syms" | grep -q '[[:space:]]sr_stage$' &&
	    { echo "$elf: retired SwapRAM evacuation stage remains linked" >&2; fail=1; }
	if [ "$kb" -le 63 ]; then
		table_size=$(printf '%s\n' "$syms" |
		    awk '$4 == "sr_tab" { print $2 }')
		[ "$table_size" = 00000258 ] || {
			echo "$elf: sr_tab is 0x${table_size:-0} bytes, expected 25 compact 24-byte records" >&2
			fail=1
		}
	fi
else
	for e in $entries swapram_pool_mem swapram_codec_work \
	    swapram_codec_acquire swapram_codec_release swapram_uarea_prefix; do
		printf '%s\n' "$syms" | grep -q "[[:space:]]$e\$" &&
		    { echo "$elf: $e is linked without -DSWAPRAM" >&2; fail=1; }
		printf '%s\n' "$relocs" | grep -q "[[:space:]]$e\$" &&
		    { echo "$object: relocation to $e without -DSWAPRAM" >&2; fail=1; }
	done
	for entry in swapram_present swapram_uarea_prefix; do
		printf '%s\n' "$sysctl_relocs" | grep -q "[[:space:]]$entry\$" &&
		    { echo "$sysctl_object: relocation to $entry without -DSWAPRAM" >&2; fail=1; }
	done
fi
if [ "$fail" -eq 0 ]; then
	if [ "$enabled" -eq 1 ]; then
		echo "$elf: SwapRAM tier linked, pool ${kb} KB"
	else
		echo "$elf: SwapRAM tier absent"
	fi
fi
exit "$fail"
