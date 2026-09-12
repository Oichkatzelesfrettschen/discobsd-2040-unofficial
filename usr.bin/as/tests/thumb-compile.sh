#!/bin/sh
#
# Test (b): assembly written by arm-none-eabi-gcc for three programs of the
# tree assembles without error, and its text matches what arm-none-eabi-as
# produces from the same input outside the relocated fields.
#
# These carry what hand-written inputs do not: switch tables that read the
# difference of two labels into a .byte, code split across .text and
# .text.startup, and calls to symbols this object does not define.
#
set -eu

AS=${AS:?set AS to the assembler under test}
GNUAS=${GNUAS:-arm-none-eabi-as}
CC=${CC:?set CC to the cross compiler command}
CPU=${CPU:-cortex-m0plus}
TOPSRC=${TOPSRC:?set TOPSRC to the top of the tree}
here=$(dirname "$0")
work=${WORK:-.}

fail=0
for src in bin/cat/cat.c bin/echo/echo.c usr.bin/wc/wc.c
do
	name=$(basename "$src" .c)
	$CC -Os -fcommon -S "$TOPSRC/$src" -o "$work/$name.s"
	$GNUAS -mcpu="$CPU" -mthumb -o "$work/$name.gnu.o" "$work/$name.s"
	if ! $AS "$work/$name.s" -o "$work/$name.aout"; then
		echo "compile: $name.s did not assemble"
		fail=$((fail + 1))
		continue
	fi
	if ! python3 "$here/thumb-aoutdiff.py" "$work/$name.s" \
	    "$work/$name.aout" "$work/$name.gnu.o"
	then
		fail=$((fail + 1))
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "compile: $fail of 3 programs disagree"
	exit 1
fi
echo "compile: 3 compiler-written programs assemble and agree"
