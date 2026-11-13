#!/bin/sh
# Calibration for tools/select-host-gates.sh: each case feeds changed paths
# on standard input and compares the printed skip list. The fail-closed
# cases (shared paths, an unreadable range) must print an empty list, so
# check-host runs every gate.
set -eu

here=$(dirname "$0")
select="$here/select-host-gates.sh"
all="check-fourfall-host check-puzzlebox-host check-ridge-host check-calc-host check-libcalc check-dcbc"
failures=0

expect() {
	name=$1
	want=$2
	got=$(printf '%s' "$3" | sh "$select" --paths)
	if [ "$got" = "$want" ]; then
		echo "PASS $name"
	else
		echo "FAIL $name: want [$want] got [$got]"
		failures=$((failures + 1))
	fi
}

expect "init change skips every heavy suite" "$all" "sbin/init/init.c
tests/init_contracts/Makefile"
expect "no change skips every heavy suite" "$all" ""
expect "fourfall source runs fourfall only" \
	"check-puzzlebox-host check-ridge-host check-calc-host check-libcalc check-dcbc" \
	"games/fourfall/fourfall.c"
expect "shared game tty header runs fourfall and puzzlebox" \
	"check-ridge-host check-calc-host check-libcalc check-dcbc" "games/gametty.h"
expect "a puzzle directory runs puzzlebox" \
	"check-fourfall-host check-ridge-host check-calc-host check-libcalc check-dcbc" \
	"games/samegame/samegame.c"
expect "libcalc runs calc and libcalc" \
	"check-fourfall-host check-puzzlebox-host check-ridge-host check-dcbc" \
	"lib/libcalc/calc.c"
expect "libbn runs dcbc" \
	"check-fourfall-host check-puzzlebox-host check-ridge-host check-calc-host check-libcalc" \
	"lib/libbn/bn.c"
expect "the mutant pool runs its three users" \
	"check-ridge-host check-libcalc check-dcbc" "tools/mutant_pool.py"
expect "ridge shim runs ridge" \
	"check-fourfall-host check-puzzlebox-host check-calc-host check-libcalc check-dcbc" \
	"tests/picocalc/shim/term.c"
expect "root Makefile runs everything" "" "sbin/init/init.c
Makefile"
expect "build rules run everything" "" "share/mk/sys.mk"
expect "libc runs everything" "" "lib/libc/stdlib/qsort.c"
expect "headers run everything" "" "include/stdio.h"
expect "kernel runs everything" "" "sys/kern/tty.c"
expect "workflow runs everything" "" ".github/workflows/firmware.yml"
expect "shared mutant library runs everything" "" "games/mutlib.py"
expect "selector itself runs everything" "" "tools/select-host-gates.sh"
expect "a game Makefile is not the root Makefile" \
	"check-puzzlebox-host check-ridge-host check-calc-host check-libcalc check-dcbc" \
	"games/fourfall/Makefile"
expect "a sibling name is not a directory prefix" "$all" "games/fourfallx/a.c
lib/libcalcx/b.c"

# A range git cannot read prints nothing: every gate runs.
got=$(sh "$select" 0000000000000000000000000000000000000000 HEAD)
if [ -z "$got" ]; then
	echo "PASS unreadable range runs everything"
else
	echo "FAIL unreadable range runs everything: got [$got]"
	failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
	echo "host gate selection: $failures failures"
	exit 1
fi
echo "host gate selection: all cases passed"
