#!/bin/sh
# Every mutant must fail the assertion for its removed admission contract.
set -eu

run_mutant()
{
    mutation=$1
    diagnostic=$2
    status=0
    program="./opendir_${mutation}.mutant.test"
    log="opendir_${mutation}.mutant.log"
    "$program" >"$log" 2>&1 || status=$?
    if [ "$status" -ne 1 ]; then
        echo "opendir: $mutation returned $status, expected 1" >&2
        exit 1
    fi
    if ! grep -Fq "$diagnostic" "$log"; then
        echo "opendir: $mutation misses $diagnostic" >&2
        cat "$log" >&2
        exit 1
    fi
    echo "PASS opendir mutation $mutation rejected"
}

run_mutant directory_type "regular file rejects directory admission"
run_mutant close_on_exec "directory descriptor has close-on-exec"
run_mutant fcntl_error_loss "close-on-exec failure stops directory admission"
run_mutant descriptor_cleanup "metadata failure closes descriptor once"
run_mutant cleanup_errno_loss "metadata cleanup preserves deciding errno"
