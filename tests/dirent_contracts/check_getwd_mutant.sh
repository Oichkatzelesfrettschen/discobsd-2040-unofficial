#!/bin/sh
# The release-before-copy mutation must fail the borrowed-entry assertion.
set -eu
status=0
./getwd_lifetime_mutant > getwd_lifetime_mutant.log 2>&1 || status=$?
if [ "$status" -ne 1 ]; then
    echo "getwd lifetime mutation returned $status, expected 1" >&2
    exit 1
fi
grep -Fq 'FAIL getwd borrowed entry lifetime' getwd_lifetime_mutant.log
echo 'PASS getwd release-before-copy mutation rejected'
