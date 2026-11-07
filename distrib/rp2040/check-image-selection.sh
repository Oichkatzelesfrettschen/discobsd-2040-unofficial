#!/bin/sh
#
# Calibrate the Make-level image selection: IMAGE_WITH and IMAGE_WITHOUT
# must reach distrib/rp2040/mkmanifest.py, a refused selection must stop
# `check-image-selection` with the reason, and GAMEBOX_TOOLS and
# PUZZLEBOX_TOOLS must carry the members the selection keeps. Each refusal
# asserts its reason, so a gate that stopped deciding fails here. Run from
# the top of the tree with PYTHON exported:
#
#   sh distrib/rp2040/check-image-selection.sh [MAKE]
#
# sys/arch/rp2040/doc/PROFILES.md is the authority for the variables.
set -eu

make=${1:-bmake}
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
# The refusals below make a child make stop on an error. A child that
# inherits the parent's job server (-j in MAKEFLAGS) broadcasts the stop to
# every make sharing the server, which then exits without output, so each
# child runs on its own: MAKEFLAGS is dropped and MACHINE named explicitly.
unset MAKEFLAGS MFLAGS
status=0

fail() {
	echo "check-image-selection: $*" >&2
	status=1
}

# refuse EXPECTED VARIABLE=VALUE ...: the selection must fail with EXPECTED.
refuse() {
	expected=$1
	shift
	if out=$("$make" MACHINE=rp2040 "$@" check-image-selection 2>&1); then
		fail "$* must be refused, and was accepted"
		return
	fi
	case $out in
	*"$expected"*) echo "check-image-selection: refuse $* -- $expected" ;;
	*) fail "$* is refused, but not for '$expected':" "$out" ;;
	esac
}

# accept VARIABLE=VALUE ...: the selection must compose.
accept() {
	if "$make" MACHINE=rp2040 "$@" check-image-selection >/dev/null 2>&1; then
		echo "check-image-selection: accept $*"
	else
		fail "$* must compose, and does not"
	fi
}

# members VARIABLE EXPECTED ASSIGNMENT ...: the exported member list.
members() {
	variable=$1
	expected=$2
	shift 2
	got=$("$make" MACHINE=rp2040 "$@" -V "\${$variable}")
	if [ "$got" = "$expected" ]; then
		echo "check-image-selection: $variable='$got' for $*"
	else
		fail "$variable is '$got' for $*, expected '$expected'"
	fi
}

refuse "closure toolchainlib needs closure cc, which this profile does not select" \
	IMAGE_WITHOUT=toolchain
refuse "unknown closure or group nosuch" IMAGE_WITH=nosuch
refuse "unknown closure or group nosuch" IMAGE_WITHOUT=nosuch
refuse "closure calc is both added (as calc) and removed (as calc)" \
	BOARD=PICOCALC IMAGE_WITH=calc IMAGE_WITHOUT=calc
refuse "closure gamebox is selected with none of its members" \
	IMAGE_WITHOUT="fifteen keen bubble fourfall coil"
refuse "takes closure or group names" "IMAGE_WITH=keen;true"
accept IMAGE_WITHOUT="toolchain toolchainlib"
accept BOARD=PICOCALC IMAGE_WITHOUT="calc-bignum calc"
accept PROFILE=base IMAGE_WITH="gamesdir puzzlebox lightup"
members GAMEBOX_TOOLS "fifteen keen bubble fourfall coil"
members GAMEBOX_TOOLS "fifteen bubble fourfall coil" IMAGE_WITHOUT=keen
members PUZZLEBOX_TOOLS "flood samegame unruly lightup"
members PUZZLEBOX_TOOLS "lightup" PROFILE=base IMAGE_WITH="gamesdir puzzlebox lightup"
members GAMEBOX_TOOLS "" PROFILE=base

exit "$status"
