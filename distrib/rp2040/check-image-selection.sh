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
tmp=$(mktemp -d)
# The EXIT trap cleans; a signal handler that returned would resume the gate
# against the removed directory, so each signal exits with its status.
# The scratch directory holds only the flat manifest files the checks below
# write, so cleanup removes those files and then the directory.
cleanup() {
	for file in "$tmp"/*; do
		if [ -f "$file" ]; then rm -f "$file"; fi
	done
	rmdir "$tmp" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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
refuse "closure ridge needs closure lcdgfx, which this profile does not select" \
	IMAGE_WITH=ridge
refuse "closure ridge needs closure lcdgfx, which this profile does not select" \
	BOARD=PICOCALC IMAGE_WITHOUT=lcdgfx
refuse "closure gamebox is selected with none of its members" \
	BOARD=PICOCALC IMAGE_WITHOUT="fifteen keen bubble fourfall coil ridge"
accept IMAGE_WITHOUT="toolchain toolchainlib"
accept BOARD=PICOCALC IMAGE_WITHOUT="calc-bignum calc"
accept BOARD=PICOCALC IMAGE_WITHOUT=sdcard
accept BOARD=PICOCALC IMAGE_WITHOUT=psramtmp
accept PROFILE=base IMAGE_WITH=psramtmp
accept UNION_KERNEL=PICOCALC_LEAN
accept UNION_KERNEL=PICOCALC_LEAN_DIAG
members KERNEL_OUT_SUFFIX ""
members KERNEL_OUT_SUFFIX "" UNION_KERNEL=PICOCALC
members KERNEL_OUT_SUFFIX "-LEAN" UNION_KERNEL=PICOCALC_LEAN
members KERNEL_OUT_SUFFIX "-LEAN-DIAG" UNION_KERNEL=PICOCALC_LEAN_DIAG
members UNION_KERNEL_UF2 "$(pwd)/sys/arch/rp2040/compile/PICOCALC/unix.uf2"
members BOARD_KERNEL_UF2 "$(pwd)/distrib/rp2040/unix-PICOCALC.uf2" BOARD=PICOCALC
members BOARD_KERNEL_UF2 "$(pwd)/distrib/rp2040/unix-PICOCALC-LEAN.uf2" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
members BOARD_KERNEL_UF2 "$(pwd)/distrib/rp2040/unix-LEAN-DIAG.uf2" UNION_KERNEL=PICOCALC_LEAN_DIAG
refuse "UNION_KERNEL must be one of" UNION_KERNEL=PICOCALC_PSRAM
refuse "UNION_KERNEL must be one of" UNION_KERNEL=PICOCALC_DIAG
refuse "BOARD must be one of" BOARD=PICOCALC_PSRAM
accept PROFILE=base IMAGE_WITH=sdcard
accept PROFILE=base IMAGE_WITH="gamesdir puzzlebox lightup"
accept BOARD=PICOCALC IMAGE_WITHOUT="ridge lcdgfx"
accept PROFILE=base IMAGE_WITH="gamesdir gamebox ridge lcdgfx"
members GAMEBOX_TOOLS "fifteen keen bubble fourfall coil"
members GAMEBOX_TOOLS "fifteen bubble fourfall coil" IMAGE_WITHOUT=keen
members GAMEBOX_TOOLS "fifteen keen bubble fourfall coil ridge" BOARD=PICOCALC
members GAMEBOX_TOOLS "fifteen keen bubble fourfall coil" BOARD=PICOCALC IMAGE_WITHOUT=ridge
members GAMEBOX_TOOLS "ridge" PROFILE=base IMAGE_WITH="gamesdir gamebox ridge lcdgfx"
members PUZZLEBOX_TOOLS "flood samegame unruly lightup"
members PUZZLEBOX_TOOLS "lightup" PROFILE=base IMAGE_WITH="gamesdir puzzlebox lightup"
members GAMEBOX_TOOLS "" PROFILE=base

# The lean PicoCalc root pairs with the lean kernels. PROFILE and the image
# names follow UNION_KERNEL on the PICOCALC board only; an explicit PROFILE
# wins, and the suffix follows the profile that was built.
members PROFILE "full"
members PROFILE "picocalc" BOARD=PICOCALC
members PROFILE "picocalc" BOARD=PICOCALC UNION_KERNEL=PICOCALC
members PROFILE "picocalc-lean" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
members PROFILE "picocalc-lean" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN_DIAG
members PROFILE "full" UNION_KERNEL=PICOCALC_LEAN
members PROFILE "picocalc" BOARD=PICOCALC PROFILE=picocalc UNION_KERNEL=PICOCALC_LEAN
members FSIMG "$(pwd)/distrib/rp2040/sdcard-PICOCALC.img" BOARD=PICOCALC
members FSIMG "$(pwd)/distrib/rp2040/sdcard-PICOCALC-LEAN.img" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
members FSIMG "$(pwd)/distrib/rp2040/sdcard-PICOCALC-LEAN.img" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN_DIAG
members FLASHBIN "$(pwd)/distrib/rp2040/flash-PICOCALC-LEAN.bin" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
members FLASHUF2 "$(pwd)/distrib/rp2040/flash-PICOCALC-LEAN.uf2" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
members FLASHBIN "$(pwd)/distrib/rp2040/flash-PICOCALC.bin" BOARD=PICOCALC
members GAMEBOX_TOOLS "fifteen keen bubble fourfall coil" BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
accept BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN
accept BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN_DIAG
refuse "closure ridge needs closure lcdgfx, which this profile does not select" \
	BOARD=PICOCALC UNION_KERNEL=PICOCALC_LEAN IMAGE_WITH=ridge

# A root reaches a kernel whose driver it needs only if the kernel has the
# driver.
#
# composed PROFILE [mkmanifest option ...]: the manifest the profile composes,
# on stdout.
composed() {
	profile=$1
	shift
	"$PYTHON" distrib/rp2040/mkmanifest.py --profile="$profile" "$@" \
	    --manifest=distrib/rp2040/mi.rp2040 \
	    --profiles=distrib/rp2040/profiles \
	    --append=distrib/rp2040/md.rp2040 --output="$tmp/manifest" >/dev/null
	cat "$tmp/manifest"
}

# kernel_has KERNEL OPTION: the kernel's Config enables OPTION.
kernel_has() {
	grep -Eq "^options[[:space:]]+$2([[:space:]]|\$)" \
	    "sys/arch/rp2040/compile/$1/Config"
}

# lacks MANIFESTFILE PATH...: no line of the composed manifest installs PATH
# (the path is the second word of a dir, node, link or pack line).
lacks() {
	file=$1
	shift
	for p in "$@"; do
		if awk -v p="$p" '$2 == p { found = 1 } END { exit !found }' "$file"; then
			return 1
		fi
	done
	return 0
}

# A root that ships the pixel device node and its platformer needs
# DEV_LCDGFX. Calibration first: the full PicoCalc kernel has the option and
# its root has both paths, the lean kernels have no such option, so a lean
# root that carried a path would be reported.
composed picocalc >"$tmp/picocalc"
composed picocalc-lean >"$tmp/lean"
if ! kernel_has PICOCALC DEV_LCDGFX; then
	fail "calibration: PICOCALC Config does not enable DEV_LCDGFX"
fi
if lacks "$tmp/picocalc" /dev/lcdgfx /usr/games/ridge; then
	fail "calibration: the picocalc root lacks the pixel device paths"
fi
for kernel in PICOCALC_LEAN PICOCALC_LEAN_DIAG; do
	if kernel_has "$kernel" DEV_LCDGFX; then
		fail "$kernel enables DEV_LCDGFX, so it pairs with picocalc, not picocalc-lean"
	fi
done
if lacks "$tmp/lean" /dev/lcdgfx /usr/games/ridge; then
	echo "check-image-selection: picocalc-lean installs no /dev/lcdgfx and no platformer"
else
	fail "picocalc-lean installs a path that needs DEV_LCDGFX"
fi
composed picocalc-lean --with=lcdgfx --with=ridge >"$tmp/lean-bad"
if lacks "$tmp/lean-bad" /dev/lcdgfx /usr/games/ridge; then
	fail "calibration: a lean root with the pixel closures added still passes the path check"
else
	echo "check-image-selection: calibration: the path check reports a lean root with the pixel closures"
fi

# CoreMark and UnixBench live on the PicoCalc's SD root, so no default flash
# root carries /usr/bin/coremark. Calibration first: the benchmark profile and
# a picocalc root built with IMAGE_WITH=coremark do carry it.
composed benchmark >"$tmp/benchmark"
composed full >"$tmp/full"
composed picocalc --with=coremark >"$tmp/picocalc-coremark"
if lacks "$tmp/benchmark" /usr/bin/coremark; then
	fail "calibration: the benchmark root lacks /usr/bin/coremark"
fi
if lacks "$tmp/picocalc-coremark" /usr/bin/coremark; then
	fail "calibration: a picocalc root with IMAGE_WITH=coremark lacks /usr/bin/coremark"
fi
for root in full picocalc lean; do
	if lacks "$tmp/$root" /usr/bin/coremark; then
		echo "check-image-selection: the $root flash root carries no /usr/bin/coremark"
	else
		fail "the $root flash root carries /usr/bin/coremark, which the SD root holds"
	fi
done

exit "$status"
