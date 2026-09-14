#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: $0 source-root arm-gcc target-machine" >&2
	exit 2
fi

source_root=$1
arm_compiler=$2
target_machine=$3
script_directory=$(CDPATH= cd "$(dirname "$0")" && pwd)
assembler=$source_root/tools/bin/as
archiver=$source_root/tools/bin/ar
linker=$source_root/tools/bin/ld
symbol_reader=$source_root/tools/bin/nm
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

"$arm_compiler" -std=gnu17 -mcpu=cortex-m0plus -mabi=aapcs \
    -mlittle-endian -mthumb -mfloat-abi=soft -nostdinc \
    -I"$source_root/include" -Os -ffreestanding -fno-builtin \
    -fno-jump-tables -S "$script_directory/aout_link_contract.c" \
    -o "$temporary_directory/aout_link_contract.s"
"$assembler" "$temporary_directory/aout_link_contract.s" \
    -o "$temporary_directory/aout_link_contract.o"

archive_index=0
for archive_path in \
    "$source_root/lib/libc_aout/libc.a" \
    "$source_root/distrib/obj/boardlibc.$target_machine/libc.a"
do
	for required_member in ctermid.o raise.o
	do
		member_count=$(
			"$archiver" t "$archive_path" |
			    awk -v member="$required_member" '$0 == member { count++ } END { print count + 0 }'
		)
		if [ "$member_count" -ne 1 ]; then
			echo "$archive_path: expected one $required_member member" >&2
			exit 1
		fi
	done

	linked_image=$temporary_directory/linked-$archive_index
	# The legacy linker parses -T with atol(), so name 0x20000000 in decimal.
	"$linker" -T536870912 -o "$linked_image" \
	    "$temporary_directory/aout_link_contract.o" "$archive_path"
	first_defined_address=$(
		"$symbol_reader" -n "$linked_image" |
		    awk 'NF == 3 && $2 != "U" { print $1; exit }'
	)
	if [ "$first_defined_address" != 20000000 ]; then
		echo "$archive_path: first symbol starts at $first_defined_address, expected 20000000" >&2
		exit 1
	fi
	undefined_symbols=$("$symbol_reader" -u "$linked_image")
	if [ -n "$undefined_symbols" ]; then
		echo "$archive_path: linked contract has undefined symbols" >&2
		printf '%s\n' "$undefined_symbols" >&2
		exit 1
	fi
	archive_index=$((archive_index + 1))
done

echo "a.out libc contract tests passed"
