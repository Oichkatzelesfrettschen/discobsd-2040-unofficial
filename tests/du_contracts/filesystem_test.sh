#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 du" >&2
	exit 2
fi

du_program=$(CDPATH= cd "$(dirname "$1")" && pwd)/$(basename "$1")
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM
cd "$temporary_directory"

mkdir -p tree/sub
printf 'root payload\n' > tree/root
printf 'child payload\n' > tree/sub/child
"$du_program" tree > ordinary
grep -Eq '[[:space:]]tree/sub$' ordinary
grep -Eq '[[:space:]]tree$' ordinary

"$du_program" -s tree > summary
if [ "$(wc -l < summary)" -ne 1 ]; then
	echo "du filesystem: -s printed more than one result" >&2
	exit 1
fi
grep -Eq '[[:space:]]tree$' summary

"$du_program" -s tree/ > trailing-slash
grep -Eq '[[:space:]]tree/$' trailing-slash

printf 'linked payload\n' > tree/linked-first
ln tree/linked-first tree/linked-second
"$du_program" -a tree > all
linked_count=$(
	grep -Ec '[[:space:]]tree/linked-(first|second)$' all || true
)
if [ "$linked_count" -ne 1 ]; then
	echo "du filesystem: a hard-linked inode was not counted once" >&2
	exit 1
fi
grep -Eq '[[:space:]]tree/sub/child$' all

if "$du_program" missing tree > multiple 2> multiple-error; then
	echo "du filesystem: a failed operand returned success" >&2
	exit 1
fi
grep -Eq '[[:space:]]tree$' multiple
grep -q 'missing' multiple-error

mkdir incomplete
printf 'incomplete link set\n' > incomplete/member
ln incomplete/member outside-member
"$du_program" -a incomplete > incomplete-output
grep -Eq '[[:space:]]incomplete/member$' incomplete-output

mkdir bulk
index=0
while [ "$index" -lt 1001 ]; do
	printf 'x' > "bulk/a$index"
	index=$((index + 1))
done
index=0
while [ "$index" -lt 1001 ]; do
	ln "bulk/a$index" "bulk/b$index"
	index=$((index + 1))
done
"$du_program" -a bulk > bulk-output
bulk_count=$(
	grep -Ec '[[:space:]]bulk/[ab][0-9]+$' bulk-output || true
)
if [ "$bulk_count" -ne 1001 ]; then
	echo "du filesystem: the dynamic hard-link set lost an identity" >&2
	exit 1
fi

if DU_FAIL_ALLOC=1 "$du_program" -a tree > allocation-output 2> allocation-error
then
	echo "du filesystem: allocation exhaustion returned success" >&2
	exit 1
fi
grep -q 'hard-link tracking exhausted' allocation-error
allocation_count=$(
	grep -Ec '[[:space:]]tree/linked-(first|second)$' allocation-output || true
)
if [ "$allocation_count" -ne 2 ]; then
	echo "du filesystem: allocation exhaustion lacks defined overcounting" >&2
	exit 1
fi

below_limit=$(awk 'BEGIN { for (i = 0; i < 1023; i++) printf "a" }')
at_limit=$(awk 'BEGIN { for (i = 0; i < 1024; i++) printf "a" }')
if "$du_program" "$below_limit" > /dev/null 2> below-error; then
	echo "du filesystem: a nonexistent boundary path returned success" >&2
	exit 1
fi
if grep -q 'path too long' below-error; then
	echo "du filesystem: a 1023-byte path was rejected by the path buffer" >&2
	exit 1
fi
if "$du_program" "$at_limit" > /dev/null 2> at-error; then
	echo "du filesystem: an over-capacity path returned success" >&2
	exit 1
fi
grep -q 'path too long' at-error

echo "du filesystem contract tests passed"
