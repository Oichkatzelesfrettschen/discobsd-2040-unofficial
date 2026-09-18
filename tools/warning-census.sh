#!/bin/sh
# Count the compiler warnings the tree would raise under a wider warning set,
# and say which file and which category each one belongs to.
#
# The census exists because the obvious ways of measuring this both lie.
# Building with -j interleaves the output of parallel compiles, so a warning
# cannot be attributed to a source file by reading back to the nearest
# compile line: the nearest line belongs to whichever job printed last. And
# raising the warning set without demoting errors stops every directory that
# already carries -Werror, which ends the build after a few dozen files and
# reports a count far below the truth.
#
# So the census compiles serially and appends -Wno-error, which lands after
# any -Werror a directory sets and leaves the build running to the end. A
# warning's own file:line, which the compiler prints, is what the census
# reports; it never infers a location from position in the log.
#
# Usage: sh tools/warning-census.sh [-w "WARNING FLAGS"] [directory ...]
# Default flags are -Wall -Wextra and the default directories are the
# userland tree. The kernel is a separate target, because its Makefile
# carries its own CWARNFLAGS:
#   bmake MACHINE=rp2040 kernel CWARNFLAGS='-Wall -Wextra -Wno-error'

set -eu

TOPSRC=$(cd "$(dirname "$0")/.." && pwd)
WARNFLAGS='-Wall -Wextra'
MACHINE=${MACHINE:-rp2040}

while [ $# -gt 0 ]; do
	case $1 in
	-w) WARNFLAGS=$2; shift 2 ;;
	-*) echo "usage: warning-census.sh [-w FLAGS] [directory ...]" >&2; exit 2 ;;
	*) break ;;
	esac
done

[ $# -gt 0 ] || set -- bin sbin usr.bin usr.sbin games lib libexec

command -v bmake >/dev/null 2>&1 || {
	echo "warning-census: bmake absent; not run" >&2
	exit 1
}

# A userland census compiles against the tree's own headers and links
# against its libc, so it wants a tree that `bmake MACHINE=<m> build` has
# already populated. Naming the missing piece here beats a screen of
# "machine/machparam.h: No such file" from every directory in turn.
[ -e "$TOPSRC/include/machine" ] || {
	echo "warning-census: include/machine absent; run bmake MACHINE=$MACHINE symlinks; not run" >&2
	exit 1
}
[ -f "$TOPSRC/lib/crt0.o" ] || {
	echo "warning-census: lib/crt0.o absent; run bmake MACHINE=$MACHINE build; not run" >&2
	exit 1
}

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT INT TERM

cd "$TOPSRC"
for d in "$@"; do
	[ -d "$d" ] || { echo "warning-census: no directory $d; not run" >&2; exit 1; }
done

# Serial on purpose: the attribution below reads the compile line that
# precedes a warning, which only holds when one compile runs at a time.
for d in "$@"; do
	bmake MACHINE="$MACHINE" -C "$d" clean >/dev/null 2>&1 || true
done
for d in "$@"; do
	bmake MACHINE="$MACHINE" -C "$d" \
		COPTS="-Os -fno-common $WARNFLAGS -Wno-error" 2>&1 || true
done > "$LOG"

total=$(grep -c 'warning:' "$LOG" || true)
compiles=$(grep -cE '(gcc|^cc) .* -c ' "$LOG" || true)
errors=$(grep -cE '^\*\*\* |Error code' "$LOG" || true)

printf 'warning census: %s\n' "$WARNFLAGS"
printf '  compiles %s   warnings %s   build errors %s\n\n' \
	"$compiles" "$total" "$errors"

if [ "$errors" -gt 0 ]; then
	printf '  %s build errors remain: the census is a floor, not a total.\n' \
		"$errors"
	printf '  The directories that stopped contribute no warnings below.\n\n'
fi

printf 'by category\n'
grep -o '\[-W[a-z0-9=+-]*\]' "$LOG" | sort | uniq -c | sort -rn |
	awk '{printf "  %6d  %s\n", $1, $2}'

printf '\nby file, from each warning own location\n'
grep 'warning:' "$LOG" |
	sed -n 's|^\([^ ][^:]*\):[0-9][0-9]*:[0-9]*:[[:space:]]*warning:.*|\1|p' |
	sed "s|^$TOPSRC/||" | sort | uniq -c | sort -rn | head -25 |
	awk '{printf "  %6d  %s\n", $1, $2}'

printf '\nwarnings raised inside a macro, by macro\n'
grep -o "in expansion of macro '[^']*'" "$LOG" | sort | uniq -c | sort -rn |
	head -10 | awk '{printf "  %6d  %s\n", $1, $6}'
