#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
	echo "usage: $0 source-root arm-compiler target-machine self-test" >&2
	exit 2
fi

source_root=$1
arm_compiler=$2
target_machine=$3
self_test=$4
script_directory=$(CDPATH= cd "$(dirname "$0")" && pwd)
archive_path=$source_root/lib/libc.a
archiver=${arm_compiler%gcc}ar
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

case $self_test in
yes|no) ;;
*) echo "self-test must be yes or no" >&2; exit 2 ;;
esac

verify_members()
{
	checked_archive=$1
	for required_member in reallocarray.o memmem.o getdelim.o getline.o
	do
		member_count=$(
			"$archiver" t "$checked_archive" |
			    awk -v member="$required_member" \
			    '$0 == member { count++ } END { print count + 0 }'
		)
		if [ "$member_count" -ne 1 ]; then
			echo "$checked_archive: expected one $required_member member" >&2
			return 1
		fi
	done
}

if [ ! -f "$archive_path" ]; then
	echo "$archive_path: build the RP2040 libc before this gate" >&2
	exit 2
fi

verify_members "$archive_path"

target_cflags="-std=c17 -pedantic-errors -Wall -Wextra -Werror
-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
-fno-common -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb
-mfloat-abi=soft -ffreestanding -fno-builtin -nostdinc
-I$source_root/include -Os"

if "$arm_compiler" $target_cflags \
    -c "$script_directory/array_memory_gnu_extension_bad.c" \
    -o "$temporary_directory/array_memory_gnu_extension_bad.o" \
    >"$temporary_directory/array_memory_gnu_extension_bad.log" 2>&1
then
	echo "strict C17 negative control accepted GNU typeof syntax" >&2
	exit 1
fi

for source_file in \
    lib/libc/stdlib/reallocarray.c \
    lib/libc/string/memmem.c \
    lib/libc/stdio/getdelim.c \
    lib/libc/stdio/getline.c
do
	object_name=$(basename "$source_file" .c)
	"$arm_compiler" $target_cflags -c "$source_root/$source_file" \
	    -o "$temporary_directory/$object_name.strict.o"
done

if [ "$self_test" = yes ]; then
	damaged_archive=$temporary_directory/libc-missing-memmem.a
	cp "$archive_path" "$damaged_archive"
	"$archiver" d "$damaged_archive" memmem.o
	if verify_members "$damaged_archive" >/dev/null 2>&1; then
		echo "archive negative control accepted a missing memmem.o" >&2
		exit 1
	fi
	echo "array-memory archive negative control passed"
fi

"$arm_compiler" $target_cflags \
    -c "$script_directory/array_memory_link_contract.c" \
    -o "$temporary_directory/array_memory_link_contract.o"

link_map=$temporary_directory/array_memory_link_contract.map
"$arm_compiler" -n -nostartfiles -fno-dwarf2-cfi-asm \
    -Wl,--warn-rwx-segments -Wl,--fatal-warnings \
    -T"$source_root/lib/elf32-arm.ld" "$source_root/lib/crt0.o" \
    -L"$source_root/lib" -o "$temporary_directory/array_memory_link_contract.elf" \
    "$temporary_directory/array_memory_link_contract.o" -lc -Wl,-Map="$link_map"

for required_member in reallocarray.o memmem.o getdelim.o getline.o
do
	selection_count=$(awk -v member="($required_member)" \
	    'index($0, "libc.a" member) { count++ } END { print count + 0 }' \
	    "$link_map")
	if [ "$selection_count" -lt 1 ]; then
		echo "$link_map: linker selected no $required_member" >&2
		exit 1
	fi
done

echo "array-memory strict C17, libc archive and link-map contracts passed"
