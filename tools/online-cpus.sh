#!/bin/sh
# Print the online logical CPU count accepted by make's -j option. getconf
# covers the CI runners; sysctl covers BSD hosts and nproc covers GNU hosts
# whose getconf omits the extension. A host without any probe stays usable.
set -eu

valid_count() {
	case $1 in
	''|*[!0-9]*|0) return 1 ;;
	esac
	return 0
}

online_count=
if command -v getconf >/dev/null 2>&1; then
	online_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null || :)
fi
if ! valid_count "$online_count" && command -v sysctl >/dev/null 2>&1; then
	online_count=$(sysctl -n hw.logicalcpu 2>/dev/null ||
	    sysctl -n hw.ncpu 2>/dev/null || :)
fi
if ! valid_count "$online_count" && command -v nproc >/dev/null 2>&1; then
	online_count=$(nproc 2>/dev/null || :)
fi
if ! valid_count "$online_count"; then
	online_count=1
fi

printf '%s\n' "$online_count"
