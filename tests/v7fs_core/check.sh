#!/bin/sh
# Build the NetBSD v7fs core imported under sys/fs/v7fs, with NetBSD's own
# userland I/O backend and newfs library, against v7fs_core_test.c under the
# address and undefined-behavior sanitizers; run it in the PDP-11 and the
# little-endian byte order; require that three mutants of the imported
# sources are rejected with the assertion status 1; then build usr.sbin/v7fs
# for the host with the same sanitizers, drive every command in both byte
# orders, and check the image it writes with the same core read-back and
# independent reader.
#
# Two warnings stay disabled for the imported files, which the tree keeps
# byte-identical to NetBSD (tools/netbsd-imports.manifest):
#   -Wno-address-of-packed-member  v7fs_freeinode_update() takes a pointer to
#       the packed superblock's freeinode cache; v7fs_core_test.c asserts
#       that the cache is aligned for v7fs_ino_t, and this script repeats the
#       assertion for the ARM target when arm-none-eabi-gcc is present.
#   -Wno-unused-parameter  scratch_remain() in v7fs_io.c uses its argument
#       only under STATIC_BUFFER.
# The imported files also compile without the alignment sanitizer.
# v7fs_superblock.c (the free-list load) and sbin/newfs_v7fs/main.c (the
# free-list shift) pass pointers to the packed structs' daddr arrays, at
# offset 2, to memcpy and memmove. Forming such a pointer is undefined in ISO
# C (C11 6.3.2.3p7), and clang's -fsanitize=alignment reports it; no access
# through it is unaligned, because arm-none-eabi-gcc and clang for thumbv6m
# both lower these copies to library calls and lower direct packed-member
# stores to byte stores. The test program keeps every sanitizer.
# The test program itself compiles with every warning enabled.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../.." && pwd)
here=$topsrc/tests/v7fs_core
compat=$topsrc/usr.sbin/v7fs
v7fs=$topsrc/sys/fs/v7fs
newfs=$topsrc/sbin/newfs_v7fs
work=$(mktemp -d "${TMPDIR:-/tmp}/v7fs-core.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
	echo "v7fs_core: FAIL: $*" >&2
	exit 1
}

core="endian superblock superblock_util inode inode_util datablock dirent io
file file_util io_user"

# build SOURCE_DIR BINARY: the oracle linked against the v7fs sources in
# SOURCE_DIR/sys/fs/v7fs and SOURCE_DIR/sbin/newfs_v7fs.
build()
{
	objects=
	mkdir -p "$2.obj"
	for source in $(for f in $core; do echo "$1/sys/fs/v7fs/v7fs_$f.c"; done) \
	    "$1/sbin/newfs_v7fs/main.c"; do
		object=$2.obj/$(basename "$source" .c).o
		compile -Wno-address-of-packed-member -Wno-unused-parameter \
		    -fno-sanitize=alignment -I"$1/sys/fs/v7fs" \
		    -I"$1/sbin/newfs_v7fs" -c -o "$object" "$source" ||
		    fail "sanitizer build of $source"
		objects="$objects $object"
	done
	compile -I"$1/sys/fs/v7fs" -I"$1/sbin/newfs_v7fs" -c \
	    -o "$2.obj/v7fs_core_test.o" "$here/v7fs_core_test.c" ||
	    fail "sanitizer build of v7fs_core_test.c"
	# shellcheck disable=SC2086  # $objects is a list of paths without spaces.
	${CC:-cc} -fsanitize=address,undefined -o "$2" \
	    "$2.obj/v7fs_core_test.o" $objects || fail "link of $2"
}

# compile ARG...: one translation unit with the sanitizers and full warnings.
compile()
{
	${CC:-cc} -std=gnu17 -O1 -g -Wall -Wextra -Werror \
	    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	    -DV7FS_EI -include "$compat/netbsd_compat.h" -I"$compat/compat" "$@"
}

