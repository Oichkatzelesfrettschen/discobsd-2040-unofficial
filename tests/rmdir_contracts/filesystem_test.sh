#!/bin/sh
set -eu

rmdir_program=$1
test_directory=$(mktemp -d "${TMPDIR:-/tmp}/discobsd-rmdir.XXXXXX")
trap 'rm -rf "$test_directory"' EXIT HUP INT TERM

fail()
{
    echo "rmdir filesystem test: $1" >&2
    exit 1
}

cd "$test_directory"

mkdir -p remove-all/parent/leaf
"$rmdir_program" -p remove-all/parent/leaf || \
    fail "complete parent removal failed"
[ ! -e remove-all ] || fail "complete parent removal left a component"

mkdir -p trailing/parent/leaf
"$rmdir_program" -p trailing/parent/leaf/// || \
    fail "trailing slash removal failed"
[ ! -e trailing ] || fail "trailing slash removal left a component"

mkdir -p partial/parent/leaf
: > partial/parent/keep
if "$rmdir_program" -p partial/parent/leaf 2> partial.stderr; then
    fail "partial parent removal reported success"
fi
[ ! -e partial/parent/leaf ] || fail "partial removal left the leaf"
[ -f partial/parent/keep ] || fail "partial removal changed the parent"
grep -F "rmdir: partial/parent:" partial.stderr >/dev/null || \
    fail "partial removal named the wrong failed component"

mkdir -p initial/leaf
: > initial/leaf/keep
if "$rmdir_program" -p initial/leaf 2> initial.stderr; then
    fail "non-empty leaf reported success"
fi
[ -f initial/leaf/keep ] || fail "initial failure changed the operand"
grep -F "rmdir: initial/leaf:" initial.stderr >/dev/null || \
    fail "initial failure named the wrong component"

mkdir -p multiple/bad multiple/good/leaf
: > multiple/bad/keep
if "$rmdir_program" -p multiple/bad multiple/good/leaf \
    2> multiple.stderr; then
    fail "multiple operands hid a failure"
fi
[ -f multiple/bad/keep ] || fail "failed operand changed"
[ ! -e multiple/good ] || fail "later operand was not processed"

mkdir ./-hyphen
"$rmdir_program" -- -hyphen || fail "option terminator rejected an operand"
[ ! -e ./-hyphen ] || fail "option terminator left the operand"

if "$rmdir_program" > usage.stdout 2> usage.stderr; then
    fail "missing operand reported success"
fi
grep -F "usage:" usage.stderr >/dev/null || \
    fail "missing operand omitted usage"

if "$rmdir_program" -z > option.stdout 2> option.stderr; then
    fail "unknown option reported success"
fi
grep -F "usage:" option.stderr >/dev/null || \
    fail "unknown option omitted usage"

echo "rmdir filesystem tests passed"
