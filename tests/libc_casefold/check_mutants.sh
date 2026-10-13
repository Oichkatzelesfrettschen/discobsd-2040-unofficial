#!/bin/sh
# Each semantic mutation must fail its named behavior assertion.
set -eu
run_mutant()
{
    mutation=$1
    diagnostic=$2
    status=0
    "./casefold_${mutation}.mutant.test" > "casefold_${mutation}.mutant.log" 2>&1 || status=$?
    if [ "$status" -ne 1 ]; then
        echo "casefold: $mutation returned $status, expected 1" >&2
        exit 1
    fi
    grep -Fq "$diagnostic" "casefold_${mutation}.mutant.log"
    echo "PASS casefold mutation $mutation rejected"
}
run_mutant high_bytes 'FAIL casefold byte oracle'
run_mutant signed_order 'FAIL casefold byte oracle'
run_mutant count_early 'FAIL casefold count and NUL boundaries'
run_mutant errno_loss 'FAIL casefold errno preservation'
