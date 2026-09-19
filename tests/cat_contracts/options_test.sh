#!/bin/sh
set -eu

cat_program=$1
test_directory=$(mktemp -d "${TMPDIR:-/tmp}/discobsd-cat.XXXXXX")
trap 'rm -rf "$test_directory"' EXIT HUP INT TERM

fail()
{
    echo "cat option test: $1" >&2
    exit 1
}

printf 'alpha\n\nbeta\t\200\n' > "$test_directory/input"
printf 'omega\n' > "$test_directory/second"
printf 'alpha\n\nbeta\t\200\nomega\n' > "$test_directory/raw.expected"
"$cat_program" "$test_directory/input" "$test_directory/second" \
    > "$test_directory/raw.actual"
cmp "$test_directory/raw.expected" "$test_directory/raw.actual" || \
    fail "raw multi-file output changed"

printf '     1\talpha\n     2\t\n     3\tbeta\t\200\n' \
    > "$test_directory/number.expected"
"$cat_program" -n "$test_directory/input" > "$test_directory/number.actual"
cmp "$test_directory/number.expected" "$test_directory/number.actual" || \
    fail "-n output changed"

printf '     1\talpha\n\n     2\tbeta\t\200\n' \
    > "$test_directory/nonblank.expected"
"$cat_program" -b "$test_directory/input" > "$test_directory/nonblank.actual"
cmp "$test_directory/nonblank.expected" "$test_directory/nonblank.actual" || \
    fail "-b output changed"

printf '\n\n\nvalue\n\n\n' > "$test_directory/blanks"
printf '\nvalue\n\n' > "$test_directory/squeeze.expected"
"$cat_program" -s "$test_directory/blanks" > "$test_directory/squeeze.actual"
cmp "$test_directory/squeeze.expected" "$test_directory/squeeze.actual" || \
    fail "-s output changed"

printf 'alpha$\n$\nbeta\tM-^@$\n' > "$test_directory/ends.expected"
"$cat_program" -e "$test_directory/input" > "$test_directory/ends.actual"
cmp "$test_directory/ends.expected" "$test_directory/ends.actual" || \
    fail "-e output changed"

printf 'alpha\n\nbeta^IM-^@\n' > "$test_directory/tabs.expected"
"$cat_program" -t "$test_directory/input" > "$test_directory/tabs.actual"
cmp "$test_directory/tabs.expected" "$test_directory/tabs.actual" || \
    fail "-t output changed"

printf 'alpha\n\nbeta\tM-^@\n' > "$test_directory/nonprinting.expected"
"$cat_program" -v "$test_directory/input" \
    > "$test_directory/nonprinting.actual"
cmp "$test_directory/nonprinting.expected" \
    "$test_directory/nonprinting.actual" || fail "-v output changed"

"$cat_program" -u "$test_directory/input" > "$test_directory/unbuffered.actual"
cmp "$test_directory/input" "$test_directory/unbuffered.actual" || \
    fail "-u output changed"

cp "$test_directory/input" "$test_directory/self.expected"
cp "$test_directory/input" "$test_directory/self.actual"
if "$cat_program" "$test_directory/self.actual" \
    >> "$test_directory/self.actual" 2> "$test_directory/self.stderr"; then
    fail "self-output input succeeded"
fi
cmp "$test_directory/self.expected" "$test_directory/self.actual" || \
    fail "self-output input changed the file"

echo "cat option tests passed"
