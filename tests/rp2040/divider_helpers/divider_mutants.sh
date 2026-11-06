#!/bin/sh
# Builds the division helpers with one deliberate defect at a time and
# requires divider_test to reject each. Usage: divider_mutants.sh SOURCE.
# MAKE, QEMU_ARM and QEMU_CPU come from the Makefile. A defect the test
# accepts means the test cannot see that class of fault, so it fails the
# gate.
set -eu

source_file=${1:?usage: divider_mutants.sh SOURCE.S}
: "${MAKE:?}" "${QEMU_ARM:?}" "${QEMU_CPU:?}"
work=$(pwd)
survivors=0
total=0

# mutate NAME AWK-PROGRAM: writes mutant_NAME.S from the real source.
mutate() {
	awk "$2" "$source_file" > "mutant_$1.S"
	if cmp -s "$source_file" "mutant_$1.S"; then
		echo "mutant $1: the defect did not change the source" >&2
		exit 2
	fi
}

# run NAME: builds and runs one mutant; a clean exit is a survivor.
run() {
	total=$((total + 1))
	if ! "$MAKE" PROG="mutant_$1" DIV_SOURCE="$work/mutant_$1.S" \
	    "mutant_$1" > "mutant_$1.build" 2>&1; then
		echo "mutant $1: build failed" >&2
		cat "mutant_$1.build" >&2
		exit 2
	fi
	if "$QEMU_ARM" -cpu "$QEMU_CPU" "./mutant_$1" > "mutant_$1.out" 2>&1; then
		echo "mutant $1: SURVIVED" >&2
		survivors=$((survivors + 1))
	else
		echo "mutant $1: rejected"
	fi
}

# The remainder takes the quotient's sign instead of the dividend's.
mutate remainder_sign '/eors\tr1, r0/ { sub(/r0/, "r2") } 1'
# The signed zero-divisor guard is gone, so a zero divisor reaches the divider.
mutate signed_zero_guard '/beq\t2f/ { n++; if (n == 2) next } 1'
# The unsigned zero-divisor guard is gone.
mutate unsigned_zero_guard '/beq\t2f/ { n++; if (n == 1) next } 1'
# The remainder is read from the quotient register.
mutate remainder_offset '/ldr\tr1, \[r3, #0x74\]/ { sub(/0x74/, "0x70") } 1'
# The quotient is read from the remainder register.
mutate quotient_offset '/ldr\tr0, \[r3, #0x70\]/ { sub(/0x70/, "0x74") } 1'
# The divisor is written to the signed alias, which no helper may touch.
mutate divisor_alias '/str\tr1, \[r3, #0x64\]/ { sub(/0x64/, "0x6c") } 1'
# The quotient keeps the magnitude's sign.
mutate quotient_sign '/subs\tr0, r3, r2/ { sub(/r2/, "#0") } 1'
# A zero divisor returns the dividend's magnitude as the remainder.
mutate signed_zero_remainder '/^2:\tmov\tr1, ip/ { print "2:\tmovs\tr1, r0"; getline; getline; next } 1'
# The helper clobbers a callee-saved register.
mutate callee_saved '/bx\tlr/ { n++; if (n == 1) print "\tmovs\tr4, #1" } 1'
# The helper never reaches the divider's wait, so it reads stale results.
mutate no_wait '/DIVIDER_MODEL_WAIT/ { next } 1'
# The dividend is magnitude-converted with the wrong mask.
mutate magnitude 'NR > 0 && /subs\tr0, r0, r3/ { sub(/r0, r0, r3/, "r0, r0, r2") } 1'

for name in remainder_sign signed_zero_guard unsigned_zero_guard \
    remainder_offset quotient_offset divisor_alias quotient_sign \
    signed_zero_remainder callee_saved no_wait magnitude; do
	run "$name"
done

echo "divider mutants: $total built, $survivors survived"
[ "$survivors" -eq 0 ]
