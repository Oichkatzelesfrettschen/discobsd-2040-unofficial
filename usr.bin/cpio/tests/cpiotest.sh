#!/bin/sh
#
# Round-trip cpio(1) through the odc format and cross-check both directions
# against the host cpio, so the format is interoperable rather than merely
# self-consistent.
#
# The tool under test is a host build of usr.bin/cpio/cpio.c: the rp2040
# a.out runs only on the board, and the format code is the same translation
# unit. Pass a different binary as the first argument to test one that exists.
#
set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cpiotest.XXXXXX")
trap 'rm -rf "$work"' EXIT INT HUP TERM

fail() {
	echo "cpiotest: FAIL: $*" >&2
	exit 1
}

if [ $# -ge 1 ]; then
	CPIO=$1
else
	CPIO=$work/cpio
	${CC:-cc} -std=gnu17 -O1 -w -o "$CPIO" "$srcdir/cpio.c" ||
	    fail "host build of cpio.c"
fi
echo "cpiotest: tool under test: $CPIO"

# The reference is GNU cpio. Homebrew's formula is keg-only because macOS
# ships a cpio of its own (bsdcpio, which has no --no-absolute-filenames),
# so on that host the keg's binary is the reference when HOSTCPIO is unset.
if [ -z "${HOSTCPIO:-}" ] && command -v brew >/dev/null 2>&1 &&
    [ -x "$(brew --prefix cpio 2>/dev/null)/bin/cpio" ]; then
	HOSTCPIO=$(brew --prefix cpio)/bin/cpio
fi
HOSTCPIO=${HOSTCPIO:-cpio}
command -v "$HOSTCPIO" >/dev/null 2>&1 || fail "no host cpio named $HOSTCPIO"

cd "$work"
mkdir -p tree/sub
echo 'the quick brown fox' > tree/plain
: > tree/empty
printf 'abcde' > tree/odd			# not a block multiple
printf '%0999d' 7 > tree/odd2
chmod 0751 tree/plain

echo "cpiotest: write odc"
find tree | "$CPIO" -o > a.odc
head -c 6 a.odc | grep -q '^070707$' || fail "no odc magic"

echo "cpiotest: list"
"$CPIO" -it < a.odc > a.list
grep -q '^tree/odd2$' a.list || fail "tree/odd2 missing from the listing"

echo "cpiotest: extract by the tool under test"
mkdir -p x-self
(cd x-self && "$CPIO" -id < ../a.odc)
diff -r tree x-self/tree || fail "odc self round trip differs"

echo "cpiotest: archive read by the host cpio"
mkdir -p x-host
(cd x-host && "$HOSTCPIO" -id --no-absolute-filenames < ../a.odc) 2>/dev/null ||
    fail "host cpio rejected the archive"
diff -r tree x-host/tree || fail "host cpio extract differs"

echo "cpiotest: archive written by the host cpio read by the tool under test"
find tree | "$HOSTCPIO" -o -H odc > h.odc 2>/dev/null
mkdir -p x-back
(cd x-back && "$CPIO" -id < ../h.odc)
diff -r tree x-back/tree || fail "reading the host cpio archive differs"

echo "cpiotest: PASS"