# mutant NAME FILE OLD NEW: a copy of the imported sources with FILE's one
# OLD replaced by NEW, built as $work/NAME.
mutant()
{
	tree=$work/$1
	mkdir -p "$tree/sys/fs" "$tree/sbin"
	cp -R "$v7fs" "$tree/sys/fs/"
	cp -R "$newfs" "$tree/sbin/"
	"$PYTHON" - "$tree/$2" "$3" "$4" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
old, new = sys.argv[2].replace("\\n", "\n").replace("\\t", "\t"), sys.argv[3]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count {source.count(old)}: {old!r}")
path.write_text(source.replace(old, new.replace("\\n", "\n").replace("\\t", "\t")),
                encoding="utf-8")
PY
	build "$tree" "$work/$1.bin"
}

# rejects BINARY ORDER LABEL...: the run exits 1, the assertion status, and
# reports a failure for every LABEL. A sanitizer report or another status is
# a crash, which does not count as calibration.
rejects()
{
	binary=$1
	order=$2
	shift 2
	status=0
	"$binary" "$work/mutant.img" "$order" >"$work/mutant.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ]; then
		cat "$work/mutant.log" >&2
		fail "$binary ($order) exited $status, not the assertion status 1"
	fi
	for label in "$@"; do
		if ! grep -q "^FAIL $label:" "$work/mutant.log"; then
			cat "$work/mutant.log" >&2
			fail "$binary ($order) did not report $label"
		fi
	done
	echo "v7fs_core: mutant $(basename "$binary" .bin) ($order) rejected: $*"
}

build "$topsrc" "$work/v7fs_core"
for order in pdp little; do
	"$work/v7fs_core" "$work/$order.img" "$order" || fail "$order run"
done

# The local patches: their regression scenarios hold, and the core with
# each patch reversed fails its scenario by name with the assertion status.
"$work/v7fs_core" "$work/regress.img" little regress >"$work/regress.log" ||
    { cat "$work/regress.log" >&2; fail "regression scenarios"; }

# unpatched NAME PATCH LABEL...: the imported sources with PATCH reversed,
# built as $work/NAME, must fail every LABEL in regress mode.
unpatched()
{
	name=$1
	patch=$topsrc/tools/netbsd-patches/$2
	shift 2
	tree=$work/$name
	mkdir -p "$tree/sys/fs" "$tree/sbin"
	cp -R "$v7fs" "$tree/sys/fs/"
	cp -R "$newfs" "$tree/sbin/"
	(cd "$tree" && git apply --reverse "$patch") || fail "reverse of $patch"
	build "$tree" "$work/$name.bin"
	got=0
	"$work/$name.bin" "$work/mutant.img" little regress >"$work/mutant.log" \
	    2>&1 || got=$?
	if [ "$got" -ne 1 ]; then
		cat "$work/mutant.log" >&2
		fail "$name exited $got, not the assertion status 1"
	fi
	for label in "$@"; do
		if ! grep -q "^FAIL $label:" "$work/mutant.log"; then
			cat "$work/mutant.log" >&2
			fail "$name did not report $label"
		fi
	done
	echo "v7fs_core: core without $(basename "$patch") fails: $*"
}
unpatched no-newfs-patch newfs_v7fs-main.patch regress-ilist
unpatched no-inode-patch v7fs_inode.patch regress-last-inode regress-inode-bound
unpatched no-superblock-patch v7fs_superblock.patch regress-superblock

# The ARM target lays out struct v7fs_self with four-byte pointers; repeat
# the freeinode alignment assertion there when the cross compiler exists.
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
	cat >"$work/align.c" <<'EOF'
#include <stddef.h>
#include <stdint.h>
#include "v7fs.h"
#include "v7fs_impl.h"
_Static_assert((offsetof(struct v7fs_self, superblock) +
    offsetof(struct v7fs_superblock, freeinode)) % _Alignof(v7fs_ino_t) == 0,
    "superblock freeinode cache is misaligned for v7fs_ino_t");
