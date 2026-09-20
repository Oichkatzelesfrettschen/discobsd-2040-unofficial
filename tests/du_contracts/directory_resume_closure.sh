#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 nm object" >&2
	exit 2
fi

symbol_reader=$1
object=$2
symbols=$($symbol_reader -u "$object")

for forbidden_symbol in seekdir telldir
do
	if printf '%s\n' "$symbols" |
	    awk -v symbol="$forbidden_symbol" \
	    '$NF == symbol || $NF == "_" symbol { found = 1 } \
	    END { exit !found }'
	then
		echo "du directory resume retains $forbidden_symbol" >&2
		exit 1
	fi
done

echo "du directory resume symbol closure passed"
