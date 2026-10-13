#!/bin/sh
# Each mutant must fail the assertion that names its removed contract.
set -eu

run_mutant()
{
	mutation_name=$1
	expected_diagnostic=$2
	program="./scandir_${mutation_name}.mutant.test"
	log="scandir_${mutation_name}.mutant.log"
	status=0
	"$program" >"$log" 2>&1 || status=$?
	if [ "$status" -ne 1 ]; then
		echo "FAIL: $mutation_name returned $status, expected 1" >&2
		exit 1
	fi
	if ! grep -Fq -- "$expected_diagnostic" "$log"; then
		echo "FAIL: $mutation_name missed: $expected_diagnostic" >&2
		cat "$log" >&2
		exit 1
	fi
	echo "PASS scandir mutation $mutation_name rejected"
}

run_mutant read_error_loss "read failure after accepted records"
run_mutant cleanup_errno_loss "allocation failure preserves errno"
run_mutant leak_record "allocation failure releases records"
run_mutant metadata_growth "geometric vector capacity"
run_mutant source_reclen "compact copy records allocated length"
run_mutant shared_comparator "nested comparator retains outer order"
