#!/bin/sh
# Measure what NetBSD rump kernel layers and file-system components cost
# when cross-compiled for the RP2040 Cortex-M0+, and which kernel symbols
# each component needs from its host.
#
# usage: measure.sh [NETBSD_SRC [OUTDIR]]
#
# NETBSD_SRC is a NetBSD source checkout (the directory holding sys/ and
# usr.sbin/). Without the argument the script takes $NETBSD_SRC from the
# environment, then BSD_Workspace/netbsd-src beside the primary checkout
# (the directory holding the repository's common .git, so a worktree finds
# the same sibling), and exits 2 when none exists. OUTDIR
# receives objects, logs and results.tsv; without it the script makes a
# fresh directory under ${TMPDIR:-/tmp} and prints its path. Every object
# is compiled only, never linked into an image: a size here bounds code
# before link-time garbage collection and excludes libkern, libc and the
# rumpuser hypercall layer. Sizes vary with the compiler version, which
# results.tsv records beside the NetBSD revision.
#
# Steps:
#   1. Each rump layer (rumpkern, rumpvfs, rumpnet) and component (v7fs,
#      msdos, net, netinet): parse SRCS from its NetBSD Makefile, resolve
#      each file under sys/, compile it with -Os for cortex-m0plus, sum
#      text, data and bss over the objects that compile.
#   2. The v7fs core: the ten sys/fs/v7fs files that NetBSD's makefs also
#      compiles in userland, combined with ld -r; list its undefined
#      symbols and those of the kernel I/O backend v7fs_io_kern.c.
#   3. The msdosfs files makefs reuses unchanged: size and the count of
#      vnode-layer symbols each needs.
#   4. Link-set boundary symbols: link one object whose section is named
#      .rodata.nanorump_comp and one whose section is nanorump_comp, and
#      record whether ld defines __start_nanorump_comp for each.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
PRIMARY=$ROOT
if common=$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null); then
	PRIMARY=$(dirname "$common")
fi
NB=${1:-${NETBSD_SRC:-$PRIMARY/../BSD_Workspace/netbsd-src}}
if [ ! -d "$NB/sys/rump" ]; then
	echo "usage: measure.sh [NETBSD_SRC [OUTDIR]]: no NetBSD checkout at $NB" >&2
	exit 2
fi
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/rump-measure.XXXXXX")}
echo "measure.sh: netbsd-src $NB, output $OUT" >&2
CC_ARM=${CC_ARM:-arm-none-eabi-gcc}
LD_ARM=${LD_ARM:-arm-none-eabi-ld}
NM_ARM=${NM_ARM:-arm-none-eabi-nm}
SIZE_ARM=${SIZE_ARM:-arm-none-eabi-size}

NB=$(cd "$NB" && pwd)
S=$NB/sys
R=$S/rump
[ -d "$R/librump/rumpkern" ] || { echo "measure.sh: $R is not a rump tree" >&2; exit 2; }
mkdir -p "$OUT/inc"
OUT=$(cd "$OUT" && pwd)
ln -sfn "$S/arch/evbarm/include" "$OUT/inc/machine"
ln -sfn "$S/arch/arm/include" "$OUT/inc/arm"

VNODE_RE='vop_|VOP_|genfs|vcache|vref|vrele|vput|uvm|ubc|kauth|namei|cache_'

cc_kern() {
	"$CC_ARM" -c -Os -mcpu=cortex-m0plus -mthumb -ffreestanding \
	    -fno-common -w -nostdinc -D_KERNEL -D_RUMPKERNEL -D__NetBSD__ \
	    -DRUMP_CURLWP=RUMP_CURLWP_HYPERCALL \
	    -I"$R/librump/rumpkern" -I"$R/librump/rumpvfs" -I"$R/include" \
	    -I"$R/include/opt" -I"$OUT/inc" -I"$S/../common/include" \
	    -I"$S/arch" -I"$S" -I"$S/lib/libkern" "$@"
}

# Print the .c names listed in the SRCS assignments of Makefile $1.
srcs_of() {
	awk '/^SRCS/ { f = 1 } f { print } /^$/ { f = 0 }' "$1" |
	    tr -s " \t\\\\" '\n' | grep '\.c$' | sort -u
}

# Print the first path, in sorted order, of file $1 under the directories
# that follow it.
resolve() {
	name=$1
	shift
	find "$@" -name "$name" -type f 2>/dev/null | sort | head -n 1
}

