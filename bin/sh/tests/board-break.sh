#!/bin/sh
#
# bin/sh against the RP2040 kernel's break.
#
# The shell's allocator (blok.c, stak.c) was written for a machine on which
# a store past the break faults and fault.c grows the break in answer. On
# the board the MPU grants the whole process window, so the store
# succeeds, and brk(2) zeroes the range it grants at the next growth
# (sys/kern/kern_mman.c). board_break.c models that break; this script
# builds the shell against it, as 32-bit host code like posix-sh.sh, and
# checks that no block header, stack item or break bookkeeping is lost:
#
#   heap      the line from issue 544 after 1 to 150 leading assignments,
#             which moves the heap top across several growth steps
#   longword  a 9000-byte word built by expansion
#   exhaust   an interactive shell that runs out of arena reports
#             "no space" and keeps a usable stack for the next command
#
# Three mutants calibrate the cases: one removes addblok()'s growth, one
# makes stakroom() a no-op, and one lets setbrk() record a refused break.
# Each must fail its own case before the verdict on the shell counts; the
# shell from before brkroom() fails all three cases.
#
# Run: sh bin/sh/tests/board-break.sh
# Environment: HOST_CC (default cc).

set -eu

SRCDIR=$(cd "$(dirname "$0")" && pwd)
SHSRC=$(cd "$SRCDIR/.." && pwd)
TOPSRC=$(cd "$SHSRC/../.." && pwd)
HOST_CC=${HOST_CC:-cc}
OUT=$SRCDIR/out-board
WORK=$OUT/work

UNITS="setbrk blok stak cmd fault main word string name args xec service
	error io print macro ctype msg test defs echo hash hashserv pwd func
	pwname"

CFLAGS="-m32 -std=gnu89 -O1 -w -fcommon -include $SRCDIR/hostshim.h
	-D_FILE_OFFSET_BITS=64 -Dalloc=sh_alloc -Dfree=sh_free
	-Dsbrk=sh_board_sbrk"

cleanup()
{
	rm -rf "$WORK"
}
trap cleanup EXIT

# build_shell NAME SOURCE-DIRECTORY
build_shell()
{
	objects=$OUT/$1
	mkdir -p "$objects"
	# shellcheck disable=SC2086
	$HOST_CC $CFLAGS -DSH_HOST_RAWDIR -c -o "$objects/expand.o" "$2/expand.c"
	# shellcheck disable=SC2086
	$HOST_CC $CFLAGS -DHOSTBUILD -c -o "$objects/edit.o" "$2/edit.c"
	for unit in $UNITS; do
		# shellcheck disable=SC2086
		$HOST_CC $CFLAGS -c -o "$objects/$unit.o" "$2/$unit.c"
	done
	$HOST_CC -m32 -o "$OUT/$1.sh" "$objects"/*.o \
	    "$OUT/hostshim.o" "$OUT/board_break.o"
}

# mutate NAME FILE OLD NEW -- copies the sources and replaces OLD, which
# must occur exactly once in FILE.
mutate()
{
	tree=$OUT/$1-src
	mkdir -p "$tree"
	cp "$SHSRC"/*.c "$SHSRC"/*.h "$tree"/
	if ! "$PYTHON" - "$tree/$2" "$3" "$4" <<'EOF'
import sys
path, old, new = sys.argv[1:]
text = open(path, encoding="utf-8").read()
if text.count(old) != 1:
    sys.exit(f"{path}: mutation anchor occurs {text.count(old)} times")
open(path, "w", encoding="utf-8").write(text.replace(old, new))
EOF
	then
		echo "ERROR mutant $1: anchor missing in $2" >&2
		exit 3
	fi
	build_shell "$1" "$tree"
}

ISSUE_LINE='x=a/b/c.d.c; IFS=/; for p in $x; do b=$p; done; IFS='"' '"'; echo $b; echo done'
WORD=$(printf '%3000s' '' | tr ' ' x)

# case_heap SHELL
case_heap()
{
	prefix=""
	n=1
	while [ "$n" -le 150 ]; do
		prefix="${prefix}v$n=abcdefghijklmnopqrstuvwxyz$n; "
		if ! out=$(cd "$WORK" && env -i PATH=/usr/bin:/bin \
		    "$1" -c "$prefix$ISSUE_LINE" 2>&1 < /dev/null) ||
		    [ "$out" != "$(printf 'c.d.c\ndone')" ]; then
			return 1
		fi
		n=$((n + 1))
	done
}

case_longword()
{
	out=$(cd "$WORK" && env -i PATH=/usr/bin:/bin "$1" -c \
	    "w=$WORD; y=\$w\$w\$w; echo \$y | wc -c" 2>&1 < /dev/null) &&
	    [ "$(echo $out)" = 9001 ]
}

# An error discards the input already buffered, so the second command
# arrives after the first has failed. The shell reads the host's
# /etc/profile first, as it does for a "-i" first operand; the case reads
# only the lines the two commands produce.
case_exhaust()
{
	out=$(cd "$WORK" && {
		printf '%s\n' 'a=xxxxxxxxxxxxxxxx; while :; do a=$a$a; done'
		sleep 1
		printf '%s\n' 'echo after-exhaust'
	} | env -i PATH=/usr/bin:/bin "$1" -i 2>&1) || :
	case $out in
	*"no stack space"*) return 1 ;;
	*"no space"*"after-exhaust"*) return 0 ;;
	*) return 1 ;;
	esac
}

CASES="heap longword exhaust"

# verdicts SHELL -- prints the cases SHELL fails
verdicts()
{
	failed=""
	for c in $CASES; do
		if ! "case_$c" "$1"; then
			failed="$failed $c"
		fi
	done
	echo "$failed"
}

: "${PYTHON:?set PYTHON to the intended interpreter}"
rm -rf "$OUT"
mkdir -p "$OUT" "$WORK"

# shellcheck disable=SC2086
if [ "$(sh "$TOPSRC/tools/compiler-probe.sh" ilp32 $HOST_CC -m32)" != yes ]; then
	echo "ERROR build: $HOST_CC cannot compile and execute an ILP32 binary." >&2
	exit 2
fi
$HOST_CC -m32 -std=gnu17 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 \
    -c -o "$OUT/hostshim.o" "$SRCDIR/hostshim.c"
$HOST_CC -m32 -std=c17 -pedantic-errors -Wall -Wextra -Werror \
    -c -o "$OUT/board_break.o" "$SRCDIR/board_break.c"

build_shell shell "$SHSRC"
mutate addblok blok.c '	brkroom(stakend + reqd' '	(void)(stakend + reqd'
mutate stakroom stak.h \
    '#define stakroom(p)	((p) >= brkend ? brkroom((p) + 1) : (void)0)' \
    '#define stakroom(p)	((void)(p))'
mutate setbrk setbrk.c '	if (a != (char *)-1)
		brkend' '	brkend'

status=0
for pair in addblok:heap stakroom:longword setbrk:exhaust; do
	mutant=${pair%%:*}
	want=${pair#*:}
	got=$(verdicts "$OUT/$mutant.sh")
	case " $got " in
	*" $want "*)
		echo "PASS mutant $mutant fails:$got"
		;;
	*)
		echo "FAIL mutant $mutant passes $want (fails:${got:- none})"
		status=1
		;;
	esac
done

got=$(verdicts "$OUT/shell.sh")
if [ -n "$got" ]; then
	echo "FAIL shell:$got"
	status=1
else
	echo "PASS shell: $CASES"
fi

if [ "$status" -ne 0 ]; then
	echo "BOARD BREAK FAILED"
	exit 1
fi
echo "BOARD BREAK OK"
