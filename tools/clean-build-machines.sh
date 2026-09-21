#!/bin/sh
# Remove every shared artifact class before releasing the machine tuple.

set -eu

if [ "$#" -lt 3 ]; then
	echo "usage: $0 SOURCE_ROOT MAKE MACHINE..." >&2
	exit 2
fi

source_root=$1
make_command=$2
shift 2

# Parent bmake overrides outrank nested command-line assignments when they
# remain in MAKEFLAGS. Each cleanup invocation below owns its explicit tuple.
unset MAKEFLAGS MFLAGS MACHINE MACHINE_ARCH MACHINE_CPU

if [ ! -d "$source_root" ]; then
	echo "$source_root: source root is not a directory" >&2
	exit 2
fi
source_root=$(CDPATH= cd "$source_root" && pwd -P)
stamp_path=$source_root/distrib/obj/.build-machine
case $make_command in
*/*)
	if [ ! -x "$make_command" ]; then
		echo "$make_command: make command is not executable" >&2
		exit 2
	fi
	;;
*)
	if ! command -v "$make_command" >/dev/null 2>&1; then
		echo "$make_command: make command is not executable" >&2
		exit 2
	fi
	;;
esac

cleanup_status=0

run_make()
{
	if "$make_command" "$@"; then
		:
	else
		cleanup_status=1
	fi
}

run_make -C "$source_root/legacy/pdp11-v6" MACHINE=rp2040 \
	BUILD_PDP11_V6=yes clean

for machine in "$@"; do
	run_make -C "$source_root" MACHINE="$machine" \
		BUILD_LEGACY_NON_ARM=no BUILD_PDP11_V6=no \
		DESTDIR="$source_root/distrib/obj/destdir.$machine" \
		RELEASEDIR="$source_root/distrib/obj/releasedir.$machine" clean
done

run_make -C "$source_root" cleantools
run_make -C "$source_root" cleankernel

if [ "$cleanup_status" -ne 0 ]; then
	echo "cleanall left $stamp_path in place" >&2
	exit "$cleanup_status"
fi

rm -f "$stamp_path"