# compile_set LABEL MAKEFILE DIR...: compile every resolvable source and
# append one results row.
compile_set() {
	label=$1
	mk=$2
	shift 2
	d=$OUT/obj/$label
	mkdir -p "$d"
	ok=0
	fail=0
	: >"$d/compile.log"
	for f in $(srcs_of "$mk"); do
		p=$(resolve "$f" "$@")
		[ -n "$p" ] || continue
		if cc_kern -o "$d/${f%.c}.o" "$p" >>"$d/compile.log" 2>&1; then
			ok=$((ok + 1))
		else
			fail=$((fail + 1))
			echo "FAIL $p" >>"$d/compile.log"
		fi
	done
	sizes=$("$SIZE_ARM" -t "$d"/*.o | tail -n 1 | awk '{ print $1 "\t" $2 "\t" $3 }')
	printf '%s\t%s\t%s\t%s\n' "$label" "$ok" "$fail" "$sizes" >>"$OUT/results.tsv"
}

rev=unknown
if git -C "$NB" rev-parse --short=11 HEAD >/dev/null 2>&1; then
	rev=$(git -C "$NB" rev-parse --short=11 HEAD)
fi
{
	printf '# netbsd-src %s\n' "$rev"
	printf '# %s\n' "$("$CC_ARM" --version | head -n 1)"
	printf 'set\tcompiled\tfailed\ttext\tdata\tbss\n'
} >"$OUT/results.tsv"

compile_set rumpkern "$R/librump/rumpkern/Makefile.rumpkern" \
    "$R/librump/rumpkern" "$S/kern" "$S/uvm" "$S/lib/libkern" "$S/dev" \
    "$S/crypto" "$S/secmodel"
compile_set rumpvfs "$R/librump/rumpvfs/Makefile.rumpvfs" \
    "$R/librump/rumpvfs" "$S/kern" "$S/miscfs" "$S/dev"
compile_set rumpnet "$R/librump/rumpnet/Makefile.rumpnet" \
    "$R/librump/rumpnet" "$S/kern" "$S/net" "$S/netinet"
compile_set net "$R/net/lib/libnet/Makefile" "$R/net/lib/libnet" "$S/net"
compile_set netinet "$R/net/lib/libnetinet/Makefile.inc" \
    "$R/net/lib/libnetinet" "$S/netinet"
compile_set v7fs "$R/fs/lib/libv7fs/Makefile" "$S/fs/v7fs"
compile_set msdos "$R/fs/lib/libmsdos/Makefile" "$S/fs/msdosfs"

# The v7fs core is the source set makefs compiles in userland, minus the
# userland I/O backend v7fs_io_user.c.
v=$OUT/obj/v7fs
core=
for f in endian superblock superblock_util inode inode_util datablock \
    dirent io file file_util; do
	core="$core $v/v7fs_$f.o"
done
# shellcheck disable=SC2086  # $core is a list of paths without spaces.
"$LD_ARM" -r -o "$OUT/v7fs-core.o" $core
"$NM_ARM" -u "$OUT/v7fs-core.o" | awk '{ print $2 }' | sort -u >"$OUT/v7fs-core.undef"
"$NM_ARM" -u "$v/v7fs_io_kern.o" | awk '{ print $2 }' | sort -u >"$OUT/v7fs-io-kern.undef"
printf 'v7fs-core\t10\t0\t%s\n' \
    "$("$SIZE_ARM" "$OUT/v7fs-core.o" | awk 'NR == 2 { print $1 "\t" $2 "\t" $3 }')" \
    >>"$OUT/results.tsv"

for f in msdosfs_fat msdosfs_conv msdosfs_lookup; do
	o=$OUT/obj/msdos/$f.o
	printf '# %s text=%s vnode_symbols=%s\n' "$f" \
	    "$("$SIZE_ARM" "$o" | awk 'NR == 2 { print $1 }')" \
	    "$("$NM_ARM" -u "$o" | grep -cE "$VNODE_RE" || true)" \
	    >>"$OUT/results.tsv"
done
for set in v7fs msdos; do
	"$LD_ARM" -r -o "$OUT/$set-all.o" "$OUT/obj/$set"/*.o
	printf '# %s whole-component undefined symbols=%s vnode_symbols=%s\n' \
	    "$set" \
	    "$("$NM_ARM" -u "$OUT/$set-all.o" | wc -l | tr -d ' ')" \
	    "$("$NM_ARM" -u "$OUT/$set-all.o" | grep -cE "$VNODE_RE" || true)" \
	    >>"$OUT/results.tsv"
done

# Link-set probe. The script places .rodata* the way kern.ldscript does.
l=$OUT/linkset
mkdir -p "$l"
printf 'SECTIONS { .text 0x10000000 : { *(.text*) *(.rodata*) } }\n' >"$l/k.ld"
for sec in .rodata.nanorump_comp nanorump_comp; do
	cat >"$l/t.c" <<EOF
struct c { int x; };
static const struct c a __attribute__((section("$sec"), used)) = { 1 };
extern const struct c __start_nanorump_comp[], __stop_nanorump_comp[];
int main(void) { return (int)(__stop_nanorump_comp - __start_nanorump_comp); }
void _start(void) { main(); }
EOF
	"$CC_ARM" -mcpu=cortex-m0plus -mthumb -Os -c "$l/t.c" -o "$l/t.o"
	if "$LD_ARM" -T "$l/k.ld" -nostdlib "$l/t.o" -o "$l/t.elf" \
	    >"$l/ld.log" 2>&1; then
		res=defined
	else
		res=undefined
	fi
	printf '# linkset section=%s __start_nanorump_comp=%s\n' "$sec" "$res" \
	    >>"$OUT/results.tsv"
done

cat "$OUT/results.tsv"
