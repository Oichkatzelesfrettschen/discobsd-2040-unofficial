#!/bin/sh
# Each mutant of bn.c, written by bn_mutate.py, must be rejected by bn_test
# with exit status 1 and a FAIL diagnostic. A mutant that passes, crashes,
# hangs past the time bound or fails to build means the test cannot see that fault.
# usage: check_mutants.sh VECTORS LIBBN_DIRECTORY
set -eu

: "${HOSTCC:?set HOSTCC to the host C compiler}"
vectors=$1
libdir=$2
count=0

for source in bn_*.mutant.c; do
    if [ ! -e "$source" ]; then
        echo "bn mutants: no mutant sources found" >&2
        exit 1
    fi
    name=${source%.mutant.c}
    "$HOSTCC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -O1 \
        -I"$libdir" -I. -DBN_SOURCE="\"$source\"" \
        -o "$name.mutant.test" bn_test.c
    status=0
    timeout 120 "./$name.mutant.test" "$vectors" > "$name.mutant.log" 2>&1 || status=$?
    if [ "$status" -eq 124 ]; then
        echo "bn mutants: $name did not finish within 120 seconds" >&2
        exit 1
    fi
    if [ "$status" -ne 1 ]; then
        echo "bn mutants: $name exited $status, expected 1" >&2
        exit 1
    fi
    if ! grep -q '^FAIL ' "$name.mutant.log"; then
        echo "bn mutants: $name printed no FAIL diagnostic" >&2
        exit 1
    fi
    echo "PASS libbn mutation $name rejected: $(grep -m1 '^FAIL ' "$name.mutant.log" | cut -c1-72)"
    count=$((count + 1))
done
echo "bn mutants: $count of $count rejected"
