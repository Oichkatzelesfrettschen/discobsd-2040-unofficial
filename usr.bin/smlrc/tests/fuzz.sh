#!/bin/sh
#
# Differential test: compile a generated program both with smlrc and its
# Thumb-1 back end and with the host compiler in 32-bit mode, run both, and
# compare. The host build is the oracle, and -m32 makes its int, long and
# pointer widths the target's; -fwrapv makes its signed overflow wrap the way
# the generated code does, so the two agree on every program the generator
# emits. gen_diff.py keeps each program free of undefined behavior, so a
# difference is a code generator defect and nothing else.
#
# This found the defect that division and modulo are calls on ARMv6-M, which
# clobber r0 and so cannot run while a subexpression is held there. Every
# hand-written test missed it, because each had a real call in the same
# statement, which already forced the temporaries onto the stack.
#
# Usage: fuzz.sh [count]   (default 120 seeds)

set -eu

TOPSRC=${TOPSRC:?TOPSRC must be set}
CROSS=${CROSS:-arm-none-eabi}
CPU=${CPU:-cortex-m0plus}
HOST_CC=${HOST_CC:-cc}
COUNT=${1:-120}
SRCDIR=$(cd "$(dirname "$0")" && pwd)
OUT=$SRCDIR/out
# smlrc rejects a long path, so the scratch directory is kept short.
W=${W:-/tmp/smlrc-thumb-fuzz}

[ -x "$OUT/smlrc-host" ] || { echo "run.sh first: $OUT/smlrc-host is missing"; exit 1; }
[ -f "$OUT/qemusys.o" ] || { echo "run.sh first: $OUT/qemusys.o is missing"; exit 1; }
command -v qemu-arm >/dev/null 2>&1 || { echo "qemu-arm absent: nothing to compare"; exit 0; }
$HOST_CC -m32 -E - </dev/null >/dev/null 2>&1 || { echo "host cc lacks -m32: no oracle"; exit 0; }

mkdir -p "$W"
pass=0
bad=""

for s in $(seq 1 "$COUNT"); do
	python3 "$SRCDIR/gen_diff.py" "$s" > "$W/d.c"
	$HOST_CC -m32 -w -fwrapv -funsigned-char -o "$W/ref" "$W/d.c"
	"$W/ref" > "$W/ref.out" 2>&1

	$CROSS-gcc -E -P -nostdinc -I"$TOPSRC/include" -U__GNUC__ \
	    -D__SMALLER_C__=0x0100 -D__SMALLER_C_32__ -D__SMALLER_C_UCHAR__ \
	    -o "$W/d.i" "$W/d.c"
	"$OUT/smlrc-host" "$W/d.i" "$W/d.s" > "$W/log" 2>&1 ||
	    { bad="$bad $s(smlrc)"; continue; }
	$CROSS-as -mcpu=$CPU -o "$W/d.o" "$W/d.s" > "$W/log" 2>&1 ||
	    { bad="$bad $s(as)"; continue; }
	$CROSS-gcc -mcpu=$CPU -mthumb -mfloat-abi=soft -nostartfiles -nostdlib \
	    -Wl,--no-warn-rwx-segments -o "$W/d.elf" "$W/d.o" "$OUT/qemusys.o" \
	    -L"$TOPSRC/lib" -lc -lgcc > "$W/log" 2>&1 ||
	    { bad="$bad $s(ld)"; continue; }

	qemu-arm "$W/d.elf" > "$W/got.out" 2>&1 || true
	if cmp -s "$W/ref.out" "$W/got.out"; then
		pass=$((pass + 1))
	else
		bad="$bad $s(differs)"
		cp "$W/d.c" "$W/fail_$s.c"
	fi
done

echo "differential: $pass/$COUNT seeds match the host oracle"
if [ -n "$bad" ]; then
	echo "failing seeds:$bad"
	echo "reproducers left in $W"
	exit 1
fi
exit 0
