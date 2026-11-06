#!/bin/sh
# Each mutant of lib/libcalc, written by calc_mutate.py, must be rejected by
# calc_test with exit status 1 and a FAIL diagnostic. A mutant that passes,
# crashes, hangs or fails to build means the test cannot see that fault.
# usage: check_mutants.sh MUTANT_DIRECTORY
set -eu

: "${HOSTCC:?set HOSTCC to the host C compiler}"
root=$1
count=0
total=0

for dir in "$root"/*/; do
    [ -d "$dir" ] || continue
    total=$((total + 1))
done
if [ "$total" -eq 0 ]; then
    echo "calc mutants: no mutants found in $root" >&2
    exit 1
fi

for dir in "$root"/*/; do
    name=$(basename "$dir")
    # The sources are word-split on purpose: file names carry no spaces.
    # shellcheck disable=SC2086
    "$HOSTCC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -Wshadow -Wconversion \
        -O1 -I"$dir" -o "$dir/calc_test" calc_test.c "$dir"calc_*.c -lm
    status=0
    timeout 120 "$dir/calc_test" > "$dir/run.log" 2>&1 || status=$?
    if [ "$status" -ne 1 ]; then
        echo "calc mutants: $name exited $status, expected 1" >&2
        exit 1
    fi
    if ! grep -q '^FAIL ' "$dir/run.log"; then
        echo "calc mutants: $name printed no FAIL diagnostic" >&2
        exit 1
    fi
    echo "PASS libcalc mutation $name rejected: $(grep -m1 '^FAIL ' "$dir/run.log" | cut -c1-72)"
    count=$((count + 1))
done
echo "calc mutants: $count of $total rejected"
