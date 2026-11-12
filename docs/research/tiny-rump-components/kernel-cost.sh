#!/bin/sh
# Flash cost of a kernel-linked v7fs core against the RP2040 kernels'
# measured headroom, and the size of the 2.11BSD inode layer an adapter
# would have to parallel.
#
# Usage: kernel-cost.sh [OUTDIR]
#
# Run from a tree where "bmake MACHINE=rp2040 build" has linked the four
# kernels. The script compiles the ten vnode-free v7fs core files the way
# a kernel would select them (native byte order only, the core's static
# scratch blocks, its kernel locking, NDEBUG) and v7fs_io_bio.c, the
# buffer-cache backend prototype beside this script, with the PICO
# kernel's own code-generation flags into OUTDIR ($KERNEL_COST_OUTDIR, or
# a fresh mktemp directory; it must be absent or empty). It prints each
# object's size, the undefined symbols of the set and which of them the
# PICO kernel already defines, check-kernel-headroom's report, and the
# sizes of the kernel's ufs_*.o objects, which hold the inode, block-map,
# allocation and directory code a v7fs mount would replace per mount.
set -eu

PYTHON=${PYTHON:?set PYTHON to the intended interpreter}
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
pico=$top/sys/arch/rp2040/compile/PICO
[ -f "$pico/unix.elf" ] || { echo "kernel-cost.sh: build the kernels first ($pico/unix.elf)" >&2; exit 2; }

out=${1:-${KERNEL_COST_OUTDIR:-}}
if [ -z "$out" ]; then
	out=$(mktemp -d "${TMPDIR:-/tmp}/v7fs-kernel-cost.XXXXXX")
elif [ -e "$out" ] && [ -n "$(ls -A "$out")" ]; then
	echo "kernel-cost.sh: $out is not empty" >&2
	exit 2
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)

var() {
	(cd "$pico" && bmake MACHINE=rp2040 PYTHON="$PYTHON" -V "\${$1}")
}
cc=$(var CC)
cmach=$(var CMACHFLAGS)
compat=$top/usr.sbin/v7fs

# The kernel-side core: no V7FS_EI (one byte order), STATIC_BUFFER (three
# 512-byte scratch blocks in .bss), V7FS_LOCK (the SUPERB, ILIST and MEM
# lock calls) and NDEBUG; DiscoBSD's libc headers stand in for the
# userland ones the files name outside NetBSD's _KERNEL.
core="endian superblock superblock_util inode inode_util datablock dirent io
file file_util"
# shellcheck disable=SC2086 # the flag variables are word lists
for f in $core; do
	$cc $cmach -Os -Wno-unused-parameter -DSTATIC_BUFFER -DV7FS_LOCK \
	    -DNDEBUG -nostdinc -include "$compat/netbsd_compat.h" \
	    -I"$compat/compat" -I"$compat/compat/discobsd" \
	    -I"$top/sys/fs/v7fs" -I"$top/include" \
	    -c -o "$out/v7fs_$f.o" "$top/sys/fs/v7fs/v7fs_$f.c"
done
# shellcheck disable=SC2086
$cc $cmach -Os -DKERNEL -DSTATIC_BUFFER -DV7FS_LOCK -DNDEBUG -nostdinc \
    -include "$compat/netbsd_compat.h" -I"$compat/compat" \
    -I"$compat/compat/discobsd" -I"$top/sys/fs/v7fs" -I"$top/include" \
    -c -o "$out/v7fs_io_bio.o" "$here/v7fs_io_bio.c"

arm-none-eabi-gcc --version | head -n 1
echo "kernel-side v7fs objects:"
arm-none-eabi-size -t "$out"/*.o
arm-none-eabi-nm -u "$out"/*.o | awk 'NF == 2 { print $2 }' | sort -u >"$out/undefined.txt"
arm-none-eabi-nm --defined-only "$out"/*.o | awk 'NF == 3 { print $3 }' | sort -u >"$out/defined.txt"
comm -23 "$out/undefined.txt" "$out/defined.txt" >"$out/needed.txt"
arm-none-eabi-nm --defined-only "$pico/unix.elf" | awk '{ print $3 }' | sort -u >"$out/kernel.txt"
echo "symbols the set needs: $(wc -l <"$out/needed.txt")"
echo "  defined by the PICO kernel: $(comm -12 "$out/needed.txt" "$out/kernel.txt" | tr '\n' ' ')"
echo "  missing from it: $(comm -23 "$out/needed.txt" "$out/kernel.txt" | tr '\n' ' ')"

echo "check-kernel-headroom:"
(cd "$top" && bmake MACHINE=rp2040 PYTHON="$PYTHON" check-kernel-headroom)

echo "2.11BSD inode layer in the PICO kernel (ufs_*.o):"
arm-none-eabi-size -t "$pico"/ufs_*.o
