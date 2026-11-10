#!/bin/sh
# Calibration of tools/verify_rp2040_core1_image.py on linked fixtures: the
# clean fixture has to pass, and each fixture built with one defect has to
# fail with exit 1 and name that defect. PYTHON names the interpreter and
# GCCPREFIX the cross toolchain prefix.
set -eu

: "${PYTHON:?set PYTHON to the intended interpreter}"
here=$(cd "$(dirname "$0")" && pwd)
verifier=$here/../../../tools/verify_rp2040_core1_image.py
cc=${GCCPREFIX:-arm-none-eabi}-gcc
work=$(mktemp -d "${TMPDIR:-/tmp}/core1image.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$PYTHON" -c 'import capstone, elftools'

# link NAME [DEFINE]: build the fixture, with DEFINE when given.
link()
{
	"$cc" -std=gnu17 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	    -ffreestanding -nostdlib -nostartfiles -Wall -Wextra -Werror \
	    -Wl,--fatal-warnings -Wl,-z,noexecstack -Wl,--build-id=none \
	    -Wl,--entry=entry -Wl,--section-start=.text=0x10000100 \
	    -Wl,--section-start=.ramfunc=0x20030100 \
	    -Wl,--section-start=.bss=0x20031000 \
	    ${2:+-D"$2"} -o "$work/$1.elf" "$here/fixture.c"
}

link clean
"$PYTHON" "$verifier" --require-image "$work/clean.elf" >"$work/clean.out"
grep -q '^PASS core1-image .*: 4 functions' "$work/clean.out"

# reject NAME DEFINE PATTERN: the defect must be found and named.
reject()
{
	link "$1" "$2"
	_rc=0
	"$PYTHON" "$verifier" "$work/$1.elf" >"$work/$1.out" 2>&1 || _rc=$?
	if [ "$_rc" -ne 1 ]; then
		echo "core1-image: fixture $1 gave exit $_rc, expected 1" >&2
		cat "$work/$1.out" >&2
		exit 1
	fi
	if ! grep -q "$3" "$work/$1.out"; then
		echo "core1-image: fixture $1 failed for another reason" >&2
		cat "$work/$1.out" >&2
		exit 1
	fi
}

reject flash-call BAD_CALL 'flash_function'
reject flash-literal BAD_LITERAL 'is the flash address'
reject indirect-call BAD_INDIRECT 'core1_main: indirect'
reject trap BAD_TRAP 'core1_fault: svc'

# An image with none of the entry symbols is reported, and refused on request.
"$cc" -std=gnu17 -Os -mcpu=cortex-m0plus -mthumb -ffreestanding -nostdlib \
    -nostartfiles -Wl,--build-id=none -Wl,--entry=flash_function \
    -Wl,--section-start=.text=0x10000100 -Dcore1_main=m -Dcore1_fault=f \
    -Dcore1_op=o -o "$work/none.elf" "$here/fixture.c"
"$PYTHON" "$verifier" "$work/none.elf" | grep -q 'no core 1 image'
_rc=0
"$PYTHON" "$verifier" --require-image "$work/none.elf" >/dev/null || _rc=$?
[ "$_rc" -eq 1 ]

# Core 0 takes the probe interrupt as an NMI whose vector is in flash, so
# every source line that takes execute-in-place down has to lie between a
# watchdog_site() declaration, which withdraws the NMI route, and the call
# that clears it.
top=$here/../../..
declared()
{
	awk '
	FNR == 1 { armed = 0 }
	/watchdog_site\(WD_SITE_FLASH_(ERASE|PROGRAM)/ { armed = 1 }
	/watchdog_site\(WD_SITE_NONE/ { armed = 0 }
	/exit_xip\(\);/ {
		calls++
		if (!armed) {
			print FILENAME ":" FNR ": execute-in-place goes down outside a declared section"
			bad = 1
		}
	}
	END { if (calls == 0) { print "no execute-in-place call found"; bad = 1 }
	      exit bad }' "$@"
}
declared "$top"/sys/arch/rp2040/dev/*.c "$top"/sys/arch/rp2040/rp2040/*.c
sed '/watchdog_site(WD_SITE_FLASH_ERASE/d' "$top/sys/arch/rp2040/dev/flash.c" \
    >"$work/undeclared.c"
if declared "$work/undeclared.c" >/dev/null; then
	echo "core1-image: an undeclared execute-in-place window passed" >&2
	exit 1
fi

echo "core1-image: clean fixture passed, 4 defects and 1 absent image reported"
