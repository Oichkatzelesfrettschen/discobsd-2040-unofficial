#!/bin/sh
# The linked target objects may reference only the C library entry points
# below and the soft-float and 64-bit integer helpers the compiler emits for
# a Cortex-M0+ without FPU. Any other undefined symbol (an allocator, stdio,
# a hard-float routine) means libcalc has grown a dependency the target
# cannot satisfy.
# usage: check_object.sh LD NM SIZE OBJECT...
set -eu

ld=$1
nm=$2
size=$3
shift 3
combined=$(mktemp "${TMPDIR:-/tmp}/libcalc-combined.XXXXXX")
trap 'rm -f "$combined"' EXIT HUP INT TERM
status=0

# A relocatable link resolves the references between the libcalc objects
# themselves, which leaves only the true external symbols undefined.
"$ld" -r -o "$combined" "$@"
for symbol in $("$nm" -u "$combined" | awk '{ print $2 }'); do
    case $symbol in
    atan2 | cos | exp | floor | log | sin | sqrt) ;;
    memcpy | memmove | memset) ;;
    __aeabi_* | __gnu_thumb1_case_*) ;;
    *)
        echo "check_object: references $symbol" >&2
        status=1
        ;;
    esac
done
"$size" -t "$@"
exit "$status"
