#!/bin/sh

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/namei-path.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
ln -s "$topsrc/sys/arch/rp2040/include" "$work/machine"
mkdir "$work/mutant"

case $(uname -s) in
Darwin)
	section_gc_flags=-Wl,-dead_strip
	compiler_version=$(${CC:-cc} --version)
	case $compiler_version in
	*clang*)
		# Host Clang parses RP2040 assembly while this harness tests C behavior.
		host_warning_flags=-Wno-error=asm-operand-widths
		;;
	*)
		host_warning_flags=
		;;
	esac
	;;
*)
	section_gc_flags=-Wl,--gc-sections
	host_warning_flags=
	;;
esac

${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror \
	-Wstrict-prototypes -Wold-style-definition -Wno-sign-compare \
	-fno-builtin -fsanitize=address,undefined \
	-ffunction-sections -fdata-sections \
	-DKERNEL $host_warning_flags \
	-I"$work" -I"$topsrc/sys" -I"$topsrc/sys/arch" \
	-I"$topsrc/sys/arch/arm/include" \
	-I"$topsrc/sys/arch/rp2040/dhara/compat" \
	-I"$topsrc/sys/arch/rp2040/heatshrink/compat" \
	$section_gc_flags -DNAMEI_SOURCE=\"$topsrc/sys/kern/ufs_namei.c\" \
	-o "$work/namei-path-test" \
	"$topsrc/tests/rp2040/namei_path/namei_path_test.c"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/namei-path-test"

sed 's/return namei_copy_user_path(ndp->ni_dirp, destination);/return copystr(ndp->ni_dirp, destination, MAXPATHLEN, (u_int *)0);/' \
	"$topsrc/sys/kern/ufs_namei.c" >"$work/mutant/ufs_namei.c"
${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror \
	-Wstrict-prototypes -Wold-style-definition -Wno-sign-compare \
	-Wno-unused-function -fno-builtin -fsanitize=address,undefined \
	-ffunction-sections -fdata-sections -DKERNEL $host_warning_flags \
	-I"$work" -I"$topsrc/sys" -I"$topsrc/sys/arch" \
	-I"$topsrc/sys/arch/arm/include" \
	-I"$topsrc/sys/arch/rp2040/dhara/compat" \
	-I"$topsrc/sys/arch/rp2040/heatshrink/compat" \
	$section_gc_flags -DNAMEI_SOURCE=\"$work/mutant/ufs_namei.c\" \
	-o "$work/namei-path-mutant" \
	"$topsrc/tests/rp2040/namei_path/namei_path_test.c"
if ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$work/namei-path-mutant" >"$work/mutant.log" 2>&1; then
	echo "namei_path: FAIL: direct user copy mutation was accepted" >&2
	exit 1
fi
echo "namei_path: PASS (user-copy boundary, source type, calibrated direct-copy rejection)"
