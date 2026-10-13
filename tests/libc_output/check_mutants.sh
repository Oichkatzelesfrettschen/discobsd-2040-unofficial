#!/bin/sh
set -eu
run_mutant()
{
    mutation=$1
    diagnostic=$2
    status=0
    "./mutant_$mutation/test" > "mutant_$mutation/result.log" 2>&1 || status=$?
    if [ "$status" -ne 1 ]; then
        echo "output: $mutation returns $status, expected 1" >&2
        exit 1
    fi
    grep -Fq "$diagnostic" "mutant_$mutation/result.log"
    echo "PASS output mutation $mutation rejected"
}
run_mutant count 'FAIL output success return'
run_mutant capacity 'FAIL output bounded write count'
run_mutant pointer 'FAIL output temporary buffer release'
run_mutant flush 'FAIL output final flush error'
run_mutant continue 'FAIL output stops after fputs failure'
