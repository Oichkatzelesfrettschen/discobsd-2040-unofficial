#!/bin/sh
# Gate for the loader composite: tools/picocalc/mkloaderuf2.py (the builder)
# and sys/arch/rp2040/dev/fat32tail.c (the locator the kernel will call to
# find the root image in the tail of DISCOBSD.UF2 on a FAT32 card).
#
#   check.sh LIBHSAOUT
#
# The FAT32 volumes are written by the tree's own msdosfs tool
# (usr.sbin/msdosfs, built for the host with a 32 KiB buffer so that it
# mounts 32 KiB clusters) on volumes that sbin/newfs_msdos/mkfs_msdos.c
# formats with 512 B, 4 KiB and 32 KiB clusters; the root image comes from
# tools/fsutil. The 32 KiB volume holds 65525 clusters, which is 2 GiB, so
# every volume is a sparse file. oracle.py judges each volume and states
# where the tail lies through tests/msdosfs_core/fat_reader.py, which shares
# no code with the locator or the writer, and checks that the sectors of
# that extent hold the root image.
#
# Known-good volumes: the composite in the root directory and in a
# subdirectory, its name in lower and upper case, on all three cluster
# sizes, behind a root directory that spans several clusters; a volume that
# starts at a nonzero sector; a root directory copy that takes precedence
# over a subdirectory copy; a deleted directory and a volume label that
# carry the name; an entry stored in lower case; a live FAT other than the
# first. Known-bad volumes make each error the locator reports: not FAT32
# (FAT16, exFAT, a zeroed sector, thirteen BPB images damaged in eleven fields), a failed read
# at every read the walk makes, an absent file, a file whose clusters are
# not one run (made by filling a volume, freeing two holes and writing a file
# larger than either), a damaged UF2 head, a tail that is empty, short or not
# whole sectors, a tail without the file-system magic, chains that loop,
# leave the volume or end early, and sixteen sibling directories that share
# one long chain (the search's read budget). A seeded run damages sectors the walk reads
# and requires a defined result inside a read budget, under the address and
# undefined-behavior sanitizers.
#
# Then mutants of the locator must fail: no contiguity check, a case-sensitive
# name compare, no subdirectory search, a head without the 32 KiB roundup, no
# magic check, a cluster base one cluster off, and the other single-point
# edits listed below. Calibration: the shipped source passes and each mutant
# fails. The locator also compiles strict C17 with the host compiler and,
# when arm-none-eabi-gcc is present, for Cortex-M0+ with no standard header
# and with the PICOCALC kernel's flags behind <sys/param.h> and <sys/systm.h>.
#
# Set TMPDIR to a directory with 0.9 GB free (a tmpfs is fine); the work
# directory holds the volumes and is emptied on exit. FAT32TAIL_KERNEL_UF2
# names a kernel UF2 for the real-kernel case; the default is the
# PICOCALC image of this tree, and the case is skipped, saying so,
# when the file is absent.
#
# Exit status: 0 when every case holds and every mutant fails, 1 otherwise.

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
: "${MAKE:?set MAKE to bmake}"

# Detached flag queries cannot use the parent's closed jobserver pipes.
unset MAKEFLAGS MFLAGS

libhsaout=${1:?usage: check.sh LIBHSAOUT}
topsrc=$(cd "$(dirname "$0")/../.." && pwd)
here=$topsrc/tests/fat32tail
dev=$topsrc/sys/arch/rp2040/dev
work=$(mktemp -d "${TMPDIR:-/tmp}/fat32tail.XXXXXX")
export SOURCE_DATE_EPOCH=1700000000
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all"

