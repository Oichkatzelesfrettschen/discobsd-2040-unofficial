#!/bin/sh
# The seek and truncation contracts of bin/dd/dd.c, which are contracts over
# a real file, and the operand bounds as the program's own exit status
# reports them. $1 is the host build of the program.
#
# Every check runs and the script reports a total, so a reverted repair
# names how many contracts it carries rather than stopping at the first.
set -eu

dd_program=$1
# The checks run inside a temporary directory, so a relative operand is
# resolved against the caller's directory before the move.
case $dd_program in
/*) ;;
*) dd_program=$(cd "$(dirname "$dd_program")" && pwd)/$(basename "$dd_program") ;;
esac

work=$(mktemp -d "${TMPDIR:-/tmp}/discobsd-dd.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

checks=0
failures=0

ok()
{
    checks=$((checks + 1))
    if [ "$1" = "yes" ]; then
        echo "ok   $2"
    else
        failures=$((failures + 1))
        echo "FAIL $2"
    fi
}

# A run of $1 bytes of the character $2, doubling rather than appending so a
# four-kilobyte fixture costs a dozen concatenations.
fill()
{
    awk -v n="$1" -v c="$2" \
        'BEGIN { b = c; while (length(b) < n) b = b b;
                 printf "%s", substr(b, 1, n) }'
}

# The distinct byte values in a range, as space-separated hex, so a range
# that holds one value reads as that value alone.
span()
{
    od -An -v -tx1 -j "$2" -N "$3" "$1" |
        tr -s ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' '
}

size()
{
    wc -c < "$1" | tr -d ' '
}

cd "$work"
fill 1024 Y > in            # 0x59

# An of= operand still truncates the output where no seek names an offset.
fill 4096 X > out
"$dd_program" if=in of=out bs=512 2>/dev/null
[ "$(size out)" = 1024 ] && r=yes || r=no
ok "$r" "of= with no seek truncates the output to the copied length"

# seek= leaves the blocks it steps over in place and truncates at the end of
# the copy.
fill 4096 X > out
"$dd_program" if=in of=out bs=512 seek=2 2>/dev/null
[ "$(size out)" = 2048 ] && r=yes || r=no
ok "$r" "seek=2 truncates at the end of the copy"
[ "$(span out 0 1024)" = "58 " ] && r=yes || r=no
ok "$r" "seek=2 preserves the blocks it steps over"
[ "$(span out 1024 1024)" = "59 " ] && r=yes || r=no
ok "$r" "seek=2 writes the copy at the seek offset"

# conv=notrunc keeps everything beyond the copy.
fill 4096 X > out
"$dd_program" if=in of=out bs=512 seek=2 conv=notrunc 2>/dev/null
[ "$(size out)" = 4096 ] && r=yes || r=no
ok "$r" "conv=notrunc leaves the output length alone"
[ "$(span out 2048 2048)" = "58 " ] && r=yes || r=no
ok "$r" "conv=notrunc leaves the bytes beyond the copy alone"
[ "$(span out 0 1024)" = "58 " ] && r=yes || r=no
ok "$r" "conv=notrunc preserves the blocks the seek steps over"

# A seek onto an output that does not exist yet leaves a hole.
rm -f fresh
"$dd_program" if=in of=fresh bs=512 seek=1 2>/dev/null
[ "$(size fresh)" = 1536 ] && r=yes || r=no
ok "$r" "seek= onto a new output places the copy at the offset"
[ "$(span fresh 0 512)" = "00 " ] && r=yes || r=no
ok "$r" "seek= onto a new output leaves a hole ahead of the copy"

# An operand that leaves the range is refused rather than wrapped. Each of
# these reads back as an accepted value where the conversion wraps: 2^32+1
# as a block size of one, 2^32 as a count of zero, which means no limit.
if "$dd_program" if=in of=out bs=4294967297 2> err; then r=no; else r=yes; fi
ok "$r" "bs=2^32+1 is refused"
grep -F "out of range" err > /dev/null && r=yes || r=no
ok "$r" "bs=2^32+1 names the operand as out of range"

if "$dd_program" if=in of=out count=65536x65536 2>/dev/null; then
    r=no
else
    r=yes
fi
ok "$r" "count=2^32 written as a product is refused"

# skip= names a block, and 4194304 blocks of 512 bytes is 2^31, one past the
# offset range. The operand itself is in range; the product is not.
if "$dd_program" if=in of=out bs=512 skip=4194304 2> err; then r=no; else r=yes; fi
ok "$r" "skip= whose byte offset leaves the range is refused"
grep -F "out of range" err > /dev/null && r=yes || r=no
ok "$r" "skip= is refused on its own bound rather than by a failing lseek"

echo "dd_contracts filesystem: $checks checks, $failures failures"
[ "$failures" -eq 0 ]
