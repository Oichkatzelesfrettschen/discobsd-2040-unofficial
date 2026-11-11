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
PYTHON=${PYTHON:?set PYTHON to the host interpreter}
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
	if ! "$PYTHON" "$here/thumb-aoutdiff.py" "$work/$name.s" \
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

# The project suffix rule must override a host make's traditional CPP rule.
# Compile the production bare-cell member: its symbol names require ##.
cpp_work=$(mktemp -d)
trap 'rm -r "$cpp_work"' EXIT HUP INT TERM
ln -s "$TOPSRC/.git" "$cpp_work/.git"
printf 'TOPSRC=%s\nCOMPILE.S = ${CC} ${AFLAGS} ${CPPFLAGS} -c -traditional-cpp\ninclude ${TOPSRC}/share/mk/sys.mk\nAFLAGS = -Wa,-x -B${TOOLBINDIR}/\nall: cpp-contract.o\n' \
    "$TOPSRC" > "$cpp_work/Makefile"
printf '#include "%s/lib/libc/arm/gen/rom_float_clz.S"\n' \
    "$TOPSRC" > "$cpp_work/cpp-contract.S"
"${MAKE:-bmake}" -s -C "$cpp_work" MACHINE="${MACHINE:?set MACHINE}" \
    TOOLBINDIR="${TOOLBINDIR:?set TOOLBINDIR}" BUILD=1 cpp-contract.o
$CC -E "$TOPSRC/lib/libc/arm/gen/rom_float_clz.S" -o "$cpp_work/modern.s"
$GNUAS -mcpu="$CPU" -mthumb -o "$cpp_work/modern.gnu.o" "$cpp_work/modern.s"
"$PYTHON" "$here/thumb-aoutdiff.py" "$cpp_work/modern.s" \
    "$cpp_work/cpp-contract.o" "$cpp_work/modern.gnu.o"
$CC -E -traditional-cpp "$TOPSRC/lib/libc/arm/gen/rom_float_clz.S" \
    -o "$cpp_work/traditional.s"
if ! grep -F '##' "$cpp_work/traditional.s" >/dev/null; then
    echo "compile: traditional CPP control did not retain token pasting" >&2
    exit 1
fi
if $AS -o "$cpp_work/traditional.o" "$cpp_work/traditional.s" \
    > "$cpp_work/control.log" 2>&1; then
    echo "compile: native assembler accepted unexpanded token pasting" >&2
    exit 1
fi
echo "compile: project assembly CPP rule passes; traditional CPP control rejected"
