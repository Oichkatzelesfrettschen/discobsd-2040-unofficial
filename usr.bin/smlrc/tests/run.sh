#!/bin/sh
#
# Build smlrc for the build host with the Thumb-1 back end, compile each
# test with it, and check the result two ways.
#
# The device tier links exactly as share/mk/sys.mk links a user program --
# the tree's crt0.o, libc.a and elf32-arm.ld, then elf2aout -- and proves
# the generated code resolves every symbol and converts to an a.out the
# kernel can load. That a.out is what runs on the board.
#
# The execution tier links the same object against a Linux EABI syscall
# layer so qemu-arm can run it. DiscoBSD reaches the kernel through an SVC
# immediate and returns errors in the carry flag, which a Linux user
# emulator does not implement, so the DiscoBSD a.out cannot run under qemu
# and the syscall layer is the only part replaced. Everything above it,
# including printf and malloc, is the tree's own libc.

set -eu

TOPSRC=${TOPSRC:?TOPSRC must be set}
MACHINE=${MACHINE:-rp2040}
HOST_CC=${HOST_CC:-cc}
CROSS=${CROSS:-arm-none-eabi}
PYTHON=${PYTHON:-python3}
CPU=${CPU:-cortex-m0plus}
SRCDIR=$(cd "$(dirname "$0")" && pwd)
OUT=$SRCDIR/out

CPP="$CROSS-gcc -E -P -nostdinc -I$TOPSRC/include -U__GNUC__ -U__PCC__ \
    -D__SMALLER_C__=0x0100 -D__SMALLER_C_32__ -D__SMALLER_C_UCHAR__ \
    -D__arm__ -D__unix__ -D__BSD__ -D__DISCOBSD__"

AS="$CROSS-as"
GCC="$CROSS-gcc"
NM="$CROSS-nm"
ELF2AOUT=$TOPSRC/tools/bin/elf2aout
LDSCRIPT=$TOPSRC/lib/elf32-arm.ld
CRT0=$TOPSRC/lib/crt0.o
LIBC=$TOPSRC/lib/libc.a

rm -rf "$OUT"
mkdir -p "$OUT"

pass=0
fail=0
skiprun=0
failed=""

note()
{
	echo "$@"
}

fail_test()
{
	fail=$((fail + 1))
	failed="$failed $1"
	note "FAIL $1: $2"
}

# The host build of the compiler. -DTHUMB selects cgthumb.c; the __SMALLER_C__
# family is omitted because it describes a self-hosted build rather than this
# one. NO_ANNOTATIONS is required, not merely tidy: the commentary the back end
# would otherwise print runs every token through GetTokenName, which rejects
# the four tokens GenPrep synthesizes for the back end's own use. STATIC is
# defined because the Makefile defines it, and it decides whether each back
# end entry point is file-local, which is what catches a definition whose
# linkage disagrees with the declaration in smlrc.c.
#
# Sources reach smlrc already preprocessed, which is the arrangement on the
# device, where usr.bin/cc runs usr.bin/cpp first. smlrc's own preprocessor
# handles no function-like macro, and <stdio.h> defines getc and putc as
# function-like macros, so a program including it has to be preprocessed by
# something else. The header set branches on __GNUC__, so that macro is
# undefined again and the Smaller C identity put in its place, which makes
# the headers expand to the same declarations the device build sees.
note "building smlrc for the host with -DTHUMB"
$HOST_CC -O1 -Wall -Wextra -DTHUMB -DNO_ANNOTATIONS -DNO_PPACK -DSTATIC \
    -DNO_EXTRA_WARNS -DSYNTAX_STACK_MAX=3200 \
    -o "$OUT/smlrc-host" "$SRCDIR/../smlrc.c" 2> "$OUT/smlrc-host.log" ||
    { cat "$OUT/smlrc-host.log"; exit 1; }
# Warnings are errors on the new back end only; smlrc.c itself is upstream.
if grep -q 'cgthumb\.c' "$OUT/smlrc-host.log"; then
	note "warnings in cgthumb.c:"
	grep -A2 'cgthumb\.c' "$OUT/smlrc-host.log"
	exit 1
fi

# The Linux EABI syscall layer used only by the execution tier.
$GCC -std=gnu17 -mcpu=$CPU -mthumb -mfloat-abi=soft -Os -Wall \
    -nostdinc -I"$TOPSRC/include" \
    -c -o "$OUT/qemusys.o" "$SRCDIR/qemusys.c"

# The device tier links against build products of lib, which a fresh tree
# does not have; say so rather than failing inside the linker.
for f in "$CRT0" "$LIBC" "$LDSCRIPT" "$ELF2AOUT" ; do
	[ -e "$f" ] || {
		echo "missing $f"
		echo "build the target's libraries and tools first:"
		echo "  bmake MACHINE=$MACHINE tools"
		echo "  bmake MACHINE=$MACHINE DESTDIR=\$TOPSRC/distrib/obj/destdir.$MACHINE -C include includes"
		echo "  bmake MACHINE=$MACHINE DESTDIR=\$TOPSRC/distrib/obj/destdir.$MACHINE -C lib"
		exit 1
	}
done

# lib/libc.a is one file shared by every ARM machine, and the linker does not
# reject an object built for a wider architecture. A libc left over from an
# stm32 build is Cortex-M4, whose Thumb-2 encodings fault on a Cortex-M0+ but
# run happily under a full-ARM emulator, so the mismatch would pass the tests
# and fail on the board. The build attribute is the thing that distinguishes
# them.
_arch=$($CROSS-readelf -A "$LIBC" 2>/dev/null |
    sed -n 's/.*Tag_CPU_arch: *//p' | head -1)
