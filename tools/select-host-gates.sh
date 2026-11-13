#!/bin/sh
# Prints the host gates a pull request may skip, one line, space separated,
# for "bmake check-host HOST_GATE_SKIP=...".
#
#   sh tools/select-host-gates.sh BASE HEAD   paths from git diff BASE HEAD
#   sh tools/select-host-gates.sh --paths     paths from standard input
#
# Each heavy suite below reads only the listed paths (its Makefile and
# tests/*.py name every input outside its directory). A suite runs when a
# changed path starts with one of its prefixes, and every suite runs when a
# path starts with a SHARED prefix: the build rules, the C library and
# headers every host build includes, the kernel, CI and this script. A git
# failure prints nothing, so check-host runs every gate. The nightly and
# main-branch runs pass no skip list; tools/select_host_gates_test.sh holds
# the calibration cases.
set -eu

SHARED="Makefile share/mk/ tools/host-test.mk tools/test-execution.mk
tools/compiler-probe.sh tools/select-host-gates.sh include/ lib/libc/ sys/
.github/ games/mutlib.py"

# gate|prefix ...
SUITES="check-fourfall-host|games/fourfall/ games/gametty.h tools/mutant_pool.py
check-puzzlebox-host|games/pzcommon/ games/puzzlebox/ games/flood/ games/samegame/ games/unruly/ games/lightup/ games/gameterm.h games/gametty.h tools/mutant_pool.py
check-ridge-host|games/ridge/ games/gametile.h tests/picocalc/shim/
check-calc-host|usr.bin/calc/ lib/libcalc/ tools/mutant_pool.py
check-libcalc|tests/libcalc/ lib/libcalc/ docs/research/picomite-maths-clean-room-spec.md
check-dcbc|tests/dcbc/ lib/libbn/ usr.bin/bc/ usr.bin/dc/"

case "${1-}" in
--paths)
	changed=$(cat)
	;;
'')
	echo "usage: select-host-gates.sh BASE HEAD | --paths" >&2
	exit 2
	;;
*)
	changed=$(git diff --name-only "$1" "${2:?HEAD required}" 2>/dev/null) || exit 0
	;;
esac

# starts_with PATH PREFIXES: a prefix ending in / names a directory, any
# other names one file.
starts_with() {
	for prefix in $2; do
		case $prefix in
		*/) case $1 in "$prefix"*) return 0 ;; esac ;;
		*) [ "$1" = "$prefix" ] && return 0 ;;
		esac
	done
	return 1
}

for path in $changed; do
	if starts_with "$path" "$SHARED"; then
		exit 0
	fi
done

skip=
newline='
'
old_ifs=$IFS
IFS=$newline
for suite in $SUITES; do
	IFS=$old_ifs
	gate=${suite%%|*}
	prefixes=${suite#*|}
	hit=
	for path in $changed; do
		if starts_with "$path" "$prefixes"; then
			hit=1
			break
		fi
	done
	[ -n "$hit" ] || skip="$skip${skip:+ }$gate"
	IFS=$newline
done
IFS=$old_ifs
printf '%s\n' "$skip"