# The work directory holds plain files and the subdirectories obj and mut.
cleanup()
{
	for sub in obj mut; do
		for file in "$work/$sub"/*; do
			if [ -f "$file" ]; then rm -f "$file"; fi
		done
		rmdir "$work/$sub" 2>/dev/null || true
	done
	for file in "$work"/*; do
		if [ -f "$file" ]; then rm -f "$file"; fi
	done
	rmdir "$work" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$work/obj" "$work/mut"

fail()
{
	echo "fat32tail: FAIL: $*" >&2
	exit 1
}

# The volumes hold about 0.55 GB of data. Too little room is a skip, not a
# failed case: nothing is built and the gate says so.
need_kib=900000
free_kib=$(df -Pk "$work" | awk 'NR == 2 { print $4 }')
echo "fat32tail: work directory on $(df -P "$work" | awk 'NR == 2 { print $1 }'), $free_kib KiB free"
if [ "$free_kib" -lt "$need_kib" ]; then
	echo "fat32tail: skip: not run, $free_kib KiB free in $work and $need_kib KiB needed; set TMPDIR"
	exit 0
fi

# ---- tools ----

# compile_msdosfs OUT: usr.sbin/msdosfs for the host, as tests/msdosfs_core
# builds it, with a buffer that holds a 32 KiB cluster.
compile_msdosfs()
{
	dir=$topsrc/usr.sbin/msdosfs
	defs=$("$MAKE" -C "$dir" -V '${MSDOSFS_DEFS}')
	netbsd=$("$MAKE" -C "$dir" -V '${NETBSD_FLAGS}')
	local_flags=$("$MAKE" -C "$dir" -V '${LOCAL_FLAGS}')
	objects=
	for source in $("$MAKE" -C "$dir" -V '${HOST_NETBSD}'); do
		object=$work/obj/$(basename "$source" .c).o
		# shellcheck disable=SC2086  # the flag variables are word lists
		${CC:-cc} -O1 -g -std=gnu17 -Wall -Wextra -Werror $sanitize \
		    $defs -DMSDOSFS_BUFSIZE=32768 $netbsd -c -o "$object" "$source" ||
		    fail "build of $source"
		objects="$objects $object"
	done
	for source in msdosfs.c msdosfs_buf.c msdosfs_env.c; do
		object=$work/obj/${source%.c}.o
		# shellcheck disable=SC2086
		${CC:-cc} -O1 -g -Wall -Wextra -Werror $sanitize $local_flags \
		    -D_DEFAULT_SOURCE $defs -DMSDOSFS_BUFSIZE=32768 -c -o "$object" \
		    "$dir/$source" || fail "build of $source"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086  # $objects is a list of paths without spaces
	${CC:-cc} $sanitize -o "$1" $objects || fail "link of $1"
}

# compile_mkfat OUT: the formatter driver with an explicit cluster size.
compile_mkfat()
{
	dir=$topsrc/usr.sbin/msdosfs
	defs=$("$MAKE" -C "$dir" -V '${MSDOSFS_DEFS}')
	netbsd=$("$MAKE" -C "$dir" -V '${NETBSD_FLAGS}')
	# shellcheck disable=SC2086
	${CC:-cc} -O1 -g -std=gnu17 -Wall -Wextra -Werror $defs $netbsd \
	    -o "$1" "$here/mkfat.c" "$topsrc/sbin/newfs_msdos/mkfs_msdos.c" ||
	    fail "build of mkfat"
}

# compile_fsutil OUT: tools/fsutil without FUSE, as tests/fsutil_clock does.
compile_fsutil()
{
	libbsd_cflags=
	libbsd_libs=
	if [ "$(uname -s)" = Linux ]; then
		libbsd_cflags=$(pkg-config libbsd-overlay --cflags) ||
		    fail "pkg-config libbsd-overlay (libbsd-dev) is required"
		libbsd_libs=$(pkg-config libbsd-overlay --libs) ||
		    fail "pkg-config libbsd-overlay (libbsd-dev) is required"
	fi
	objects=
	for unit in fsutil inode create check file manifest block superblock; do
		# shellcheck disable=SC2086
		${CC:-cc} -std=gnu17 -O1 -g -Wall -Werror $libbsd_cflags \
		    -I"$topsrc/tools/fsutil" -c -o "$work/obj/fsutil_$unit.o" \
		    "$topsrc/tools/fsutil/$unit.c" || fail "build of fsutil $unit"
		objects="$objects $work/obj/fsutil_$unit.o"
	done
	# shellcheck disable=SC2086
	${CC:-cc} -o "$1" $objects "$libhsaout" $libbsd_libs ||
	    fail "link of fsutil"
}

# compile_cli OUT SOURCE: the locator driver over SOURCE, which includes the
# shipped fat32tail.h.
compile_cli()
{
	${CC:-cc} -std=gnu17 -O1 -g -Wall -Wextra -Werror $sanitize \
	    -I"$dev" -o "$1" "$here/fat32tail_cli.c" "$2" || return 1
}

compile_msdosfs "$work/msdosfs"
compile_mkfat "$work/mkfat"
compile_fsutil "$work/fsutil"
compile_cli "$work/cli" "$dev/fat32tail.c" || fail "build of the locator driver"
msdosfs=$work/msdosfs
oracle() { "$PYTHON" "$here/oracle.py" "$@"; }
fixtures() { "$PYTHON" "$here/fixtures.py" "$@"; }
builder() { "$PYTHON" "$topsrc/tools/picocalc/mkloaderuf2.py" "$@"; }

# ---- the builder ----

"$PYTHON" "$here/builder_test.py" "$topsrc" "$work" || fail "builder tests"

# The header and the builder state one head alignment.
header_align=$(sed -n 's/^#define[[:space:]]*FT32_HEAD_ALIGN[[:space:]]*\([0-9]*\).*/\1/p' \
    "$dev/fat32tail.h")
builder_align=$("$PYTHON" -c 'import sys
sys.path.insert(0, sys.argv[1])
import mkloaderuf2
print(mkloaderuf2.HEAD_ALIGN)' "$topsrc/tools/picocalc")
[ "$header_align" = "$builder_align" ] ||
    fail "FT32_HEAD_ALIGN $header_align, builder $builder_align"
echo "fat32tail: builder and locator share the head alignment $header_align"

# ---- inputs ----

"$work/fsutil" -n -s 512 "$work/root512.img" >/dev/null || fail "fsutil 512"
"$work/fsutil" -n -s 1024 "$work/root1024.img" >/dev/null || fail "fsutil 1024"
"$work/fsutil" -n -s 16384 "$work/root16m.img" >/dev/null || fail "fsutil 16384"
# A kernel of 471 blocks (the PicoCalc PSRAM image's size when the case was
# written): 471 * 512 is not
# a multiple of 32768, so the head's roundup moves the tail.
fixtures uf2 471 "$work/k471.uf2"
printf 'decoy\n' >"$work/decoy.txt"

# build_composite ROOT: $work/discobsd.uf2 from the kernel and the root image.
build_composite()
{
	builder -o "$work/discobsd.uf2" "$work/k471.uf2" "$1" || fail "builder on $1"
}

# put IMAGE LOCAL PATH, mkdir IMAGE PATH: msdosfs commands that must succeed.
put() { "$msdosfs" put "$1" "$2" "$3" || fail "put $3 into $1"; }
mkd() { "$msdosfs" mkdir "$1" "$2" || fail "mkdir $2 in $1"; }

# decoys IMAGE: 24 long names, so the root directory spans several clusters
# on small-cluster volumes and the file's entry is not in the first sector.
decoys()
{
	n=1
	while [ "$n" -le 24 ]; do
		put "$1" "$work/decoy.txt" "/decoy file number $n.txt"
		n=$((n + 1))
	done
}

# volume NAME SPC SECTORS: a formatted FAT32 volume $work/NAME.img.
volume()
{
	"$work/mkfat" "$work/$1.img" "$3" "$2" >/dev/null || fail "mkfat $1"
}

cases=$work/cases.tsv
: >"$cases"
tab=$(printf '\t')

# add_case LABEL IMAGE VOLUME_LBA EXPECT: EXPECT is "ok LBA NSECTORS" or
# "error NAME".
add_case()
{
	printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >>"$cases"
}

# ok_extent IMAGE VOLUME_LBA ROOT: the oracle's "ok LBA NSECTORS" for IMAGE.
ok_extent()
{
	extent=$(oracle extent "$1" "$2" "$3") ||
	    { echo "$extent" >&2; fail "oracle on $1: $extent"; }
	echo "ok $extent"
}

# kind_is IMAGE VOLUME_LBA KIND: the oracle finds the file in the state KIND.
kind_is()
{
	got=$(oracle classify "$1" "$2") || fail "oracle classify $1: $got"
	[ "$got" = "$3" ] || fail "$1: the oracle reads the file as $got, expected $3"
}

# ---- known-good volumes: three cluster sizes, two places, two cases ----

for geometry in 512:1:70000 4k:8:600000 32k:64:4194900; do
	name=${geometry%%:*}
	rest=${geometry#*:}
	spc=${rest%%:*}
	sectors=${rest#*:}
	build_composite "$work/root16m.img"
	for place in root apps; do
		for casing in lower upper; do
			label=g$name-$place-$casing
			image=$work/$label.img
			volume "$label" "$spc" "$sectors"
			decoys "$image"
			if [ "$casing" = lower ]; then
				file=discobsd.uf2
			else
				file=DISCOBSD.UF2
			fi
			if [ "$place" = root ]; then
				path=/$file
			else
				mkd "$image" /aaa
				put "$image" "$work/decoy.txt" /aaa/decoy.txt
				mkd "$image" /pico1-apps
				path=/pico1-apps/$file
			fi
			put "$image" "$work/discobsd.uf2" "$path"
			"$msdosfs" df "$image" >"$work/df.out"
			grep -q " cluster $((spc * 512)) " "$work/df.out" ||
			    fail "$label: cluster size is not $((spc * 512)): $(cat "$work/df.out")"
			oracle check "$image" 0 >"$work/oracle.out" ||
			    fail "$label: the reader rejects the volume: $(cat "$work/oracle.out")"
			kind_is "$image" 0 contiguous
			add_case "$label" "$image" 0 "$(ok_extent "$image" 0 "$work/root16m.img")"
		done
	done
done
echo "fat32tail: 12 volumes of 512 B, 4 KiB and 32 KiB clusters hold a 16 MiB root in the composite"

# ---- base volumes for the damaged variants ----

build_composite "$work/root512.img"
cp "$work/discobsd.uf2" "$work/small.uf2"
volume base-root 1 70000
decoys "$work/base-root.img"
put "$work/base-root.img" "$work/small.uf2" /discobsd.uf2
volume base-apps 1 70000
decoys "$work/base-apps.img"
mkd "$work/base-apps.img" /aaa
put "$work/base-apps.img" "$work/decoy.txt" /aaa/decoy.txt
mkd "$work/base-apps.img" /pico1-apps
put "$work/base-apps.img" "$work/small.uf2" /pico1-apps/discobsd.uf2
for base in base-root base-apps; do
	oracle check "$work/$base.img" 0 >"$work/oracle.out" || fail "$base: $(cat "$work/oracle.out")"
	kind_is "$work/$base.img" 0 contiguous
done
exp_root=$(ok_extent "$work/base-root.img" 0 "$work/root512.img")
exp_apps=$(ok_extent "$work/base-apps.img" 0 "$work/root512.img")
add_case base-root "$work/base-root.img" 0 "$exp_root"
add_case base-apps "$work/base-apps.img" 0 "$exp_apps"

# variant LABEL BASE EXPECT PATCH...: a copy of BASE damaged by fixtures.py patch.
variant()
{
	label=$1
	base=$2
	expect=$3
	shift 3
	fixtures shift "$work/$base.img" "$work/v-$label.img" 0
	fixtures patch "$work/v-$label.img" 0 "$@"
	add_case "$label" "$work/v-$label.img" 0 "$expect"
}

# Not FAT32: each field of the BPB the locator tests, then FAT16 and exFAT.
variant nf-signature base-root "error notfat32" bpb 510 2 0
variant nf-sector-size base-root "error notfat32" bpb 11 2 1024
variant nf-sectors-per-cluster base-root "error notfat32" bpb 13 1 3
variant nf-root-entries base-root "error notfat32" bpb 17 2 16
variant nf-fat16-size base-root "error notfat32" bpb 22 2 1
variant nf-fats base-root "error notfat32" bpb 16 1 0
variant nf-few-clusters base-root "error notfat32" bpb 32 4 66110
variant nf-small-fat base-root "error notfat32" bpb 36 4 100
variant nf-live-fat base-root "error notfat32" bpb 40 2 0x0089
variant nf-reserved-zero base-root "error notfat32" bpb 14 2 0
variant nf-root-cluster-0 base-root "error notfat32" bpb 44 4 0
variant nf-root-cluster-1 base-root "error notfat32" bpb 44 4 1
variant nf-root-cluster-high base-root "error notfat32" bpb 44 4 0x0FFFFFF0
variant nf-exfat base-root "error notfat32" exfat-like
"$msdosfs" newfs "$work/fat16.img" 20000 16 >/dev/null || fail "newfs FAT16"
put "$work/fat16.img" "$work/small.uf2" /discobsd.uf2
oracle check "$work/fat16.img" 0 >"$work/oracle.out" || fail "FAT16 volume: $(cat "$work/oracle.out")"
add_case nf-fat16 "$work/fat16.img" 0 "error notfat32"
head -c 4096 /dev/zero >"$work/zero.img"
add_case nf-zero-sector "$work/zero.img" 0 "error notfat32"

# An image that ends inside the volume: the read past its end fails.
head -c 51200 "$work/base-root.img" >"$work/io-short.img"
add_case io-short-image "$work/io-short.img" 0 "error io"

# Absent: an empty volume, near-miss names, a directory with the name, a
# deleted entry, a label and a directory with the name, a copy two
# directories down.
volume absent-empty 1 70000
decoys "$work/absent-empty.img"
kind_is "$work/absent-empty.img" 0 absent
add_case absent-empty "$work/absent-empty.img" 0 "error absent"
volume absent-decoys 1 70000
decoys "$work/absent-decoys.img"
put "$work/absent-decoys.img" "$work/small.uf2" /DISCOBSD.UF3
put "$work/absent-decoys.img" "$work/small.uf2" /DISCOBS.UF2
put "$work/absent-decoys.img" "$work/small.uf2" /DISCOBSDX.UF2
mkd "$work/absent-decoys.img" /discobsd.uf2
mkd "$work/absent-decoys.img" /aaa
mkd "$work/absent-decoys.img" /aaa/bbb
put "$work/absent-decoys.img" "$work/small.uf2" /aaa/bbb/discobsd.uf2
kind_is "$work/absent-decoys.img" 0 absent
add_case absent-decoys "$work/absent-decoys.img" 0 "error absent"
variant absent-deleted base-root "error absent" delete-entry DISCOBSDUF2
variant absent-label base-root "error absent" file-attr 0x08
variant absent-directory base-root "error absent" file-attr 0x10

# Found although the walk must step over other entries.
variant found-after-label base-root "$exp_root" label-before
variant found-after-deleted base-root "$exp_root" delete-before
variant found-lowercase-short-root base-root "$exp_root" lowercase-short
variant found-lowercase-short-apps base-apps "$exp_apps" lowercase-short
variant found-second-fat base-root "$exp_root" active-fat1

# Chains and entries that are damaged: a looping directory, a chain through a
# free, a bad and an end-of-chain entry, a first cluster outside the volume,
# sizes that disagree with the chain, a subdirectory outside the volume.
variant corrupt-root-loop base-root "error corrupt" root-loop
variant corrupt-chain-free base-root "error corrupt" file-fat 3 0
variant corrupt-chain-bad base-root "error corrupt" file-fat 3 0x0FFFFFF7
variant corrupt-chain-early-end base-root "error corrupt" file-fat 5 0x0FFFFFFF
variant corrupt-first-cluster-1 base-root "error corrupt" file-first 1
variant corrupt-first-cluster-high base-root "error corrupt" file-first 0x0FFFFFF0
variant corrupt-size-past-chain base-root "error corrupt" file-size 0x6000000
variant corrupt-subdirectory base-apps "error corrupt" subdir-first 0x0FFFFFF0
variant fragmented-link base-root "error fragmented" file-fat 2 5
variant uf2head-size-0 base-root "error uf2head" file-size 0
variant uf2head-size-100 base-root "error uf2head" file-size 100

# Sixteen sibling directories whose entries name one chain of deleted entries:
# the search stops at its read budget (the 4000-cluster chain costs about 4,000
# reads to scan once and 64,000 over sixteen siblings); with a chain of 100
# clusters it ends with the file absent.
for length in 4000 100; do
	volume aliased-$length 1 70000
	n=1
	while [ "$n" -le 16 ]; do
		mkd "$work/aliased-$length.img" "/subdirectory $n"
		n=$((n + 1))
	done
	fixtures patch "$work/aliased-$length.img" 0 alias-chain "$length"
done
add_case aliased-chains-over-budget "$work/aliased-4000.img" 0 "error corrupt"
add_case aliased-chains-in-budget "$work/aliased-100.img" 0 "error absent"

# UF2 head, tail and magic: files the builder refuses, put in a blank volume.
volume blank 1 70000
# blank_with LABEL FILE EXPECT: a copy of the blank volume holding FILE as /discobsd.uf2.
blank_with()
{
	fixtures shift "$work/blank.img" "$work/b-$1.img" 0
	put "$work/b-$1.img" "$2" /discobsd.uf2
	add_case "$1" "$work/b-$1.img" 0 "$3"
}
# composite_case LABEL KIND EXPECT: EXPECT "oracle" asks the oracle for the extent.
composite_case()
{
	fixtures composite "$2" 471 "$work/root512.img" "$work/comp.bin"
	expect=$3
	if [ "$expect" = oracle ]; then
		fixtures shift "$work/blank.img" "$work/b-$1.img" 0
		put "$work/b-$1.img" "$work/comp.bin" /discobsd.uf2
		add_case "$1" "$work/b-$1.img" 0 "$(ok_extent "$work/b-$1.img" 0 "$work/root512.img")"
		return
	fi
	blank_with "$1" "$work/comp.bin" "$expect"
}
composite_case uf2head-start-magic bad-start-magic "error uf2head"
composite_case uf2head-end-magic bad-end-magic "error uf2head"
composite_case uf2head-zero-blocks zero-blocks "error uf2head"
composite_case uf2head-huge-blocks huge-blocks "error uf2head"
composite_case uf2head-first-number first-number "error uf2head"
composite_case uf2head-last-number last-number "error uf2head"
composite_case uf2head-last-total last-total "error uf2head"
composite_case uf2head-last-magic last-magic "error uf2head"
composite_case uf2head-lies-short lies-short "error uf2head"
composite_case tail-empty no-tail "error tail"
composite_case tail-one-sector one-sector "error tail"
composite_case tail-short short-tail "error tail"
composite_case tail-stray-bytes stray-bytes "error tail"
composite_case nomagic-first bad-magic1 "error nomagic"
composite_case nomagic-second bad-magic2 "error nomagic"
composite_case nomagic-zero-tail zero-tail "error nomagic"
head -c 100 /dev/zero >"$work/tiny.bin"
blank_with uf2head-tiny-file "$work/tiny.bin" "error uf2head"
: >"$work/empty.bin"
blank_with uf2head-empty-file "$work/empty.bin" "error uf2head"
composite_case control-good good oracle

# Two copies: the root directory's wins over a subdirectory's, and a deleted
# directory's copy is not seen.
fixtures shift "$work/blank.img" "$work/priority.img" 0
put "$work/priority.img" "$work/small.uf2" /discobsd.uf2
mkd "$work/priority.img" /pico1-apps
builder -o "$work/discobsd.uf2" "$work/k471.uf2" "$work/root1024.img" || fail "builder on root1024"
put "$work/priority.img" "$work/discobsd.uf2" /pico1-apps/discobsd.uf2
add_case priority-root-first "$work/priority.img" 0 "$(ok_extent "$work/priority.img" 0 "$work/root512.img")"
fixtures shift "$work/blank.img" "$work/deleted-dir.img" 0
mkd "$work/deleted-dir.img" /gone
put "$work/deleted-dir.img" "$work/small.uf2" /gone/discobsd.uf2
mkd "$work/deleted-dir.img" /pico1-apps
put "$work/deleted-dir.img" "$work/discobsd.uf2" /pico1-apps/discobsd.uf2
fixtures patch "$work/deleted-dir.img" 0 delete-entry GONE
add_case deleted-directory-skipped "$work/deleted-dir.img" 0 "$(ok_extent "$work/deleted-dir.img" 0 "$work/root1024.img")"

# A volume that starts at sector 2048, as behind a partition table.
fixtures shift "$work/base-root.img" "$work/offset.img" 2048
add_case volume-at-2048 "$work/offset.img" 2048 "$(ok_extent "$work/offset.img" 2048 "$work/root512.img")"

# Fragmented through the tool: fill the volume, free two holes of 1000
# clusters either side of a kept file, then write a composite larger than
# either hole (1536 clusters). Only the two holes and a few clusters are free,
# so no allocator can place the file in one run.
volume fragmented 1 70000
head -c 512000 /dev/zero >"$work/hole.bin"
printf 'kept\n' >"$work/kept.bin"
put "$work/fragmented.img" "$work/hole.bin" /x1
put "$work/fragmented.img" "$work/kept.bin" /k1
put "$work/fragmented.img" "$work/hole.bin" /x2
"$msdosfs" df "$work/fragmented.img" >"$work/df.out"
free=$(awk '{ print $NF }' "$work/df.out")
head -c $(((free - 4) * 512)) /dev/zero >"$work/fill.bin"
put "$work/fragmented.img" "$work/fill.bin" /big
"$msdosfs" rm "$work/fragmented.img" /x1 || fail "rm x1"
"$msdosfs" rm "$work/fragmented.img" /x2 || fail "rm x2"
put "$work/fragmented.img" "$work/small.uf2" /discobsd.uf2
kind_is "$work/fragmented.img" 0 fragmented
add_case fragmented "$work/fragmented.img" 0 "error fragmented"

# Volumes written by independent software (dosfstools' mkfs.fat and mtools'
# mmd and mcopy), which store the short entries in their own forms, are cases
# too, and fsck.fat agrees with each; each tool absent is reported.
mkfs_fat=$(command -v mkfs.fat || true)
mcopy_tool=$(command -v mcopy || true)
mmd_tool=$(command -v mmd || true)
fsck_fat=$(command -v fsck.fat || true)
if [ -n "$mkfs_fat" ] && [ -n "$mcopy_tool" ] && [ -n "$mmd_tool" ]; then
	build_composite "$work/root16m.img"
	for geometry in 512:1:70000 4k:8:600000 32k:64:4194900; do
		name=${geometry%%:*}
		rest=${geometry#*:}
		spc=${rest%%:*}
		sectors=${rest#*:}
		for place in root apps; do
			label=x$name-$place
			image=$work/$label.img
			"$PYTHON" -c 'import sys
open(sys.argv[1], "wb").truncate(int(sys.argv[2]) * 512)' "$image" "$sectors"
			"$mkfs_fat" -F 32 -s "$spc" -S 512 "$image" >/dev/null ||
			    fail "mkfs.fat $label"
			if [ "$place" = root ]; then
				MTOOLS_SKIP_CHECK=1 "$mcopy_tool" -i "$image" "$work/discobsd.uf2" ::discobsd.uf2 ||
				    fail "mcopy $label"
			else
				MTOOLS_SKIP_CHECK=1 "$mmd_tool" -i "$image" ::/pico1-apps || fail "mmd $label"
				MTOOLS_SKIP_CHECK=1 "$mcopy_tool" -i "$image" "$work/discobsd.uf2" \
				    ::/pico1-apps/DISCOBSD.UF2 || fail "mcopy $label"
			fi
			if [ -n "$fsck_fat" ]; then
				"$fsck_fat" -n "$image" >"$work/fsck.out" 2>&1 ||
				    { cat "$work/fsck.out" >&2; fail "fsck.fat rejects $label"; }
			fi
			oracle check "$image" 0 >"$work/oracle.out" ||
			    fail "$label: the reader rejects the volume: $(cat "$work/oracle.out")"
			kind_is "$image" 0 contiguous
			add_case "$label" "$image" 0 "$(ok_extent "$image" 0 "$work/root16m.img")"
		done
	done
	echo "fat32tail: 6 volumes written by mkfs.fat and mcopy are cases"
else
	echo "fat32tail: independent-writer volumes not run: mkfs.fat, mmd or mcopy absent (dosfstools, mtools)"
fi
if [ -z "$fsck_fat" ]; then
	echo "fat32tail: fsck.fat cross-check not run: dosfstools absent"
fi

# A real kernel image, when one is built: the builder accepts it and the
# locator finds the tail.
kernel_uf2=${FAT32TAIL_KERNEL_UF2:-$topsrc/sys/arch/rp2040/compile/PICOCALC/unix.uf2}
if [ -f "$kernel_uf2" ]; then
	builder -o "$work/discobsd.uf2" "$kernel_uf2" "$work/root512.img" ||
	    fail "builder rejects $kernel_uf2"
	volume realkernel 8 600000
	put "$work/realkernel.img" "$work/discobsd.uf2" /discobsd.uf2
	add_case real-kernel "$work/realkernel.img" 0 "$(ok_extent "$work/realkernel.img" 0 "$work/root512.img")"
	echo "fat32tail: a real kernel image is a case"
else
	echo "fat32tail: real kernel case not run: no kernel UF2 at the default path or in FAT32TAIL_KERNEL_UF2"
fi

# Reads fail at every position, and sectors the walk reads are damaged.
sweeps="g512-apps-upper g4k-root-lower g32k-apps-lower base-apps"
fuzzes="g512-root-lower:1 g4k-apps-upper:2 base-apps:3 base-root:4 g32k-root-upper:5"

# The oracle rejects a wrong root image and a damaged one before its verdicts
# count: the extent of a volume holding root512 is not root1024's, and one
# changed byte of the tail fails the comparison.
if oracle extent "$work/base-root.img" 0 "$work/root1024.img" >"$work/oracle.out"; then
	fail "the oracle accepted the wrong root image"
fi
cp "$work/root512.img" "$work/root-flipped.img"
printf 'X' | dd of="$work/root-flipped.img" bs=1 seek=70000 conv=notrunc 2>/dev/null
if oracle extent "$work/base-root.img" 0 "$work/root-flipped.img" >"$work/oracle.out"; then
	fail "the oracle accepted a root image that differs by one byte"
fi
echo "fat32tail: the oracle rejects a wrong root image and a one-byte difference"

# ---- the suite ----

timeout_cmd=
if command -v timeout >/dev/null 2>&1; then
	timeout_cmd="timeout 20"
fi

# suite CLI: every case against CLI; FAIL lines on stdout, status 1 on any.
suite()
{
	cli=$1
	bad=0
	while IFS=$tab read -r label image vol expect; do
		# shellcheck disable=SC2086  # $timeout_cmd is a word list
		got=$($timeout_cmd "$cli" "$image" "$vol" 2>&1) || got="exit $?: $got"
		case $got in
		"ok "*) got=$(echo "$got" | awk '{ print $1, $2, $3 }') ;;
		*) got=$(echo "$got" | awk '{ print $1, $2 }') ;;
		esac
		[ "$got" = "$expect" ] && continue
		echo "FAIL $label: got '$got', want '$expect'"
		bad=1
	done <"$cases"
	for label in $sweeps; do
		# shellcheck disable=SC2086
		got=$($timeout_cmd "$cli" "$work/$label.img" 0 sweep 2>&1) || got="FAIL sweep $label: $got"
		case $got in
		"sweep ok "*) ;;
		*) echo "FAIL sweep $label: $got"; bad=1 ;;
		esac
	done
	for item in $fuzzes; do
		label=${item%:*}
		seed=${item#*:}
		# shellcheck disable=SC2086
		got=$($timeout_cmd "$cli" "$work/$label.img" 0 fuzz "$seed" 400 2>&1) || got="FAIL fuzz $label: $got"
		case $got in
		"fuzz ok "*) ;;
		*) echo "FAIL fuzz $label: $got"; bad=1 ;;
		esac
	done
	return $bad
}

echo "fat32tail: the work directory holds $(du -sk "$work" | awk '{ print $1 }') KiB"
ncases=$(wc -l <"$cases" | tr -d ' ')
if ! suite "$work/cli" >"$work/suite.out"; then
	cat "$work/suite.out" >&2
	fail "the shipped locator fails a case"
fi
echo "fat32tail: the shipped locator passes $ncases cases, a read failure at every read of 4 volumes and 5 damaged-sector runs"

# Each error code is produced by some case.
for name in io notfat32 absent fragmented uf2head tail nomagic corrupt; do
	grep -q "${tab}error $name\$" "$cases" || fail "no case produces error $name"
done

# ---- the locator compiles for the kernel ----

${CC:-cc} -std=c17 -pedantic-errors -Wall -Wextra -Werror -Wshadow -Wundef \
    -Wconversion -I"$dev" -c -o "$work/obj/fat32tail_host.o" "$dev/fat32tail.c" ||
    fail "strict C17 build of fat32tail.c"
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
	# No include path and no standard headers: the file is kernel-ready as it stands.
	arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu17 -Wall -Wextra \
	    -Werror -Wshadow -Wundef -Wformat=2 -ffreestanding -nostdinc \
	    -I"$dev" -c -o "$work/obj/fat32tail_arm.o" "$dev/fat32tail.c" ||
	    fail "Cortex-M0+ build of fat32tail.c"
	if command -v arm-none-eabi-size >/dev/null 2>&1; then
		echo "fat32tail: Cortex-M0+ -Os text size: $(arm-none-eabi-size "$work/obj/fat32tail_arm.o" | awk 'NR == 2 { print $1, "bytes" }')"
	fi
	# With the PICOCALC kernel's own flags and the kernel headers in front, so
	# that no parameter or macro of the locator collides with a kernel name
	# (-Wshadow -Werror rejects a parameter named read).
	kernel_dir=$topsrc/sys/arch/rp2040/compile/PICOCALC
	if [ ! -e "$kernel_dir/machine" ] || [ ! -e "$kernel_dir/sys" ]; then
		echo "fat32tail: kernel-flag build skip: not run, sys/arch/rp2040/compile/PICOCALC has no machine and sys links yet; bmake MACHINE=rp2040 kernel makes them"
	else
		kernel_flags=$("$MAKE" -C "$kernel_dir" -V '${CFLAGS}' | sed 's/-g -gdwarf-4//; s/-fdebug-prefix-map=[^ ]*//')
		kernel_defs=$("$MAKE" -C "$kernel_dir" -V '${DEFS}' | sed 's/-MT [^ ]* -MP -MD -MF [^ ]*//')
		[ -n "$kernel_flags" ] && [ -n "$kernel_defs" ] || fail "no CFLAGS or DEFS from $kernel_dir"
		# shellcheck disable=SC2086  # the flag variables are word lists
		(cd "$kernel_dir" && arm-none-eabi-gcc $kernel_flags $kernel_defs \
		    -include sys/param.h -include sys/systm.h \
		    -c -o "$work/obj/fat32tail_kernel.o" ../../dev/fat32tail.c) ||
		    fail "build of fat32tail.c with the PICOCALC kernel flags and headers"
		echo "fat32tail: the locator builds with the PICOCALC kernel flags after <sys/param.h> and <sys/systm.h>"
	fi
else
	echo "fat32tail: Cortex-M0+ builds not run: arm-none-eabi-gcc absent"
fi

# ---- mutants ----

# mutant NAME OLD NEW: the locator with the one OLD replaced by NEW must fail
# the suite. \n and \t in OLD and NEW stand for a newline and a tab.
nmutants=0
mutant()
{
	name=$1
	"$PYTHON" - "$dev/fat32tail.c" "$work/mut/$name.c" "$2" "$3" <<'PY'
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = sys.argv[3].replace("\\n", "\n").replace("\\t", "\t")
new = sys.argv[4].replace("\\n", "\n").replace("\\t", "\t")
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count {source.count(old)}: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
	[ -f "$work/mut/$name.c" ] || fail "mutant $name: the anchor does not match"
	compile_cli "$work/mut/$name" "$work/mut/$name.c" ||
	    fail "mutant $name does not compile"
	if suite "$work/mut/$name" >"$work/suite.out" 2>&1; then
		fail "mutant $name passed the suite"
	fi
	nmutants=$((nmutants + 1))
	echo "fat32tail: mutant rejected: $name ($(grep -c '^FAIL' "$work/suite.out") failures, first: $(head -n 1 "$work/suite.out" | cut -c1-70))"
}

mutant no-contiguity-check '\t\tif (next != c + 1)\n' '\t\tif (0)\n'
mutant case-sensitive-name "if (c >= 'a' && c <= 'z')" 'if (0)'
mutant no-subdirectory-search 'r = dir_scan(&v, v.root, 1, &f);' 'r = NOENT;'
mutant no-head-roundup 'headsec = (nblocks + FT32_HEAD_SECTORS - 1) &\n\t    ~(unsigned)(FT32_HEAD_SECTORS - 1);' 'headsec = nblocks;'
mutant no-first-magic-check 'if (le32(v.buf) != FS_MAGIC1)' 'if (0)'
mutant no-second-magic-check 'if (le32(v.buf + FS_MAGIC2_OFF) != FS_MAGIC2)' 'if (0)'
mutant cluster-base-off-by-one 'return v->data + (clus - 2) * v->spc;' 'return v->data + (clus - 1) * v->spc;'
mutant no-tail-alignment-check 'if (f.size % FT32_SECTOR != 0 || nsect < headsec + FS_TAIL_SECTORS)' 'if (nsect < headsec + FS_TAIL_SECTORS)'
mutant no-tail-minimum 'nsect < headsec + FS_TAIL_SECTORS)' 'nsect <= headsec)'
mutant fat12-threshold '#define\tFAT32_MIN_CLUSTERS\t65525u' '#define\tFAT32_MIN_CLUSTERS\t4085u'
mutant no-fat-capacity-check 'fatsz < (v->nclusters + 2 + FAT_PER_SECTOR - 1) / FAT_PER_SECTOR)' '0)'
mutant no-search-budget 'if (v->budget == 0)' 'if (0)'
mutant no-reserved-sectors-check 'rsvd == 0 || ' ''
mutant no-root-cluster-check 'if (!clus_ok(v, v->root))\n\t\treturn FT32_E_NOTFAT32;' ''
mutant read-error-ignored 'if (v->readfn(v->ctx, n, v->buf) != 0)' 'if ((v->readfn(v->ctx, n, v->buf), 0))'
mutant no-last-block-check 'if ((r = uf2_block(&v, nblocks - 1, nblocks, first)) != FT32_OK)' 'if (0)'
mutant no-block-count-bound 'if (nblocks == 0 || nblocks > nsect)' 'if (nblocks == 0)'
mutant no-block-number-check 'le32(b + 20) != n ||' '0 ||'
mutant no-start-magic-check 'if (le32(b) != UF2_START0 || le32(b + 4) != UF2_START1 ||' 'if (le32(b + 4) != UF2_START1 ||'
mutant no-end-magic-check 'le32(b + 508) != UF2_END ||' '0 ||'
mutant no-size-minimum 'if (f.size < FT32_SECTOR)\n\t\treturn FT32_E_UF2HEAD;' ''
mutant deleted-entry-read-as-live 'if (e[0] == DIR_FREE || (e[11] & ATTR_VOLUME) != 0)' 'if ((e[11] & ATTR_VOLUME) != 0)'
mutant deleted-entry-ends-directory 'if (e[0] == DIR_END)' 'if (e[0] == DIR_END || e[0] == DIR_FREE)'
mutant label-read-as-file 'if (e[0] == DIR_FREE || (e[11] & ATTR_VOLUME) != 0)' 'if (e[0] == DIR_FREE)'
mutant directory-matches-name 'if ((e[11] & ATTR_DIR) == 0 && name_match(e)) {' 'if (name_match(e)) {'
mutant live-fat-ignored 'fatn = (ext & 0x80) != 0 ? (ext & 0x0f) : 0;' 'fatn = (ext & 0) != 0 ? (ext & 0x0f) : 0;'
mutant subdirectory-before-root 'r = dir_scan(&v, v.root, 0, &f);\n\tif (r == NOENT)\n\t\tr = dir_scan(&v, v.root, 1, &f);' 'r = dir_scan(&v, v.root, 1, &f);\n\tif (r == NOENT)\n\t\tr = dir_scan(&v, v.root, 0, &f);'
mutant first-subdirectory-only '\t\t\t\t\tif (r != NOENT)\n\t\t\t\t\t\treturn r;\n' '\t\t\t\t\treturn r;\n'
mutant volume-start-ignored 'if ((r = bpb(&v, volume_lba)) != FT32_OK)' 'if ((r = bpb(&v, volume_lba & 0)) != FT32_OK)'
mutant extent-one-sector-short 'out->nsectors = nsect - headsec;' 'out->nsectors = nsect - headsec - 1;'
mutant extent-one-sector-late 'out->lba = first;' 'out->lba = first + 1;'

echo "fat32tail: $nmutants mutants of the locator rejected"
echo "fat32tail: PASS"
