#!/bin/sh
# Verify that the RP2040 userland links without common symbols.
#
# Builds the C libraries and every program that once depended on -fcommon
# with the tree's default flags, then fails when any object still carries a
# COMMON (nm type C) symbol or when a program does not link. A COMMON symbol
# means a Makefile reintroduced -fcommon or a header regained a tentative
# definition; a link failure names the multiply defined symbol.
#
# usage: tools/verify_userland_no_common.sh [MACHINE]
set -eu

MACHINE=${1:-rp2040}
TOPSRC=$(cd "$(dirname "$0")/.." && pwd)
NM=${NM:-arm-none-eabi-nm}
MAKE=${MAKE:-bmake}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

LIBS="startup-arm libc libm libcurses libtermlib libutil libvmf"
PROGRAMS="games/atc usr.bin/awk games/backgammon usr.bin/basic
games/battlestar games/caesar games/canfield games/cribbage usr.bin/diff
usr.bin/forth sbin/fsck games/hangman usr.bin/med games/mille games/pom
games/primes usr.bin/re games/robots games/sail usr.bin/sed bin/sh
usr.bin/sl games/snake usr.bin/tail usr.bin/tip games/trek games/worm"

status=0

build() {
	dir=$1
	$MAKE MACHINE="$MACHINE" -C "$TOPSRC/$dir" clean >/dev/null 2>&1 || true
	if ! $MAKE MACHINE="$MACHINE" -C "$TOPSRC/$dir" >"$LOG" 2>&1; then
		echo "FAIL $dir: build"
		grep -E "multiple definition|error:" "$LOG" | sed "s#$TOPSRC/##g" | sort -u | head -5
		status=1
		return
	fi
	commons=$(find "$TOPSRC/$dir" -maxdepth 2 -name '*.o' -exec $NM {} + 2>/dev/null | awk '$2 == "C"' | wc -l)
	if [ "$commons" -ne 0 ]; then
		echo "FAIL $dir: $commons COMMON symbols"
		find "$TOPSRC/$dir" -maxdepth 2 -name '*.o' -exec $NM -A {} + | awk '$2 == "C"' | head -5
		status=1
		return
	fi
	echo "ok   $dir"
}

# lib/Makefile builds its subdirectories through a FRC rule whose result
# depends on directory timestamps, so each library is built by name.
rm -f "$TOPSRC"/lib/*.a
for l in $LIBS; do
	build "lib/$l"
done
for p in $PROGRAMS; do
	build "$p"
done
exit $status
