#!/bin/sh
#
# Round-trip tar(1) through both header formats it writes and cross-check
# the ustar archive against the host tar, so the format is interoperable
# rather than merely self-consistent.
#
# The tool under test is a host build of bin/tar/tar.c: the rp2040 a.out
# runs only on the board, and the format code is the same translation unit.
# Pass a different binary as the first argument to test one that exists.
#
set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tartest.XXXXXX")
trap 'rm -rf "$work"' EXIT INT HUP TERM

fail() {
	echo "tartest: FAIL: $*" >&2
	exit 1
}

#
# The LZW filter -z and -Z fork. On the board it is /usr/bin/compress, the
# tree's own usr.bin/compress; here it is whatever host program answers to
# the name, which reads and writes the same format.
#
ZPROG=$(command -v compress 2>/dev/null || true)

#
# The tool under test.
#
if [ $# -ge 1 ]; then
	TAR=$1
else
	TAR=$work/tar
	${CC:-cc} -std=gnu17 -O1 -w \
	    ${ZPROG:+-DCOMPRESS=\"$ZPROG\"} \
	    -o "$TAR" "$srcdir/tar.c" ||
	    fail "host build of tar.c"
fi
echo "tartest: tool under test: $TAR"

#
# The host tar that proves the ustar archive is readable elsewhere. GNU tar
# and bsdtar both answer to this name; either one settles interoperability.
#
HOSTTAR=${HOSTTAR:-tar}
command -v "$HOSTTAR" >/dev/null 2>&1 || fail "no host tar named $HOSTTAR"
echo "tartest: host tar: $("$HOSTTAR" --version 2>&1 | head -1)"

#
# GNU tar writes its own format by default: magic "ustar  " rather than the
# POSIX "ustar\0", a LongName entry of typeflag 'L' rather than the prefix
# field, and time fields where ustar puts prefix. Ask for ustar explicitly
# when the host tar writes the archive. bsdtar spells the option the same.
#
"$HOSTTAR" --format=ustar --version >/dev/null 2>&1 &&
    HOSTFMT=--format=ustar || HOSTFMT=

#
# A tree exercising every case the header fields have to carry: a path
# needing the ustar prefix field, an empty file, a file whose size is not a
# multiple of TBLOCK, a symbolic link, and a hard link.
#
# The long path is nested directories rather than one long basename: ustar
# splits a path at a slash, so a single component over 99 bytes is
# unrepresentable in the format and a deep path is the case that works.
#
deep=d1234567890123456789012345678901234567890/d2234567890123456789012345678901234567890
deep=$deep/d3234567890123456789012345678901234567890/d4234567890123456789012345678901234567890
deep=$deep/d5234567890123456789012345678901234567890

mkshort() {
	root=$1
	mkdir -p "$root/sub"
	echo 'the quick brown fox' > "$root/plain"
	: > "$root/empty"
	printf 'abcde' > "$root/odd"		# 5 bytes, not a block multiple
	printf '%0999d' 7 > "$root/odd2"	# 999 bytes, spans two blocks
	ln -s plain "$root/symlink"
	ln "$root/plain" "$root/sub/hardlink"
	chmod 0751 "$root/plain"
}

cd "$work"

#
# ustar: the full tree, long path included.
#
mkdir -p ustar
(cd ustar && mkshort tree && mkdir -p "tree/$deep" &&
    echo 'buried' > "tree/$deep/leaf.txt")
(cd ustar && "$TAR" cf ../u.tar tree)

#
# Two runs over an unchanged tree must produce the same bytes. The block a
# short file's data ends in is written whole, so anything left unset in it
# is heap contents, which differ run to run and leak into the archive.
#
echo "tartest: archive bytes are reproducible"
(cd ustar && "$TAR" cf ../u2.tar tree)
cmp -s u.tar u2.tar || fail "two runs over one tree wrote different bytes"
rm -f u2.tar

echo "tartest: ustar magic check"
dd if=u.tar bs=1 skip=257 count=5 2>/dev/null | grep -q '^ustar$' ||
    fail "no ustar magic at offset 257"

echo "tartest: ustar list"
"$TAR" tf u.tar > u.list
grep -q "leaf.txt" u.list || fail "long path missing from the listing"

echo "tartest: ustar extract by the tool under test"
mkdir -p x-self
(cd x-self && "$TAR" xf ../u.tar)
diff -r ustar/tree x-self/tree || fail "ustar self round trip differs"

echo "tartest: ustar listed by the host tar"
"$HOSTTAR" tf u.tar > u.hostlist 2>u.hosterr ||
    fail "host tar rejected the ustar archive: $(cat u.hosterr)"
[ ! -s u.hosterr ] || fail "host tar warned on the archive: $(cat u.hosterr)"
grep -q "leaf.txt" u.hostlist ||
    fail "host tar did not see the prefix-split path"

echo "tartest: ustar extracted by the host tar"
mkdir -p x-host
(cd x-host && "$HOSTTAR" xf ../u.tar)
diff -r ustar/tree x-host/tree || fail "host tar extract differs"

echo "tartest: archive written by the host tar read by the tool under test"
(cd ustar && "$HOSTTAR" ${HOSTFMT:+"$HOSTFMT"} -cf ../h.tar tree)
mkdir -p x-back
(cd x-back && "$TAR" xf ../h.tar)
diff -r ustar/tree x-back/tree || fail "reading the host tar archive differs"

#
# v7: the short tree only. The v7 header has no prefix field, so a path
# over 99 bytes has no representation at all and the tool refuses it; that
# refusal is asserted rather than archived.
#
mkdir -p v7
(cd v7 && mkshort tree)
(cd v7 && "$TAR" cOf ../v.tar tree)

echo "tartest: v7 header carries no magic"
dd if=v.tar bs=1 skip=257 count=5 2>/dev/null | grep -q '^ustar$' &&
    fail "v7 archive carries the ustar magic"

echo "tartest: v7 extract by the tool under test"
mkdir -p x-v7
(cd x-v7 && "$TAR" xf ../v.tar)
diff -r v7/tree x-v7/tree || fail "v7 round trip differs"

echo "tartest: v7 archive listed by the host tar"
"$HOSTTAR" tf v.tar > v.hostlist || fail "host tar rejected the v7 archive"
grep -q "tree/plain" v.hostlist || fail "host tar did not see tree/plain"

echo "tartest: v7 refuses a path the header cannot hold"
(cd ustar && "$TAR" cOf ../vlong.tar tree) > vlong.out 2>&1 || true
grep -q "file name too long" vlong.out ||
    fail "v7 accepted a path over 99 bytes"

#
# ustar splits a path at a slash, so a single component over NAMSIZ-1 bytes
# has no representation whatever the prefix field holds. Refusing it is the
# answer; writing an entry with an empty name field is not, because
# endtape() reads an empty name as the end of the archive and every entry
# after it is lost. The file after the refused one proves which happened.
#
echo "tartest: a component over the name field is refused, not truncated"
long=llllllllllmmmmmmmmmmnnnnnnnnnnoooooooooopppppppppp
long=$long$long$long
mkdir -p toolong/tree/"$long"
echo 'buried' > toolong/tree/"$long"/leaf
echo 'after' > toolong/tree/zzz-after
(cd toolong && "$TAR" cf ../t.tar tree) > t.out 2>&1 || true
grep -q "file name too long" t.out ||
    fail "a 150-byte path component was not refused"
"$TAR" tf t.tar > t.list
grep -q "tree/zzz-after" t.list ||
    fail "entries after the refused name were lost"
"$HOSTTAR" tf t.tar > /dev/null 2>&1 ||
    fail "host tar rejected the archive around the refused name"

echo "tartest: both formats read without a format flag"
"$TAR" tf u.tar > /dev/null || fail "reading ustar needs a flag"
"$TAR" tf v.tar > /dev/null || fail "reading v7 needs a flag"

#
# -z and -Z pipe the archive through the LZW filter as a separate process
# rather than linking the codec in, so a truncated result means tar exited
# before the child drained; the diff is what catches that.
#
if [ -n "$ZPROG" ] && [ -z "${1:-}" ]; then
	echo "tartest: LZW filter through $ZPROG"
	(cd ustar && "$TAR" cZf ../z.tar tree)
	#
	# The decompressed stream is a plain archive the host tar reads.
	# It is not byte-identical to u.tar: getbuf() takes the blocking
	# factor from the archive fd's st_blksize, which differs between a
	# pipe and a regular file, so the trailing pad differs in length.
	#
	"$ZPROG" -d < z.tar > z.plain
	mkdir -p x-zplain
	(cd x-zplain && "$HOSTTAR" xf ../z.plain)
	diff -r ustar/tree x-zplain/tree ||
	    fail "the decompressed -Z archive differs"
	[ "$(wc -c < z.tar)" -lt "$(wc -c < u.tar)" ] ||
	    fail "-Z did not compress the archive"
	mkdir -p x-z
	(cd x-z && "$TAR" xZf ../z.tar)
	diff -r ustar/tree x-z/tree || fail "-Z round trip differs"

	(cd ustar && "$TAR" czf ../z2.tar tree)
	cmp -s z2.tar z.tar || fail "-z and -Z wrote different archives"
	rm -f z2.tar
	echo "tartest: -Z archive is $(wc -c < z.tar) bytes against \
$(wc -c < u.tar) uncompressed"
else
	echo "tartest: SKIP the LZW filter: no compress on this host"
fi

echo "tartest: PASS"
