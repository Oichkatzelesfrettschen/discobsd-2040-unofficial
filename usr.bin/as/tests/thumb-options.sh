#!/bin/sh
#
# Test (d): the assembler accepts the options gcc forwards on the native
# a.out route and produces the same object it produces without them.
#
# For an assembler source (.S), gcc -fdebug-prefix-map=OLD=NEW runs the
# assembler selected by -B as "as ... --debug-prefix-map OLD=NEW ...", with
# the operand in its own argument, whether or not -g is given; for C, cc1
# applies the mapping itself. lib/libc_aout assembles its .S files this
# way. The assembler writes no debug sections, so each object must be
# byte-identical to the one assembled without the options; an operand read
# as a source file fails the run with "too many input files".
#
set -eu

AS=${AS:?set AS to the assembler under test}
CC=${CC:?set CC to the cross compiler command}
work=${WORK:-.}
dir=$work/thumb-options
rm -rf "$dir"
mkdir -p "$dir/bin"

cat >"$dir/t.s" <<'EOF'
	.syntax unified
	.thumb
	.text
	.globl	f
f:
	movs	r0, #1
	bx	lr
	.data
v:	.word	f
EOF
cp "$dir/t.s" "$dir/cpp.S"

fail=0
"$AS" "$dir/t.s" -o "$dir/plain.aout"
for args in "-g --debug-prefix-map /src=." "--debug-prefix-map=/src=. -g"; do
	# shellcheck disable=SC2086 # args is a word list
	if ! "$AS" $args "$dir/t.s" -o "$dir/opt.aout"; then
		echo "options: as $args failed"
		fail=$((fail + 1))
	elif ! cmp -s "$dir/plain.aout" "$dir/opt.aout"; then
		echo "options: as $args changed the object"
		fail=$((fail + 1))
	fi
done

# The forwarded route: gcc picks the assembler named "as" in the -B
# directory, as lib/libc_aout does.
ln -s "$AS" "$dir/bin/as"
$CC -B"$dir/bin/" -c "$dir/cpp.S" -o "$dir/plain-S.aout"
if ! $CC -g -fdebug-prefix-map="$dir"=. -B"$dir/bin/" \
    -c "$dir/cpp.S" -o "$dir/opt-S.aout"; then
	echo "options: gcc -g -fdebug-prefix-map on a .S source through -B failed"
	fail=$((fail + 1))
elif ! cmp -s "$dir/plain-S.aout" "$dir/opt-S.aout"; then
	echo "options: gcc -g -fdebug-prefix-map on a .S source changed the object"
	fail=$((fail + 1))
fi

if [ "$fail" -ne 0 ]; then
	echo "thumb-options: $fail failures"
	exit 1
fi
echo "thumb-options: forwarded debug options accepted, objects unchanged"
