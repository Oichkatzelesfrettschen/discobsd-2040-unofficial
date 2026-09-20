#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 source-root ELF-image" >&2
	exit 2
fi

source_root=$1
image=$2
symbol_reader=${GCCPREFIX:-arm-none-eabi}-nm

if [ ! -d "$source_root" ] || [ ! -f "$image" ]; then
	echo "resize closure: source root or ELF image is absent" >&2
	exit 1
fi
symbols=$("$symbol_reader" "$image")
for forbidden_symbol in _ctype_ _doscan scanf sscanf
do
	if printf '%s\n' "$symbols" |
	    awk -v symbol="$forbidden_symbol" '$NF == symbol { found = 1 } END { exit !found }'
	then
		echo "resize closure: utilbox retains $forbidden_symbol" >&2
		exit 1
	fi
done

echo "resize utilbox symbol closure passed"
