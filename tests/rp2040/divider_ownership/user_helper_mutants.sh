#!/bin/sh
# Calibrates the --user-helper check of the divider ownership verifier: the
# real division helpers must pass, and each deliberate defect in a copy of
# them must exit 1 with the finding named for it. Usage:
#   user_helper_mutants.sh VERIFIER HELPER.S
# PYTHON and FIXTURE_CC come from the Makefile.
set -eu

verifier=${1:?usage: user_helper_mutants.sh VERIFIER HELPER.S}
source_file=${2:?usage: user_helper_mutants.sh VERIFIER HELPER.S}
: "${PYTHON:?}" "${FIXTURE_CC:?}"

assemble() {
	"$FIXTURE_CC" -x assembler-with-cpp -mcpu=cortex-m0plus -mthumb \
	    -mfloat-abi=soft -Wa,--fatal-warnings -c -o "$2" "$1"
}

assemble "$source_file" user_helper_real.o
"$PYTHON" "$verifier" --user-helper user_helper_real.o \
    > user_helper_real.stdout 2> user_helper_real.stderr
test ! -s user_helper_real.stderr
grep -F 'user_helper_entries=2 sio_accesses=8 min_wait_cycles=8 user_helper_findings=0' \
    user_helper_real.stdout

# mutant NAME AWK-PROGRAM EXPECTED-FINDING
mutant() {
	awk "$2" "$source_file" > "user_helper_$1.S"
	if cmp -s "$source_file" "user_helper_$1.S"; then
		echo "mutant $1: the defect did not change the source" >&2
		exit 2
	fi
	assemble "user_helper_$1.S" "user_helper_$1.o"
	if "$PYTHON" "$verifier" --user-helper "user_helper_$1.o" \
	    > "user_helper_$1.stdout" 2> "user_helper_$1.stderr"; then
		echo "mutant $1: the verifier accepted it" >&2
		exit 1
	else
		status=$?
		test "$status" -eq 1
	fi
	grep -F -- "$3" "user_helper_$1.stderr" > /dev/null || {
		echo "mutant $1: no finding containing '$3'" >&2
		cat "user_helper_$1.stderr" >&2
		exit 1
	}
	echo "user helper mutant $1: rejected"
}

mutant short_wait '/^1:\tb\t1f/ { n++; if (n == 1) next } 1' \
    'cycles between the divisor store'
mutant quotient_first \
    '/ldr\tr1, \[r3, #0x74\]/ { sub(/0x74/, "0x70") } /ldr\tr0, \[r3, #0x70\]/ { sub(/0x70/, "0x74") } 1' \
    'divider accesses are'
mutant csr_poll '/^1:\tb\t1f/ { n++; if (n == 1) { print "\tldr\tr2, [r3, #0x78]"; next } } 1' \
    'divider accesses are'
mutant system_call '/str\tr1, \[r3, #0x64\]/ { print; print "\tsvc\t#0"; next } 1' \
    'svc #0'
mutant call_inside '/str\tr1, \[r3, #0x64\]/ { print; print "\tbl\t__aeabi_uidiv"; next } 1' \
    'inside the divider window'
mutant stack_inside '/str\tr1, \[r3, #0x64\]/ { print; print "\tpush\t{r4}"; next } 1' \
    'inside the divider window'
mutant breakpoint_inside '/str\tr1, \[r3, #0x64\]/ { print; print "\tbkpt\t#0"; next } 1' \
    'inside the divider window'
mutant wrong_base '/movs\tr3, #0xd0/ { sub(/0xd0/, "0xd1") } 1' \
    'not the SIO base'
mutant missing_helper '/__aeabi_idivmod:/ { next } /\.globl\t__aeabi_idivmod/ { next } /\.type\t__aeabi_idivmod/ { next } 1' \
    'missing helper __aeabi_idivmod'
