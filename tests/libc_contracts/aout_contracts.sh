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
tool_directory=$source_root/tools/bin/$target_machine
assembler=$tool_directory/as
archiver=$tool_directory/ar
indexer=$tool_directory/ranlib
linker=$tool_directory/ld
symbol_reader=$tool_directory/nm
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

for required_tool in "$assembler" "$archiver" "$indexer" "$linker" "$symbol_reader"
do
	if [ ! -x "$required_tool" ]; then
		echo "a.out libc contracts require a completed MACHINE=$target_machine build: $required_tool is absent" >&2
		exit 2
	fi
done

"$arm_compiler" -std=gnu17 -Wall -Wextra -Werror -mcpu=cortex-m0plus -mabi=aapcs \
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
	if [ ! -f "$archive_path" ]; then
		echo "a.out libc contracts require a completed MACHINE=$target_machine distribution: $archive_path is absent" >&2
		exit 2
	fi
	for required_source in \
	    "$source_root/lib/libc/gen/confstr.c" \
	    "$source_root/lib/libc/gen/ctermid.c" \
	    "$source_root/lib/libc/gen/difftime.c" \
	    "$source_root/lib/libc/gen/raise.c" \
	    "$source_root/lib/libc/gen/strnlen.c" \
	    "$source_root/lib/libc/gen/strndup.c" \
	    "$source_root/lib/libc/gen/stpcpy.c" \
	    "$source_root/lib/libc/gen/stpncpy.c" \
	    "$source_root/lib/libc/gen/sysconf.c" \
	    "$source_root/lib/libc/gen/vis.c" \
	    "$source_root/lib/libc/string/explicit_bzero.c" \
	    "$source_root/lib/libc/string/timingsafe_bcmp.c"
	do
		if [ "$required_source" -nt "$archive_path" ]; then
			echo "$archive_path: stale against $required_source; rebuild the distribution" >&2
			exit 1
		fi
	done
	for required_member in confstr.o ctermid.o difftime.o explicit_bzero.o \
	    raise.o sysconf.o strnlen.o strndup.o stpcpy.o stpncpy.o \
	    timingsafe_bcmp.o vis.o
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
	for missing_member in strnlen strndup stpcpy stpncpy
	do
		damaged_archive=$temporary_directory/damaged.a
		cp "$archive_path" "$damaged_archive"
		"$archiver" d "$damaged_archive" "$missing_member.o"
		"$indexer" "$damaged_archive"
		if "$linker" -T536870912 -o "$temporary_directory/damaged-image" \
		    "$temporary_directory/aout_link_contract.o" "$damaged_archive" \
		    > "$temporary_directory/damaged.log" 2>&1; then
			echo "$archive_path: missing $missing_member still links" >&2
			exit 1
		fi
		if ! grep -F "$missing_member" "$temporary_directory/damaged.log" >/dev/null; then
			echo "$archive_path: missing-member control fails for another reason" >&2
			cat "$temporary_directory/damaged.log" >&2
			exit 1
		fi
	done
	archive_index=$((archive_index + 1))
done

echo "a.out libc contract tests passed; eight missing-string-member controls reject"