EOF
	arm-none-eabi-gcc -std=gnu17 -mcpu=cortex-m0plus -mthumb -fsyntax-only \
	    -Wall -Wextra -Werror -DV7FS_EI \
	    -include "$compat/netbsd_compat.h" -I"$compat/compat" -I"$v7fs" \
	    "$work/align.c" || fail "freeinode alignment on cortex-m0plus"
	echo "v7fs_core: freeinode cache aligned on cortex-m0plus"
else
	echo "v7fs_core: cortex-m0plus alignment not run: arm-none-eabi-gcc absent"
fi

# A consistent but wrong 24-bit address order: the core reads back its own
# writes, so only the independent reader can reject it.
mutant address-order sys/fs/v7fs/v7fs_endian.c \
    'case LITTLE_ENDIAN:\n\t\tops->conv32 = val32_normal_order;\n\t\tops->conv16 = val16_normal_order;\n\t\tops->conv24read = val24_normal_order_read;\n\t\tops->conv24write = val24_normal_order_write;' \
    'case LITTLE_ENDIAN:\n\t\tops->conv32 = val32_normal_order;\n\t\tops->conv16 = val16_normal_order;\n\t\tops->conv24read = val24_reverse_order_read;\n\t\tops->conv24write = val24_reverse_order_write;'
rejects "$work/address-order.bin" little root
grep -q '^FAIL core-' "$work/mutant.log" &&
    fail "address-order mutant failed the core check; it must pass it"

# Directory entries stored one character short.
mutant dirent-name sys/fs/v7fs/v7fs_file.c \
    'memcpy((char *)dir[n].name, filename, V7FS_NAME_MAX);' \
    'memcpy((char *)dir[n].name, filename + 1, V7FS_NAME_MAX - 1);'
rejects "$work/dirent-name.bin" pdp core-hello reader-hello

# newfs records one block fewer per free-list block than it lists.
mutant freelist-count sbin/newfs_v7fs/main.c \
    'fb->nfreeblock = (*val16)(k);' \
    'fb->nfreeblock = (*val16)(k - 1);'
rejects "$work/freelist-count.bin" pdp freelist accounting

# usr.sbin/v7fs: the tool's host build, its commands, and its image.
"${MAKE:?set MAKE to bmake}" -C "$topsrc/usr.sbin/v7fs" HOSTCC="${CC:-cc}" \
    HOSTCFLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined" \
    HOST_NETBSD_CFLAGS=-fno-sanitize=alignment host >"$work/tool-build.log" 2>&1 ||
    { cat "$work/tool-build.log" >&2; fail "usr.sbin/v7fs host build"; }
tool=$topsrc/usr.sbin/v7fs/v7fs-host
mkdir "$work/pat"
"$work/v7fs_core" "$work/pat" - patterns || fail "pattern files"

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

for order in pdp little; do
	flag=
	[ "$order" = pdp ] && flag=-p
	img=$work/tool-$order.img
	# shellcheck disable=SC2086  # $flag is empty or one word.
	{
		status 0 "$tool" $flag newfs "$img" 2048 64
		status 0 "$tool" $flag put "$img" "$work/pat/hello" /hello
		status 0 "$tool" $flag put "$img" "$work/pat/big" big
		status 0 "$tool" $flag mkdir "$img" /sub
		status 0 "$tool" $flag put "$img" "$work/pat/inner" /sub/inner
		"$work/v7fs_core" "$img" "$order" verify >"$work/verify.log" ||
		    { cat "$work/verify.log" >&2; fail "tool image ($order)"; }

		status 0 "$tool" $flag get "$img" /big "$work/big.out"
		cmp -s "$work/big.out" "$work/pat/big" || fail "get big ($order)"
		status 0 "$tool" $flag cat "$img" sub/inner
		cmp -s "$work/cmd.out" "$work/pat/inner" || fail "cat inner ($order)"
		status 0 "$tool" $flag ls "$img"
		[ "$(awk '{ printf "%s ", $NF }' "$work/cmd.out")" = \
		    ". .. hello big sub/ " ] || fail "ls names ($order)"
		status 0 "$tool" $flag df "$img"
		[ "$(cat "$work/cmd.out")" = \
		    "blocks 2048 data 2038 free 1953 inodes 64 free 58" ] ||
		    fail "df before removal ($order)"

		status 1 "$tool" $flag rm "$img" /sub
		status 1 "$tool" $flag put "$img" "$work/pat/hello" hello
		status 1 "$tool" $flag cat "$img" /absent
		status 1 "$tool" $flag put "$img" "$work/pat/hello" /fifteen-bytes-x
		status 1 "$tool" $flag ls "$img" /hello
		status 2 "$tool" $flag frobnicate "$img"

		status 0 "$tool" $flag rm "$img" sub/inner
		status 0 "$tool" $flag rm "$img" sub
		status 0 "$tool" $flag df "$img"
		[ "$(cat "$work/cmd.out")" = \
		    "blocks 2048 data 2038 free 1956 inodes 64 free 60" ] ||
		    fail "df after removal ($order)"
	}
	echo "v7fs_core: usr.sbin/v7fs ($order) commands and image agree"
