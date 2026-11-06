#!/bin/sh
# Every status-returning entry point declared in mathematics.h must appear in
# the NULL-pointer sweep of calc_test.c (a NULL_ARG(...) call), so a function
# added without its sweep entry fails here instead of passing unnoticed.
# usage: check_null_sweep.sh HEADER TEST_SOURCE
set -eu

header=$1
source=$2
missing=0

# Declarations start at the beginning of a line: "calc_status calc_name(".
declared=$(sed -n 's/^calc_status \(calc_[a-z0-9_]*\)(.*/\1/p' "$header" | sort -u)
if [ -z "$declared" ]; then
    echo "libcalc null sweep: no declarations found in $header" >&2
    exit 1
fi
for name in $declared; do
    if ! grep -q "NULL_ARG($name(" "$source"; then
        echo "libcalc null sweep: $name has no NULL_ARG entry in $source" >&2
        missing=$((missing + 1))
    fi
done
if [ "$missing" -ne 0 ]; then
    exit 1
fi
count=$(printf '%s\n' "$declared" | wc -l | tr -d ' ')
echo "libcalc null sweep: $count entry points covered"
