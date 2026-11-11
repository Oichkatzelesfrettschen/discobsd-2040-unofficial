#!/bin/sh
# Gate for usr.sbin/msdosfs: the NetBSD msdosfs core, the makefs forks and
# newfs_msdos's formatter imported under tools/netbsd-imports.manifest,
# behind the DiscoBSD fixed buffer pool and front end, built for the host
# under the address and undefined-behavior sanitizers.
#
# fat_reader.py, written from the FAT specification and sharing no code
# with the imports, judges every image the tool writes: FAT copies, chains,
# long names, dot entries, lost clusters and file contents. Known-bad
# images calibrate it first. The tool then runs a command session on
# FAT12, FAT16 and FAT32, repeats it for byte-identical images, refuses
# invalid operations, survives a corrupt image and a failed FAT write, and
# runs with the smallest buffer pool the commands need. Source mutants of
# the imported code must fail the reader. dosfstools (fsck.fat) and mtools
# cross-check the images when installed; each absent tool prints "not run".

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
: "${MAKE:?set MAKE to bmake}"

topsrc=$(cd "$(dirname "$0")/../.." && pwd)
here=$topsrc/tests/msdosfs_core
reader=$here/fat_reader.py
work=$(mktemp -d "${TMPDIR:-/tmp}/msdosfs-core.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
export SOURCE_DATE_EPOCH=1700000000

fail()
{
	echo "msdosfs_core: FAIL: $*" >&2
	exit 1
}

sanitize="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

# build TREE BINARY [DEFINE...]: the host tool from the sources under TREE
# (a copy of the imported directories and usr.sbin/msdosfs, or $topsrc),
# with the flags usr.sbin/msdosfs/Makefile itself defines.
build()
{
	tree=$1
	binary=$2
	shift 2
	dir=$tree/usr.sbin/msdosfs
	defs=$("$MAKE" -C "$dir" -V '${MSDOSFS_DEFS}')
	netbsd=$("$MAKE" -C "$dir" -V '${NETBSD_FLAGS}')
	local_flags=$("$MAKE" -C "$dir" -V '${LOCAL_FLAGS}')
	objects=
	mkdir -p "$binary.obj"
	for source in $("$MAKE" -C "$dir" -V '${HOST_NETBSD}'); do
		object=$binary.obj/$(basename "$source" .c).o
		# shellcheck disable=SC2086  # the flag variables are word lists
		${CC:-cc} -O1 -g -std=gnu17 -Wall -Wextra -Werror $sanitize \
		    $defs "$@" $netbsd -c -o "$object" "$source" ||
		    fail "build of $source"
		objects="$objects $object"
	done
	for source in msdosfs.c msdosfs_buf.c msdosfs_env.c; do
		object=$binary.obj/${source%.c}.o
		# shellcheck disable=SC2086
		${CC:-cc} -O1 -g -Wall -Wextra -Werror $sanitize $local_flags \
		    -D_DEFAULT_SOURCE $defs "$@" -c -o "$object" "$dir/$source" ||
		    fail "build of $source"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086  # $objects is a list of paths without spaces
	${CC:-cc} $sanitize -o "$binary" $objects || fail "link of $binary"
}

# copy_tree NAME: a writable copy of the sources build reads.
copy_tree()
{
	tree=$work/$1
	mkdir -p "$tree/sys/fs" "$tree/usr.sbin" "$tree/sbin" "$tree/share"
	cp -R "$topsrc/sys/fs/msdosfs" "$tree/sys/fs/"
	cp -R "$topsrc/usr.sbin/makefs" "$tree/usr.sbin/"
	cp -R "$topsrc/sbin/newfs_msdos" "$tree/sbin/"
	cp -R "$topsrc/share/mk" "$tree/share/"
	mkdir "$tree/usr.sbin/msdosfs"
	(cd "$topsrc/usr.sbin/msdosfs" &&
	    cp -R Makefile netbsd_compat.h msdosfs.c msdosfs_buf.c msdosfs_env.c \
	    compat "$tree/usr.sbin/msdosfs/")
}

# status WANT CMD...: CMD exits with WANT and writes no sanitizer report.
status()
{
	want=$1
	shift
	got=0
	"$@" >"$work/cmd.out" 2>"$work/cmd.err" || got=$?
	if [ "$got" -ne "$want" ] || grep -q 'Sanitizer' "$work/cmd.err"; then
		cat "$work/cmd.err" >&2
		fail "$* exited $got, not $want"
	fi
}

# verdict IMAGE [EXPECT]: the reader accepts IMAGE (and EXPECT's contents).
verdict()
{
	"$PYTHON" "$reader" "$@" >"$work/reader.out" ||
	    { cat "$work/reader.out" >&2; fail "reader rejected $1"; }
}

# rejects IMAGE TEXT: the reader exits 1 and reports TEXT.
rejects()
{
	got=0
	"$PYTHON" "$reader" "$1" check >"$work/reader.out" || got=$?
	[ "$got" -eq 1 ] && grep -q "^FAIL .*$2" "$work/reader.out" ||
	    { cat "$work/reader.out" >&2; fail "reader accepted $1 (wanted: $2)"; }
}

# external IMAGE: fsck.fat and mtools agree with the image, when present.
external()
{
	if [ -n "$fsck" ]; then
		"$fsck" -n "$1" >"$work/fsck.out" 2>&1 ||
		    { cat "$work/fsck.out" >&2; fail "fsck.fat rejected $1"; }
	fi
	if [ -n "$mtype" ]; then
		MTOOLS_SKIP_CHECK=1 "$mtype" -i "$1" "::/A Long File Name.bin" \
		    >"$work/mtype.out" 2>"$work/mtype.err" ||
		    { cat "$work/mtype.err" >&2; fail "mtype read of $1"; }
		cmp -s "$work/mtype.out" "$work/pat/big" ||
		    fail "mtype read different bytes from $1"
	fi
}

fsck=$(command -v fsck.fat || true)
mtype=$(command -v mtype || true)
mformat=$(command -v mformat || true)
mcopy=$(command -v mcopy || true)
[ -n "$fsck" ] || echo "msdosfs_core: fsck.fat cross-check not run: dosfstools absent"
[ -n "$mtype" ] || echo "msdosfs_core: mtools cross-check not run: mtype absent"

mkdir "$work/pat"
printf 'hello, world\n' >"$work/pat/hello"
printf 'inner\n' >"$work/pat/inner"
"$PYTHON" - "$work/pat/big" <<'PY'
import sys

# 70,000 bytes: more than one cluster at every geometry the session uses.
with open(sys.argv[1], "wb") as out:
    out.write(bytes((i * 7 + i // 251) & 0xFF for i in range(70000)))
PY

build "$topsrc" "$work/msdosfs"
tool=$work/msdosfs

# size_for FAT: a volume size whose cluster count mkfs_msdos fits to FAT.
size_for()
{
	case $1 in
	12) echo 2048 ;;
	16) echo 20000 ;;
	32) echo 140000 ;;
	esac
}

# session TOOL IMAGE FAT: the command session the reader then judges.
session()
{
	status 0 "$1" newfs "$2" "$(size_for "$3")" "$3"
	status 0 "$1" put "$2" "$work/pat/hello" /hello
	status 0 "$1" put "$2" "$work/pat/big" "/A Long File Name.bin"
	status 0 "$1" mkdir "$2" /sub
	status 0 "$1" put "$2" "$work/pat/inner" /sub/inner
	status 0 "$1" mkdir "$2" "/sub/Deeper Dir"
	status 0 "$1" put "$2" "$work/pat/big" "/sub/Deeper Dir/copy"
	# 24 long names fill more than one 512-byte directory cluster.
	n=1
	while [ "$n" -le 24 ]; do
		status 0 "$1" put "$2" "$work/pat/hello" "/sub/entry number $n.txt"
		n=$((n + 1))
	done
}

cat >"$work/expect" <<EOF
/hello=$work/pat/hello
/A Long File Name.bin=$work/pat/big
/sub/inner=$work/pat/inner
/sub/Deeper Dir/copy=$work/pat/big
/sub/entry number 24.txt=$work/pat/hello
EOF

# The reader rejects each class of damage it claims to detect.
session "$tool" "$work/good.img" 16
verdict "$work/good.img" check "$work/expect"
"$PYTHON" - "$work" <<'PY'
import struct
import sys
from pathlib import Path

work = Path(sys.argv[1])
good = (work / "good.img").read_bytes()
bps, spc, res, nfats, rde = struct.unpack_from("<HBHBH", good, 11)
spf = struct.unpack_from("<H", good, 22)[0]
fat0, fat1 = res * bps, (res + spf) * bps
root = (res + nfats * spf) * bps


def save(name, data):
    (work / name).write_bytes(bytes(data))


def entry_offset(data, short):
    for off in range(root, root + rde * 32, 32):
        if data[off : off + 11] == short:
            return off
    raise SystemExit(f"no root entry {short!r}")


img = bytearray(good)
img[fat1 + 4] ^= 0xFF
save("bad-mirror.img", img)

img = bytearray(good)
lfn = next(off for off in range(root, root + rde * 32, 32) if img[off + 11] == 0x0F)
img[lfn + 13] ^= 0x01
save("bad-checksum.img", img)

img = bytearray(good)
# Cluster 1000 is free: the session's files take about 300 clusters.
for fat in (fat0, fat1):
    if struct.unpack_from("<H", img, fat + 2 * 1000)[0] != 0:
        raise SystemExit("cluster 1000 is not free")
    struct.pack_into("<H", img, fat + 2 * 1000, 0xFFFF)
save("bad-lost.img", img)

img = bytearray(good)
hello = entry_offset(img, b"HELLO      ")
big = next(off for off in range(root, root + rde * 32, 32)
           if img[off + 11] == 0x20 and img[off : off + 5] == b"ALONG")
# /hello takes over the last cluster of the long file's chain, so each
# chain alone fits its size and only the shared cluster is wrong.
tail = struct.unpack_from("<H", img, big + 26)[0]
while struct.unpack_from("<H", img, fat0 + 2 * tail)[0] < 0xFFF8:
    tail = struct.unpack_from("<H", img, fat0 + 2 * tail)[0]
struct.pack_into("<H", img, hello + 26, tail)
save("bad-crosslink.img", img)

img = bytearray(good)
struct.pack_into("<I", img, big + 28, 1)
save("bad-size.img", img)

img = bytearray(good)
img[510] = 0
save("bad-signature.img", img)
PY
rejects "$work/bad-mirror.img" "FAT copy 1 differs"
rejects "$work/bad-checksum.img" "long name checksum"
rejects "$work/bad-lost.img" "lost clusters"
rejects "$work/bad-crosslink.img" "also belongs to"
rejects "$work/bad-size.img" "clusters for 1 bytes"
rejects "$work/bad-signature.img" "boot sector signature"
echo "msdosfs_core: reader rejects mirror, checksum, lost, cross-link, size and signature damage"

for fat in 12 16 32; do
	img=$work/fat$fat.img
	session "$tool" "$img" "$fat"
	verdict "$img" check "$work/expect"
	grep -q "^fat $fat " "$work/reader.out" || fail "FAT$fat image read as $(tail -n 1 "$work/reader.out")"
	external "$img"

	status 0 "$tool" cat "$img" "/a long file name.BIN"
	cmp -s "$work/cmd.out" "$work/pat/big" || fail "cat by folded name (FAT$fat)"
	status 0 "$tool" get "$img" "/sub/Deeper Dir/copy" "$work/copy.out"
	cmp -s "$work/copy.out" "$work/pat/big" || fail "get (FAT$fat)"
	status 0 "$tool" ls "$img"
	[ "$(awk '{ printf "%s|", substr($0, 12) }' "$work/cmd.out")" = \
	    "hello|A Long File Name.bin|sub/|" ] || fail "ls root (FAT$fat)"
	status 0 "$tool" ls "$img" sub
	[ "$(awk 'NR <= 4 { printf "%s|", substr($0, 12) }' "$work/cmd.out")" = \
	    ".|..|inner|Deeper Dir/|" ] || fail "ls sub (FAT$fat)"
	status 0 "$tool" df "$img"
	free_before=$(awk '{ print $NF }' "$work/cmd.out")

	status 1 "$tool" put "$img" "$work/pat/hello" /HELLO
	status 1 "$tool" cat "$img" /absent
	status 1 "$tool" rm "$img" /sub
	status 1 "$tool" rm "$img" /
	status 1 "$tool" ls "$img" /hello
	status 1 "$tool" cat "$img" /sub
	status 2 "$tool" frobnicate "$img"
	status 0 "$tool" df "$img"
	[ "$(awk '{ print $NF }' "$work/cmd.out")" = "$free_before" ] ||
	    fail "refused commands changed the free count (FAT$fat)"

	n=1
	while [ "$n" -le 24 ]; do
		status 0 "$tool" rm "$img" "/SUB/ENTRY NUMBER $n.TXT"
		n=$((n + 1))
	done
	status 0 "$tool" rm "$img" "/sub/Deeper Dir/copy"
	status 0 "$tool" rm "$img" "/sub/Deeper Dir"
	status 0 "$tool" rm "$img" /sub/inner
	status 0 "$tool" rm "$img" /sub
	status 0 "$tool" rm "$img" "/A Long File Name.bin"
	status 0 "$tool" rm "$img" /hello
	verdict "$img" check
	status 0 "$tool" df "$img"
	clusters=$(awk '{ print $6 }' "$work/cmd.out")
	free=$(awk '{ print $NF }' "$work/cmd.out")
	root=0
	[ "$fat" = 32 ] && root=1
	[ "$free" -eq $((clusters - root)) ] ||
	    fail "FAT$fat: $free of $clusters clusters free after removing everything"
	external_empty=$work/fat$fat-empty.img
	cp "$img" "$external_empty"
	[ -z "$fsck" ] || "$fsck" -n "$external_empty" >/dev/null 2>&1 ||
	    fail "fsck.fat rejected the emptied FAT$fat image"
	echo "msdosfs_core: FAT$fat session, refusals and removal agree with the reader"
done

# The DOS timestamps hold SOURCE_DATE_EPOCH as UTC, through the
# compat/dev/clock_subr.h conversion msdosfs_conv.c calls.
"$PYTHON" - "$here" "$work/good.img" "$SOURCE_DATE_EPOCH" <<'PY'
import struct
import sys
import time

sys.path.insert(0, sys.argv[1])
import fat_reader  # noqa: E402

data = open(sys.argv[2], "rb").read()
vol = fat_reader.parse(data)
root = (vol.reserved + vol.nfats * vol.fat_sectors) * vol.bps
for off in range(root, root + vol.root_entries * 32, 32):
    if data[off : off + 11] == b"HELLO      ":
        mtime, mdate = struct.unpack_from("<HH", data, off + 22)
        break
else:
    raise SystemExit("no HELLO entry")
t = time.gmtime(int(sys.argv[3]))
want_date = (t.tm_year - 1980) << 9 | t.tm_mon << 5 | t.tm_mday
want_time = t.tm_hour << 11 | t.tm_min << 5 | t.tm_sec // 2
if (mdate, mtime) != (want_date, want_time):
    raise SystemExit(f"timestamp {mdate:#x} {mtime:#x}, want {want_date:#x} {want_time:#x}")
PY
echo "msdosfs_core: entry timestamps encode SOURCE_DATE_EPOCH as UTC"

# Equal inputs and SOURCE_DATE_EPOCH give byte-identical images.
session "$tool" "$work/again.img" 16
cmp -s "$work/good.img" "$work/again.img" || fail "repeated session differs"
echo "msdosfs_core: repeated FAT16 session is byte-identical"

# A put that cannot fit (70,000 bytes into 85 512-byte clusters) is
# refused before any slot or cluster changes.
status 0 "$tool" newfs "$work/full.img" 120 12
status 0 "$tool" df "$work/full.img"
before=$(cat "$work/cmd.out")
cp "$work/full.img" "$work/full.ref"
status 1 "$tool" put "$work/full.img" "$work/pat/big" /big
cmp -s "$work/full.img" "$work/full.ref" || fail "refused put modified the image"
echo "msdosfs_core: oversized put refused with the image unchanged ($before)"

# get refuses its own image, under its name or a hard link, and a directory.
cp "$work/good.img" "$work/self.img"
cp "$work/self.img" "$work/self.ref"
status 1 "$tool" get "$work/self.img" /hello "$work/self.img"
ln "$work/self.img" "$work/self.link"
status 1 "$tool" get "$work/self.img" /hello "$work/self.link"
cp "$work/pat/hello" "$work/keep"
status 1 "$tool" get "$work/self.img" /sub "$work/keep"
cmp -s "$work/keep" "$work/pat/hello" || fail "get of a directory truncated its target"
cmp -s "$work/self.img" "$work/self.ref" || fail "get modified its image"
echo "msdosfs_core: get refuses its image and directories"

# A chain pointer past the last cluster: status 1, no sanitizer report.
"$PYTHON" - "$here" "$work/good.img" "$work/corrupt.img" <<'PY'
import struct
import sys

sys.path.insert(0, sys.argv[1])
import fat_reader  # noqa: E402

data = bytearray(open(sys.argv[2], "rb").read())
vol = fat_reader.parse(bytes(data))
root = (vol.reserved + vol.nfats * vol.fat_sectors) * vol.bps
starts = {n: c for n, _, _, c, _ in fat_reader.entries(data[root : root + vol.root_entries * 32], "/")}
sub = b"".join(vol.cluster(c) for c in [starts["sub"]])
deeper = {n: c for n, _, _, c, _ in fat_reader.entries(sub, "/sub")}["Deeper Dir"]
# The long file's first link and the directory's own entry become 0xFFF0,
# a reserved value no chain may hold.
for cluster in (starts["A Long File Name.bin"], deeper):
    for copy in range(vol.nfats):
        fat = (vol.reserved + copy * vol.fat_sectors) * vol.bps
        struct.pack_into("<H", data, fat + 2 * cluster, 0xFFF0)
open(sys.argv[3], "wb").write(data)
PY
status 1 "$tool" cat "$work/corrupt.img" "/A Long File Name.bin"
status 1 "$tool" ls "$work/corrupt.img" "/sub/Deeper Dir"
echo "msdosfs_core: corrupt chain rejected with status 1"

# The smallest pool the commands need, and one buffer fewer.
build "$topsrc" "$work/msdosfs-nbuf2" -DMSDOSFS_NBUF=2
session "$work/msdosfs-nbuf2" "$work/nbuf2.img" 12
status 0 "$work/msdosfs-nbuf2" rm "$work/nbuf2.img" "/sub/Deeper Dir/copy"
verdict "$work/nbuf2.img" check
build "$topsrc" "$work/msdosfs-nbuf1" -DMSDOSFS_NBUF=1
status 0 "$work/msdosfs-nbuf1" newfs "$work/nbuf1.img" 2048 12
status 0 "$work/msdosfs-nbuf1" df "$work/nbuf1.img"
status 1 "$work/msdosfs-nbuf1" put "$work/nbuf1.img" "$work/pat/hello" /hello
grep -q 'buffer pool exhausted' "$work/cmd.err" || fail "one-buffer pool did not stop"
echo "msdosfs_core: two buffers suffice; one buffer stops at exhaustion"

# A failed FAT write fails the command although updatefats drops it.
if [ "$(uname -s)" = Linux ]; then
	cat >"$work/write_wrap.c" <<'EOF'
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t __real_write(int, const void *, size_t);

/* Fail every write at byte offset MSDOSFS_TEST_FAIL_OFFSET. */
ssize_t
__wrap_write(int descriptor, const void *buffer, size_t size)
{
	const char *text = getenv("MSDOSFS_TEST_FAIL_OFFSET");

	if (text != NULL &&
	    lseek(descriptor, 0, SEEK_CUR) == (off_t)strtol(text, NULL, 10)) {
		errno = EIO;
		return -1;
	}
	return __real_write(descriptor, buffer, size);
}
EOF
	${CC:-cc} -std=c17 -Wall -Wextra -Werror -c -o "$work/write_wrap.o" \
	    "$work/write_wrap.c" || fail "write wrapper build"
	# shellcheck disable=SC2086
	${CC:-cc} $sanitize -Wl,--wrap=write -o "$work/msdosfs-wrap" \
	    "$work/msdosfs.obj/"*.o "$work/write_wrap.o" || fail "write wrapper link"
	status 0 "$tool" newfs "$work/wfail.img" 2048 12
	# The second FAT copy begins at sector 1 + sectors per FAT.
	spf=$("$PYTHON" -c 'import struct,sys; print(struct.unpack_from("<H", open(sys.argv[1],"rb").read(), 22)[0])' "$work/wfail.img")
	status 1 env MSDOSFS_TEST_FAIL_OFFSET=$(((1 + spf) * 512)) \
	    "$work/msdosfs-wrap" put "$work/wfail.img" "$work/pat/hello" /hello
	grep -q 'Input/output error' "$work/cmd.err" || fail "FAT mirror write error text"

	copy_tree drop-write
	"$PYTHON" - "$work/drop-write/usr.sbin/msdosfs/msdosfs.c" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = "(error = buf_write_error()) == 0 && "
if text.count(old) != 1:
    raise SystemExit("write-error mutation anchor mismatch")
path.write_text(text.replace(old, ""), encoding="utf-8")
PY
	build "$work/drop-write" "$work/drop-write.bin"
	# shellcheck disable=SC2086
	${CC:-cc} $sanitize -Wl,--wrap=write -o "$work/drop-write-wrap" \
	    "$work/drop-write.bin.obj/"*.o "$work/write_wrap.o" ||
	    fail "dropped-write mutant link"
	status 0 "$tool" newfs "$work/wfail2.img" 2048 12
	status 0 env MSDOSFS_TEST_FAIL_OFFSET=$(((1 + spf) * 512)) \
	    "$work/drop-write-wrap" put "$work/wfail2.img" "$work/pat/hello" /hello
	echo "msdosfs_core: failed FAT mirror write exits 1; without buf_write_error it exits 0"
else
	echo "msdosfs_core: write-failure injection not run: needs GNU ld --wrap on Linux"
fi

# mutant NAME FILE OLD NEW: a tree with FILE's one OLD replaced by NEW,
# built as $work/NAME.bin.
mutant()
{
	copy_tree "$1"
	"$PYTHON" - "$work/$1/$2" "$3" "$4" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = sys.argv[2].replace("\\n", "\n").replace("\\t", "\t")
new = sys.argv[3].replace("\\n", "\n").replace("\\t", "\t")
if text.count(old) != 1:
    raise SystemExit(f"mutation anchor count {text.count(old)}: {old!r}")
path.write_text(text.replace(old, new), encoding="utf-8")
PY
	build "$work/$1" "$work/$1.bin"
}

# The FAT mirror is never written.
mutant no-mirror sys/fs/msdosfs/msdosfs_fat.c \
    'for (i = 1; i < pmp->pm_FATs; i++) {' 'for (i = 1; i < 1; i++) {'
session "$work/no-mirror.bin" "$work/mutant.img" 16
rejects "$work/mutant.img" "FAT copy 1 differs"
echo "msdosfs_core: unmirrored FAT mutant rejected"

# Long-name entries carry a checksum one off.
mutant lfn-checksum sys/fs/msdosfs/msdosfs_conv.c \
    'wep->weChksum = chksum;' 'wep->weChksum = chksum + 1;'
status 0 "$work/lfn-checksum.bin" newfs "$work/mutant.img" 20000 16
status 0 "$work/lfn-checksum.bin" put "$work/mutant.img" "$work/pat/big" "/A Long File Name.bin"
rejects "$work/mutant.img" "long name checksum"
echo "msdosfs_core: long-name checksum mutant rejected"

# Freed FAT16 clusters keep their links.
mutant leak-chain sys/fs/msdosfs/msdosfs_fat.c \
    'putushort((char *)bp->b_data + bo, MSDOSFSFREE);' \
    'putushort((char *)bp->b_data + bo, cluster);'
session "$work/leak-chain.bin" "$work/mutant.img" 16
status 0 "$work/leak-chain.bin" rm "$work/mutant.img" "/A Long File Name.bin"
rejects "$work/mutant.img" "lost clusters"
echo "msdosfs_core: unfreed cluster chain mutant rejected"

# removede leaves the short entry live over freed clusters.
mutant keep-entry sys/fs/msdosfs/msdosfs_lookup.c \
    'ep--->deName[0] = SLOT_DELETED;' 'ep--;'
session "$work/keep-entry.bin" "$work/mutant.img" 16
status 0 "$work/keep-entry.bin" rm "$work/mutant.img" /hello
rejects "$work/mutant.img" "/hello"
echo "msdosfs_core: undeleted directory entry mutant rejected"

# A directory whose cluster chain breaks reads as an error: without the
# msdosfs_denode.patch, deget prints and returns the directory as empty.
copy_tree no-denode-patch
(cd "$work/no-denode-patch" &&
    git apply --reverse "$topsrc/tools/netbsd-patches/msdosfs_denode.patch") ||
    fail "reverse of msdosfs_denode.patch"
build "$work/no-denode-patch" "$work/no-denode-patch.bin"
status 0 "$work/no-denode-patch.bin" ls "$work/corrupt.img" "/sub/Deeper Dir"
echo "msdosfs_core: core without msdosfs_denode.patch lists a broken directory as empty"

# Without mkfs_msdos.patch the formatter leaks its sector buffer, which
# LeakSanitizer reports at exit. Apple's clang ships the address sanitizer
# without its leak detector, so the control has an oracle on Linux alone.
if [ "$(uname -s)" = Linux ]; then
	copy_tree no-mkfs-patch
	(cd "$work/no-mkfs-patch" &&
	    git apply --reverse "$topsrc/tools/netbsd-patches/mkfs_msdos.patch") ||
	    fail "reverse of mkfs_msdos.patch"
	build "$work/no-mkfs-patch" "$work/no-mkfs-patch.bin"
	got=0
	"$work/no-mkfs-patch.bin" newfs "$work/leak.img" 2048 12 >/dev/null \
	    2>"$work/leak.err" || got=$?
	[ "$got" -ne 0 ] && grep -q 'LeakSanitizer' "$work/leak.err" ||
	    { cat "$work/leak.err" >&2; fail "formatter without mkfs_msdos.patch did not leak"; }
	echo "msdosfs_core: formatter without mkfs_msdos.patch leaks its sector buffer"
else
	echo "msdosfs_core: formatter leak control skipped on $(uname -s)"
fi

# DiscoBSD has no mmap(2): msdosfs_wfile without msdosfs_vnops.patch does
# not compile for the target.
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
	copy_tree no-vnops-patch
	(cd "$work/no-vnops-patch" &&
	    git apply --reverse "$topsrc/tools/netbsd-patches/msdosfs_vnops.patch") ||
	    fail "reverse of msdosfs_vnops.patch"
	dir=$work/no-vnops-patch/usr.sbin/msdosfs
	defs=$("$MAKE" -C "$dir" -V '${MSDOSFS_DEFS}')
	got=0
	# shellcheck disable=SC2086
	arm-none-eabi-gcc -std=gnu17 -mcpu=cortex-m0plus -mthumb -fsyntax-only \
	    -nostdinc -I"$topsrc/include" -I"$dir/compat/discobsd" $defs \
	    "$work/no-vnops-patch/usr.sbin/makefs/msdos/msdosfs_vnops.c" \
	    >"$work/mman.log" 2>&1 || got=$?
	[ "$got" -ne 0 ] && grep -q 'sys/mman.h' "$work/mman.log" ||
	    { cat "$work/mman.log" >&2; fail "unpatched msdosfs_wfile compiled for DiscoBSD"; }
	echo "msdosfs_core: msdosfs_wfile without its patch needs the absent <sys/mman.h>"
else
	echo "msdosfs_core: target mmap check not run: arm-none-eabi-gcc absent"
fi

# An image mtools formats and fills, read back through the tool.
if [ -n "$mformat" ] && [ -n "$mcopy" ]; then
	"$PYTHON" -c 'import sys; open(sys.argv[1], "wb").truncate(20000 * 512)' \
	    "$work/mtools.img"
	# mformat picks FAT16 for 20,000 sectors, as the cluster count requires.
	MTOOLS_SKIP_CHECK=1 "$mformat" -i "$work/mtools.img" :: || fail "mformat"
	MTOOLS_SKIP_CHECK=1 "$mcopy" -i "$work/mtools.img" "$work/pat/big" \
	    "::/Written By Mtools.bin" || fail "mcopy"
	verdict "$work/mtools.img" check
	status 0 "$tool" cat "$work/mtools.img" "/written by mtools.bin"
	cmp -s "$work/cmd.out" "$work/pat/big" || fail "tool read of an mtools image"
	echo "msdosfs_core: mtools image read back through the tool"
else
	echo "msdosfs_core: mtools image read not run: mformat or mcopy absent"
fi

echo "msdosfs_core: PASS"
