#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 CHECKER" >&2
	exit 2
fi

checker=$1
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/file-owner-ubsan.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

write_mutant()
{
	mutant_path=$1
	mutant_body=$2
	{
		echo '#!/bin/sh'
		echo "$mutant_body"
	} >"$mutant_path"
	chmod +x "$mutant_path"
}

expect_acceptance()
{
	mutant_path=$1
	case_name=$2
	acceptance_output=$temporary_directory/acceptance.out
	if ! (cd "$temporary_directory" && sh "$checker" "$mutant_path") \
	    >"$acceptance_output" 2>&1; then
		cat "$acceptance_output" >&2
		echo "file-owner UBSan oracle: rejected $case_name" >&2
		exit 1
	fi
}

expect_rejection()
{
	mutant_path=$1
	case_name=$2
	if (cd "$temporary_directory" && sh "$checker" "$mutant_path") \
	    >/dev/null 2>&1; then
		echo "file-owner UBSan oracle: accepted $case_name" >&2
		exit 1
	fi
}

diagnostic='runtime error: negation of -2147483648'
regular_mutant=$temporary_directory/regular-mutant
abort_mutant=$temporary_directory/abort-mutant
success_mutant=$temporary_directory/success-mutant
wrong_status_mutant=$temporary_directory/wrong-status-mutant
wrong_diagnostic_mutant=$temporary_directory/wrong-diagnostic-mutant

write_mutant "$regular_mutant" "echo '$diagnostic' >&2; exit 1"
write_mutant "$abort_mutant" "echo '$diagnostic' >&2; kill -ABRT \$\$"
write_mutant "$success_mutant" "echo '$diagnostic' >&2; exit 0"
write_mutant "$wrong_status_mutant" "echo '$diagnostic' >&2; exit 2"
write_mutant "$wrong_diagnostic_mutant" "echo 'unrelated failure' >&2; exit 1"

expect_acceptance "$regular_mutant" "regular sanitizer termination"
expect_acceptance "$abort_mutant" "SIGABRT sanitizer termination"
expect_rejection "$success_mutant" "successful execution"
expect_rejection "$wrong_status_mutant" "unrecognized failure status"
expect_rejection "$wrong_diagnostic_mutant" "unrelated diagnostic"

echo "file-owner UBSan oracle calibration passed"
