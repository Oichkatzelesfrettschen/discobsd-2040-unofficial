#!/bin/sh
set -eu

unifdef_program=$1
test_directory=$(mktemp -d)
trap 'rm -rf "${test_directory}"' EXIT HUP INT TERM

run_case()
{
	expected_status=$1
	arguments=$2
	input_text=$3
	expected_text=$4

	printf '%s' "${input_text}" > "${test_directory}/input"
	printf '%s' "${expected_text}" > "${test_directory}/expected"
	set +e
	# The contract accepts classic combined options; word splitting supplies
	# the independently bounded option strings in this host-only fixture.
	# shellcheck disable=SC2086
	"${unifdef_program}" ${arguments} < "${test_directory}/input" \
	    > "${test_directory}/output" 2> "${test_directory}/error"
	actual_status=$?
	set -e
	if [ "${actual_status}" -ne "${expected_status}" ]; then
		echo "unifdef CLI status ${actual_status}, expected ${expected_status}" >&2
		exit 1
	fi
	cmp "${test_directory}/expected" "${test_directory}/output"
}

run_case 0 '-DFOO' 'plain' 'plain'
run_case 1 '-DFOO' '#ifdef FOO
yes
#else
no
#endif' 'yes
'
run_case 1 '-UFOO' '#ifndef FOO
yes
#endif
' 'yes
'
run_case 1 '-UFOO' '#ifdef FOO
no
#endif
' ''
run_case 0 '-iDFOO' '#ifdef FOO
kept
#else
also kept
#endif
' '#ifdef FOO
kept
#else
also kept
#endif
'
run_case 0 '-iUFOO' '#ifdef FOO
kept
#endif
' '#ifdef FOO
kept
#endif
'
run_case 0 '-iUFOO' '#ifdef FOO
/* non-C ignored text
#endif
' '#ifdef FOO
/* non-C ignored text
#endif
'
run_case 0 '-iDFOO' '#ifdef FOO
kept
#else
"non-C ignored text
#endif
' '#ifdef FOO
kept
#else
"non-C ignored text
#endif
'
run_case 1 '-DFOO' '# /* gap */ ifdef /* gap */ FOO
kept
#endif
' 'kept
'
run_case 1 '-DFOO' '#ifdef FOO /* open
comment */
int kept;
#endif
' '/* open
comment */
int kept;
'
run_case 1 '-UFOO' '#ifdef FOO /* open
comment */
int removed;
#endif
' ''
run_case 1 '-DFOO' '#ifdef FOO
kept
#else /* open
comment */
removed
#endif
' 'kept
'
run_case 1 '-UOUTER -DFOO' '#ifdef OUTER
#ifdef FOO /* open
comment */
#endif
#endif
' ''
run_case 1 '-t -DFOO' '/* plain text
#ifdef FOO
kept
#endif' '/* plain text
kept
'
run_case 1 '-t -DFOO' '#ifdef FOO /* plain text
kept
#endif
' 'kept
'
run_case 1 '-l -DFOO' '#ifdef FOO
yes
#endif
' '
yes

'
run_case 1 '-c -DFOO' '#ifdef FOO
yes
#else
no
#endif
' '#ifdef FOO
#else
no
#endif
'
run_case 0 '-c -UFOO' '#ifdef FOO
no
#endif
' '#ifdef FOO
no
#endif
'
run_case 1 '-c -DFOO' '/*
#ifdef FOO
*/
' ''

echo "unifdef CLI contracts passed"
