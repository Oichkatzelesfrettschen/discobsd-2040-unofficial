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

# Reproduce the inherited bmake rule with the production ROM trampoline.
# The comparison also checks that the project rule used the a.out assembler.
cpp_work=$(mktemp -d "${TMPDIR:-/tmp}/as-cpp.XXXXXX")
trap 'rm -r "$cpp_work"' EXIT
trap 'exit 1' HUP INT TERM
for file in rom_float_clz.S rom_float.inc; do
	cp "$TOPSRC/lib/libc/arm/gen/$file" "$cpp_work/$file"
done
cat > "$cpp_work/Makefile" <<'MAKEFILE'
COMPILE.S = ${CC} ${AFLAGS} ${CPPFLAGS} -c -traditional-cpp
include ${TOPSRC}/share/mk/sys.mk
AFLAGS = -Wa,-x -B${TOOLBINDIR}/
MAKEFILE
(
	unset MAKEFLAGS MFLAGS
	GIT_DIR=$(git -C "$TOPSRC" rev-parse --absolute-git-dir)
	export GIT_DIR
	"${MAKE:-bmake}" -s -C "$cpp_work" "TOPSRC=$TOPSRC" \
	    "MACHINE=${MACHINE:?set MACHINE}" "TOOLBINDIR=${TOOLBINDIR:?set TOOLBINDIR}" \
	    rom_float_clz.o
)
$CC -E "$cpp_work/rom_float_clz.S" -o "$cpp_work/modern.s"
$GNUAS -mcpu="$CPU" -mthumb -o "$cpp_work/modern.o" "$cpp_work/modern.s"
"$PYTHON" "$here/thumb-aoutdiff.py" "$cpp_work/modern.s" \
    "$cpp_work/rom_float_clz.o" "$cpp_work/modern.o"
$CC -E -traditional-cpp "$cpp_work/rom_float_clz.S" -o "$cpp_work/traditional.s"
grep -F '##' "$cpp_work/traditional.s" >/dev/null
if $AS -o "$cpp_work/traditional.o" "$cpp_work/traditional.s" \
    >"$cpp_work/control.log" 2>&1; then
	echo "compile: native assembler accepted unexpanded token pasting" >&2
	exit 1
fi
grep -F 'bad instruction arguments' "$cpp_work/control.log" >/dev/null
echo "compile: ISO assembly CPP output matches GNU as; traditional CPP rejected"