done

# A block pointer past the end of the volume: the tool reports an error,
# exits 1 and leaves the image size unchanged.
cp "$work/tool-little.img" "$work/corrupt.img"
"$PYTHON" - "$work/corrupt.img" <<'PY'
import sys

# Inode 4 is /big; its first 24-bit address sits 12 bytes into the inode.
offset = 2 * 512 + (4 - 1) * 64 + 12
with open(sys.argv[1], "r+b") as image:
    image.seek(offset)
    image.write(b"\xff\xff\xff")
PY
size_before=$(wc -c <"$work/corrupt.img")
status 1 "$tool" cat "$work/corrupt.img" /big
[ "$(wc -c <"$work/corrupt.img")" -eq "$size_before" ] ||
    fail "corrupt image changed size"
echo "v7fs_core: corrupt block pointer rejected with status 1"

# newfs refuses a geometry it cannot format before creating the target: a
# volume below the 128 sectors a mount accepts, and an inode list that
# leaves no room for the free-block cache.
status 1 "$tool" newfs "$work/small.img" 100
status 1 "$tool" newfs "$work/crowded.img" 2048 65535
[ ! -e "$work/small.img" ] && [ ! -e "$work/crowded.img" ] ||
    fail "newfs created a refused image"

# A put that does not fit is refused before any block is allocated: the
# free counts stay exact and no partial file remains. The 128-sector image
# has 120 free data blocks; big takes 80, so a second copy cannot fit.
status 0 "$tool" newfs "$work/full.img" 128
status 0 "$tool" put "$work/full.img" "$work/pat/big" /a
status 0 "$tool" df "$work/full.img"
before=$(cat "$work/cmd.out")
status 1 "$tool" put "$work/full.img" "$work/pat/big" /b
status 0 "$tool" df "$work/full.img"
[ "$(cat "$work/cmd.out")" = "$before" ] || fail "refused put changed free counts"
status 1 "$tool" cat "$work/full.img" /b
echo "v7fs_core: oversized put refused with exact free counts"

# get truncates nothing it should not: a directory source, the image as
# its own destination, and a hard link to the image are refused, and each
# destination keeps its bytes.
cp "$work/pat/hello" "$work/keep"
status 1 "$tool" get "$work/tool-little.img" / "$work/keep"
cmp -s "$work/keep" "$work/pat/hello" || fail "get of a directory truncated"
cp "$work/tool-little.img" "$work/self.img"
cp "$work/self.img" "$work/self.ref"
status 1 "$tool" get "$work/self.img" /hello "$work/self.img"
cmp -s "$work/self.img" "$work/self.ref" || fail "get overwrote its image"
ln "$work/self.img" "$work/self.link"
status 1 "$tool" get "$work/self.img" /hello "$work/self.link"
cmp -s "$work/self.img" "$work/self.ref" || fail "get overwrote its image by a link"
echo "v7fs_core: get refuses a directory, its image and a link to it"

echo "v7fs_core: PASS (pdp and little; 3 mutants rejected; usr.sbin/v7fs)"
