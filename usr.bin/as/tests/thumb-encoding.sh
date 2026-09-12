#!/bin/sh
#
# Test (a): every ARMv6-M instruction form, encoded by this tree's Thumb
# assembler and by arm-none-eabi-as, compared halfword by halfword.
#
# The inputs below name each 16-bit encoding the architecture defines plus
# the 32-bit BL, MSR, MRS, DMB, DSB and ISB, and exercise the directives,
# local labels, PC-relative forms and literal pool alongside them. The last
# is assembly written by the tree's own Thumb-1 compiler, whose literal
# pool and section use differ from the cross compiler's.
#
set -eu

AS=${AS:?set AS to the assembler under test}
GNUAS=${GNUAS:-arm-none-eabi-as}
CPU=${CPU:-cortex-m0plus}
here=$(dirname "$0")
work=${WORK:-.}

fail=0
for src in "$here"/thumb-shift.s "$here"/thumb-alu.s "$here"/thumb-ldst.s \
           "$here"/thumb-misc.s "$here"/thumb-branch.s \
           "$here"/thumb-directives.s "$here"/thumb-native.s
do
	name=$(basename "$src" .s)
	$GNUAS -mcpu="$CPU" -mthumb -o "$work/$name.gnu.o" "$src"
	$AS "$src" -o "$work/$name.aout"
	if ! python3 "$here/thumb-aoutdiff.py" "$src" \
	    "$work/$name.aout" "$work/$name.gnu.o"
	then
		fail=$((fail + 1))
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "encoding: $fail of 7 inputs disagree with $GNUAS"
	exit 1
fi
echo "encoding: 7 inputs agree with $GNUAS on every unrelocated halfword"
