#!/bin/sh
# The target object may call only the C library entry points below and the
# 32-bit EABI divide helpers. A reference to a 64-bit helper (__aeabi_lmul,
# __aeabi_uldivmod, ...) means a limb loop widened to 64 bits, which the
# 16-bit limb design exists to avoid on the Cortex-M0+.
# usage: check_object.sh NM SIZE OBJECT
set -eu

nm=$1
size=$2
object=$3
status=0

for symbol in $("$nm" -u "$object" | awk '{ print $2 }'); do
    case $symbol in
    malloc | realloc | free | memcpy | memmove | memset) ;;
    __aeabi_uidiv | __aeabi_uidivmod) ;;
    *)
        echo "check_object: $object references $symbol" >&2
        status=1
        ;;
    esac
done
"$size" "$object"
exit "$status"
