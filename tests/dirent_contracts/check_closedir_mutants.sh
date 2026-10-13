#!/bin/sh
# Every mutant must fail the assertion for its removed close contract.
set -eu

run_mutant()
{
    mutation=$1
    diagnostic=$2
    status=0
    program="./closedir_${mutation}.mutant.test"
    log="closedir_${mutation}.mutant.log"
    "$program" >"$log" 2>&1 || status=$?
    if [ "$status" -ne 1 ]; then
        echo "closedir: $mutation returned $status, expected 1" >&2
        exit 1
    fi
    if ! grep -Fq "$diagnostic" "$log"; then
        echo "closedir: $mutation misses $diagnostic" >&2
        cat "$log" >&2
        exit 1
    fi
    echo "PASS closedir mutation $mutation rejected"
}

run_mutant result_loss "closedir reports close failure"
run_mutant errno_loss "closedir preserves close errno across free"
run_mutant free_loss "closedir success releases ownership once"
run_mutant close_repeat "closedir failure releases ownership once"
