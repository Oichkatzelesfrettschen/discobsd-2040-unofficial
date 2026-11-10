#!/bin/sh
# Require an object's undefined symbols to equal the listed set exactly.
#   check_imports.sh NM OBJECT [SYMBOL...]
# A 64-bit multiply or divide the compiler could not open-code appears here
# as an __aeabi_ or libgcc import, and libc links no such routine.
set -eu

nm=$1
object=$2
shift 2

actual=$("$nm" -u "$object" | awk '{ print $NF }' | sort | tr '\n' ' ')
expected=$(for symbol in "$@"; do echo "$symbol"; done | sort | tr '\n' ' ')
if [ "$actual" != "$expected" ]; then
	echo "check_imports: $object imports [ $actual], expected [ $expected]" >&2
	exit 1
fi