case "$_arch" in
v6*)	;;
"")	note "warning: cannot read the CPU architecture of $LIBC" ;;
*)	echo "$LIBC is built for $_arch, not the Cortex-M0+'s v6-M"
	echo "rebuild it for this machine:"
	echo "  bmake MACHINE=rp2040 DESTDIR=\$TOPSRC/distrib/obj/destdir.rp2040 -C lib"
	exit 1 ;;
esac

if command -v qemu-arm >/dev/null 2>&1; then
	QEMU=qemu-arm
else
	QEMU=""
	note "qemu-arm not found: linking only, a.outs left in $OUT for the device"
fi

run_one()
{
	name=$1
	src=$2
	base=$OUT/$name

	if ! $CPP -o "$base.i" "$src" > "$base.cpp.log" 2>&1; then
		fail_test "$name" "preprocessing failed, see $base.cpp.log"
		return
	fi

	# Compile with the freshly built Thumb-1 back end, from inside $OUT:
	# smlrc keeps file names under 96 bytes, which a deep checkout exceeds.
	if ! (cd "$OUT" && ./smlrc-host "$name.i" "$name.s") > "$base.smlrc.log" 2>&1; then
		fail_test "$name" "smlrc failed, see $base.smlrc.log"
		return
	fi

	# Assemble as a strict ARMv6-M target: any 32-bit Thumb-2 encoding or
	# out-of-range branch the generator emitted is rejected here.
	if ! $AS -mcpu=$CPU -o "$base.o" "$base.s" > "$base.as.log" 2>&1; then
		fail_test "$name" "assembly failed, see $base.as.log"
		return
	fi

	# A companion compiled by the cross compiler, for a test that needs
	# something smlrc cannot express. t17_align's alignment probes are
	# naked functions, and smlrc reads no inline assembly.
	extra=""
	if [ -f "$SRCDIR/$name.gnu.c" ]; then
		if ! $GCC -std=gnu17 -mcpu=$CPU -mthumb -mfloat-abi=soft -Os \
		    -Wall -Wextra -nostdinc -I"$TOPSRC/include" \
		    -c -o "$base.gnu.o" "$SRCDIR/$name.gnu.c" \
		    > "$base.gnu.log" 2>&1; then
			fail_test "$name" "companion failed, see $base.gnu.log"
			return
		fi
		extra="$base.gnu.o"
	fi

	# Device tier: the tree's own link line.
	if ! $GCC -mcpu=$CPU -mthumb -mfloat-abi=soft \
	    -N -nostartfiles -fno-dwarf2-cfi-asm -Wl,--no-warn-rwx-segments \
	    -T"$LDSCRIPT" "$CRT0" -L"$TOPSRC/lib" \
	    -o "$base.elf" "$base.o" $extra -lc > "$base.ld.log" 2>&1; then
		fail_test "$name" "device link failed, see $base.ld.log"
		return
	fi
	if $NM -u "$base.elf" 2>/dev/null | grep -q .; then
		fail_test "$name" "undefined symbols in the device link"
		return
	fi
	if ! $ELF2AOUT "$base.elf" "$base.aout" >> "$base.ld.log" 2>&1; then
		fail_test "$name" "elf2aout failed, see $base.ld.log"
		return
	fi

	if [ -z "$QEMU" ]; then
		skiprun=$((skiprun + 1))
		note "LINK $name (a.out at $base.aout; expected output in $SRCDIR/$name.expected)"
		return
	fi

	# Execution tier.
	if ! $GCC -mcpu=$CPU -mthumb -mfloat-abi=soft \
	    -nostartfiles -nostdlib -Wl,--no-warn-rwx-segments \
	    -o "$base.qemu.elf" "$base.o" $extra "$OUT/qemusys.o" \
	    -L"$TOPSRC/lib" -lc -lgcc > "$base.qld.log" 2>&1; then
		fail_test "$name" "qemu link failed, see $base.qld.log"
		return
	fi

	set +e
	if [ -f "$SRCDIR/$name.stdin" ]; then
		$QEMU "$base.qemu.elf" $(cat "$SRCDIR/$name.args" 2>/dev/null) \
		    < "$SRCDIR/$name.stdin" > "$base.out" 2>&1
	else
		$QEMU "$base.qemu.elf" $(cat "$SRCDIR/$name.args" 2>/dev/null) \
		    < /dev/null > "$base.out" 2>&1
	fi
	rc=$?
	set -e
	if [ $rc -ne 0 ]; then
		fail_test "$name" "exited $rc under qemu"
		return
	fi

	if ! cmp -s "$base.out" "$SRCDIR/$name.expected"; then
		fail_test "$name" "output differs"
		diff -u "$SRCDIR/$name.expected" "$base.out" | head -20 || true
		return
	fi

	pass=$((pass + 1))
	note "PASS $name"
}

for src in "$SRCDIR"/t[0-9][0-9]_*.c ; do
	[ -e "$src" ] || continue
	name=$(basename "$src" .c)
	# A .gnu.c is a companion the cross compiler builds for the test of
	# the same name, not a test of its own.
	case "$name" in
	*.gnu)	continue ;;
	esac
	run_one "$name" "$src"
done

# Every call the back end wrote, in every test, reaches its callee with SP
# 8-byte aligned. The checker models the generator's own SP-moving forms and
# errors on anything it does not recognize, so a new emitter cannot pass by
# being skipped.
if ! $PYTHON "$SRCDIR/spalign.py" "$OUT"/*.s ; then
	fail=$((fail + 1))
	failed="$failed spalign"
fi

note ""
note "passed $pass, failed $fail, link-only $skiprun"
if [ $fail -ne 0 ]; then
	note "failing:$failed"
	exit 1
fi
exit 0
