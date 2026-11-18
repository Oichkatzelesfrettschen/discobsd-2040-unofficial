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

# The ROM cell's token-pasted names exercise the host suffix rule.
cpp_work=$(mktemp -d "$work/thumb-cpp.XXXXXX")
cleanup_cpp()
{
	rm -f "$cpp_work/Makefile" "$cpp_work/probe.S" "$cpp_work/probe.o" \
	    "$cpp_work/probe.o.sec" "$cpp_work/iso.s" "$cpp_work/iso.o" \
	    "$cpp_work/traditional.s" "$cpp_work/traditional.o" \
	    "$cpp_work/traditional.o.sec" "$cpp_work/traditional.log"
	rmdir "$cpp_work"
}
trap cleanup_cpp EXIT
trap 'exit 130' HUP INT TERM
cat > "$cpp_work/Makefile" <<'EOF'
COMPILE.S= ${CC} ${AFLAGS} ${CPPFLAGS} -c -traditional-cpp
include ${TOPSRC}/share/mk/sys.mk
AFLAGS= -Wa,-x -B${TOOLBINDIR:Q}/ -I${TOPSRC:Q}/lib/libc/arm/gen
EOF
printf '#include "rom_float_clz.S"\n' > "$cpp_work/probe.S"
"${MAKE:?set MAKE}" -s -C "$cpp_work" MACHINE="${MACHINE:?set MACHINE}" \
    TOPSRC="$TOPSRC" TOOLBINDIR="${TOOLBINDIR:?set TOOLBINDIR}" \
    BUILD=1 probe.o
cell="$TOPSRC/lib/libc/arm/gen/rom_float_clz.S"
$CC -E "$cell" -o "$cpp_work/iso.s"
$GNUAS -mcpu="$CPU" -mthumb -o "$cpp_work/iso.o" "$cpp_work/iso.s"
"$PYTHON" "$here/thumb-aoutdiff.py" "$cpp_work/iso.s" \
    "$cpp_work/probe.o" "$cpp_work/iso.o"
$CC -E -traditional-cpp "$cell" -o "$cpp_work/traditional.s"
if ! grep -F '##' "$cpp_work/traditional.s" >/dev/null; then
	echo "compile: traditional CPP control lost its token-pasting witness" >&2
	exit 1
fi
if $AS -o "$cpp_work/traditional.o" "$cpp_work/traditional.s" \
    > "$cpp_work/traditional.log" 2>&1; then
	echo "compile: traditional CPP control assembled" >&2
	exit 1
fi
echo "compile: ISO-preprocessed ROM cell agrees with GNU; traditional CPP rejects"
