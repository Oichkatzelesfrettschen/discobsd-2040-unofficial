#!/bin/sh
# Build the NetBSD v7fs core imported under sys/fs/v7fs, with NetBSD's own
# userland I/O backend and newfs library, against v7fs_core_test.c under the
# address and undefined-behavior sanitizers. A second build uses the fixed
# scratch pool, NDEBUG and the descriptor backend used by the target. Both
# exercise the PDP-11 and little-endian byte orders. Patch reversals and
# targeted mutants calibrate the regression checks. The host v7fs command
# runs in both byte orders against the same independent image reader.
#
# The imported code suppresses unused-parameter warnings for scratch_remain(),
# whose argument is used only with STATIC_BUFFER. Historical patch reversals
# retain their original alignment exemption; production variants use the
# alignment sanitizer and packed-member warnings.

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

cat >"$work/alignment-calibration.c" <<'EOF'
#include <stdint.h>
int main(int argc, char **argv)
{
	uint32_t storage[2] = {0};
	uint32_t *value;
	(void)argv;
	value = (uint32_t *)(void *)((unsigned char *)storage + (argc > 1));
	return *value != 0;
}
EOF
${CC:-cc} -std=c17 -Wall -Wextra -Werror -fsanitize=alignment \
    -fno-sanitize-recover=alignment "$work/alignment-calibration.c" \
    -o "$work/alignment-calibration" || fail "alignment calibration build"
"$work/alignment-calibration" || fail "aligned calibration"
if "$work/alignment-calibration" bad >"$work/alignment.log" 2>&1; then
	fail "alignment sanitizer accepted a misaligned load"
fi
grep -q 'runtime error:.*misaligned' "$work/alignment.log" ||
    fail "alignment calibration failed without a misaligned-load diagnostic"
echo "v7fs_core: alignment sanitizer rejected a misaligned load"

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
		# Word splitting permits the historical reversal flags.
		# shellcheck disable=SC2086
		compile ${legacy_flags:-} -Wno-unused-parameter -I"$1/sys/fs/v7fs" \
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

# Build the same reader with the descriptor backend and fixed scratch pool.
build_fixed()
{
	objects=
	mkdir -p "$2.obj"
	for name in endian superblock superblock_util inode inode_util datablock \
	    dirent io file file_util; do
		source=$1/sys/fs/v7fs/v7fs_$name.c
		object=$2.obj/v7fs_$name.o
		compile -DSTATIC_BUFFER -DNDEBUG -Wno-unused-parameter \
		    -I"$1/sys/fs/v7fs" -I"$1/sbin/newfs_v7fs" \
		    -c -o "$object" "$source" || fail "fixed build of $source"
		objects="$objects $object"
	done
	compile -DSTATIC_BUFFER -DNDEBUG \
	    -I"$1/sys/fs/v7fs" -I"$1/sbin/newfs_v7fs" \
	    -c -o "$2.obj/newfs.o" "$1/sbin/newfs_v7fs/main.c" ||
	    fail "fixed newfs build"
	compile -D_DEFAULT_SOURCE -DSTATIC_BUFFER -I"$1/sys/fs/v7fs" \
	    -I"$1/sbin/newfs_v7fs" -c -o "$2.obj/backend.o" \
	    "$compat/v7fs_io_fd.c" || fail "fixed descriptor backend build"
	compile -DSTATIC_BUFFER -I"$1/sys/fs/v7fs" -I"$1/sbin/newfs_v7fs" \
	    -c -o "$2.obj/test.o" "$here/v7fs_core_test.c" ||
	    fail "fixed oracle build"
	# shellcheck disable=SC2086  # $objects is a list of paths without spaces.
	${CC:-cc} -fsanitize=address,undefined -o "$2" "$2.obj/test.o" \
	    "$2.obj/newfs.o" "$2.obj/backend.o" $objects || fail "fixed link"
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
build_fixed "$topsrc" "$work/v7fs_fixed"
for order in pdp little; do
	"$work/v7fs_fixed" "$work/fixed-$order.img" "$order" ||
	    fail "fixed descriptor backend ($order)"
done
"$work/v7fs_fixed" "$work/fixed-regress.img" little regress ||
    fail "fixed descriptor backend regressions"
tree=$work/scratch-exhaustion
mkdir -p "$tree/sys/fs" "$tree/sbin"
cp -R "$v7fs" "$tree/sys/fs/"
cp -R "$newfs" "$tree/sbin/"
"$PYTHON" - "$tree/sys/fs/v7fs/v7fs_io.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
old = "assert(0);\n\t\tMEM_UNLOCK(fs);\n\t\treturn NULL;"
new = "assert(0);\n\t\tMEM_UNLOCK(fs);\n\t\treturn fs->scratch[0];"
if source.count(old) != 1:
    raise SystemExit("scratch exhaustion mutation anchor mismatch")
path.write_text(source.replace(old, new), encoding="utf-8")
PY
build_fixed "$tree" "$work/scratch-exhaustion.bin"
got=0
"$work/scratch-exhaustion.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-scratch-exhaustion:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "scratch exhaustion mutant"; }
echo "v7fs_core: scratch exhaustion mutant rejected"

mutant inode-list-read sys/fs/v7fs/v7fs_inode_util.c \
    'return EIO;\n\t\t}\n\t\tdi = (struct v7fs_inode_diskimage *)buf;' \
    'continue;\n\t\t}\n\t\tdi = (struct v7fs_inode_diskimage *)buf;'
got=0
"$work/inode-list-read.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-ilist-read:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "inode-list read mutant"; }
echo "v7fs_core: ignored inode-list read mutant rejected"

mutant replace-read sys/fs/v7fs/v7fs_file_util.c \
    'if (error == V7FS_ITERATOR_END)\n\t\treturn ENOENT;\n\treturn error > 0 ? error : EIO;' \
    'if (error == V7FS_ITERATOR_END)\n\t\treturn ENOENT;\n\treturn ENOENT;'
got=0
"$work/replace-read.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-replace-read:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "directory replacement read mutant"; }
echo "v7fs_core: hidden directory replacement error mutant rejected"

mutant lookup-number-read sys/fs/v7fs/v7fs_file_util.c \
    'if (ret == V7FS_ITERATOR_END)\n\t\treturn ENOENT;\n\treturn ret > 0 ? ret : EIO;' \
    'if (ret == V7FS_ITERATOR_END)\n\t\treturn ENOENT;\n\treturn ENOENT;'
got=0
"$work/lookup-number-read.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-lookup-number:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "inode-number lookup read mutant"; }
echo "v7fs_core: hidden inode-number lookup error mutant rejected"

mutant lookup-name-overread sys/fs/v7fs/v7fs_file_util.c \
    'v7fs_dirent_filename(p->buf, dir->name,\n\t\t\t\t    V7FS_NAME_MAX);' \
    'v7fs_dirent_filename(p->buf, dir->name,\n\t\t\t\t    strlen(dir->name));'
got=0
"$work/lookup-name-overread.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -ne 0 ] && grep -q 'AddressSanitizer: heap-buffer-overflow' \
    "$work/mutant.log" && grep -q 'lookup_by_number_subr' "$work/mutant.log" ||
    { cat "$work/mutant.log" >&2; fail "directory-name overread mutant"; }
echo "v7fs_core: directory-name overread mutant rejected"

mutant rename-inode-read sys/fs/v7fs/v7fs_file_util.c \
    'if ((error = v7fs_inode_load(fs, &inode, from_ino)) != 0)\n\t\treturn error;' \
    'if ((error = v7fs_inode_load(fs, &inode, from_ino)) != 0)\n\t\treturn ENOENT;'
got=0
"$work/rename-inode-read.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-rename:' "$work/mutant.log" ||
    { cat "$work/mutant.log" >&2; fail "rename inode read mutant"; }
echo "v7fs_core: hidden rename inode read error mutant rejected"

mutant rename-parent-read sys/fs/v7fs/v7fs_file_util.c \
    'if (!(buf = scratch_read(fs, blk)))\n\t\treturn EIO;\n\tstruct v7fs_dirent *dir' \
    'if (!(buf = scratch_read(fs, blk)))\n\t\treturn 0;\n\tstruct v7fs_dirent *dir'
got=0
"$work/rename-parent-read.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-rename:' "$work/mutant.log" ||
    { cat "$work/mutant.log" >&2; fail "rename parent read mutant"; }
echo "v7fs_core: hidden rename parent read error mutant rejected"

mutant rename-inode-write sys/fs/v7fs/v7fs_file_util.c \
    'if ((error = v7fs_inode_writeback(fs, &inode)) != 0)\n\t\t\treturn error;' \
    'if ((error = v7fs_inode_writeback(fs, &inode)) != 0)\n\t\t\treturn 0;'
got=0
"$work/rename-inode-write.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-rename:' "$work/mutant.log" ||
    { cat "$work/mutant.log" >&2; fail "rename inode write mutant"; }
echo "v7fs_core: hidden rename inode write error mutant rejected"

tree=$work/rename-hierarchy-late
mkdir -p "$tree/sys/fs" "$tree/sbin"
cp -R "$v7fs" "$tree/sys/fs/"
cp -R "$newfs" "$tree/sbin/"
"$PYTHON" - "$tree/sys/fs/v7fs/v7fs_file_util.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
validation = """\t/* Check directory hierarchy. t_vnops rename_dir(5) */
\tif (dir_move && (error = can_dirmove(fs, from_ino,
\t    parent_to->inode_number))) {
\t\tDPRINTF("dst '%.*s' is child dir of '%.*s'. error=%d\\n",
\t\t    (int)tolen, to, (int)fromlen, from, error);
\t\treturn error;
\t}
"""
publication = "\tif ((error = v7fs_directory_add_entry(fs, parent_to, from_ino, to,\n"
if source.count(validation) != 1 or source.count(publication) != 1:
    raise SystemExit("rename hierarchy mutation anchor mismatch")
source = source.replace(validation, "", 1)
source = source.replace(publication, validation + "\n" + publication, 1)
path.write_text(source, encoding="utf-8")
PY
build "$tree" "$work/rename-hierarchy-late.bin"
got=0
"$work/rename-hierarchy-late.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-rename-hierarchy:' \
    "$work/mutant.log" || {
	cat "$work/mutant.log" >&2
	fail "late rename hierarchy validation mutant"
}
echo "v7fs_core: late rename hierarchy validation mutant rejected"

# The local regression scenarios hold; reversals and mutants below must
# fail the corresponding scenario by name with the assertion status.
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
	if [ "$name" = no-inode-patch ]; then
		# Keep the checked release interface while reversing inode bounds.
		"$PYTHON" - "$tree/sys/fs/v7fs/v7fs_inode.c" "$v7fs/v7fs_inode.c" <<'PY'
from pathlib import Path
import sys

legacy, current = (Path(arg) for arg in sys.argv[1:])
old = legacy.read_text(encoding="utf-8")
new = current.read_text(encoding="utf-8")
start_old = old.index("\nvoid\nv7fs_inode_deallocate(")
start_new = new.index("\nint\nv7fs_inode_deallocate(")
end_marker = "\nvoid\nv7fs_inode_setup_memory_image("
end_old = old.index(end_marker, start_old)
end_new = new.index(end_marker, start_new)
legacy.write_text(old[:start_old] + new[start_new:end_new] + old[end_old:],
                  encoding="utf-8")
PY
	fi
	case $name in
	no-newfs-patch|no-superblock-patch)
		legacy_flags='-Wno-address-of-packed-member -fno-sanitize=alignment' ;;
	*) legacy_flags= ;;
	esac
	build "$tree" "$work/$name.bin"
	legacy_flags=
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
unpatched no-superblock-patch v7fs_superblock.patch regress-superblock regress-inode-read regress-superblock-writeback regress-volume-read
unpatched no-datablock-patch v7fs_datablock.patch regress-block-read regress-block-write regress-freeblock-refill regress-grow-writeback regress-grow-rollback regress-indirect-read regress-indirect-writeback regress-address-range regress-size-overflow regress-single-transition regress-double-transition
grep -q '^FAIL regress-single-transition: data block has no owner' \
    "$work/mutant.log" || fail "single-transition ownership oracle missed a lost block"
unpatched no-file-patch v7fs_file.patch regress-lookup-read regress-create-file-1 regress-create-directory-3 regress-remove-read regress-remove-write regress-remove-contract regress-rmdir-retry regress-unlink-inode-write

mutant rmdir-parent-restore sys/fs/v7fs/v7fs_file.c \
    'restore_error = v7fs_directory_add_checked(fs, parent_dir, target,\n\t\t    name, namelen, &unpublished);' \
    'restore_error = 0;\n\t\t(void)unpublished;'
got=0
"$work/rmdir-parent-restore.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-rmdir-parent-link:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "rmdir parent restore mutant"; }
echo "v7fs_core: rmdir parent restore mutant rejected"

mutant remove-last-block sys/fs/v7fs/v7fs_file.c \
    'if (lastblk == 0)\n\t\treturn EIO;' \
    'if (lastblk == (v7fs_daddr_t)-1)\n\t\treturn EIO;'
got=0
"$work/remove-last-block.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-remove-last-read:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "remove last block mutant"; }
echo "v7fs_core: failed last-block lookup mutant rejected"

mutant inode-release-error sys/fs/v7fs/v7fs_inode.c \
    'if ((error = v7fs_inode_writeback(fs, &inode)) != 0)\n\t\treturn error;' \
    'if ((error = v7fs_inode_writeback(fs, &inode)) != 0)\n\t\terror = 0;'
got=0
"$work/inode-release-error.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-inode-release:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "inode release mutant"; }
echo "v7fs_core: inode release error mutant rejected"

mutant empty-inode-cache sys/fs/v7fs/v7fs_inode.c \
    'if (sb->nfreeinode <= 0) {\n\t\terror = ENOSPC;\n\t\tgoto errexit;\n\t}' \
    'if (sb->nfreeinode <= 0) {\n\t\terror = EIO;\n\t\tgoto errexit;\n\t}'
got=0
"$work/empty-inode-cache.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-empty-inode-cache:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "empty inode cache mutant"; }
echo "v7fs_core: empty inode cache error mutant rejected"

mutant uncertain-rollback sys/fs/v7fs/v7fs_datablock.c \
    'if (!restored) {\n\t\t*uncertain = true;\n\t\treturn error;\n\t}' \
    'if (!restored) {\n\t\tgoto cleanup;\n\t}'
got=0
"$work/uncertain-rollback.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-uncertain-publication:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "uncertain publication mutant"; }
echo "v7fs_core: uncertain publication rollback mutant rejected"

mutant stale-direct-pointer sys/fs/v7fs/v7fs_datablock.c \
    'child = inode->addr[map.index[0]];\n\t\tinode->addr[map.index[0]] = 0;' \
    'child = inode->addr[map.index[0]];\n\t\tinode->addr[map.index[0]] = child;'
got=0
"$work/stale-direct-pointer.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-shrink-regrow:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "stale direct pointer mutant"; }
echo "v7fs_core: stale direct pointer mutant rejected"

mutant stale-allocated-slot sys/fs/v7fs/v7fs_datablock.c \
    'if (!is_free) {' \
    'if (false) {'
got=0
"$work/stale-allocated-slot.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-stale-allocated:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "allocated stale slot mutant"; }
echo "v7fs_core: allocated stale slot mutant rejected"

mutant stale-free-chain sys/fs/v7fs/v7fs_datablock.c \
    'next = count == 0 ? 0 : sb->freeblock[0];' \
    'next = 0;'
got=0
"$work/stale-free-chain.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-stale-free-chain:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "free-list traversal mutant"; }
echo "v7fs_core: stale free-list traversal mutant rejected"

mutant stale-multiple-slots sys/fs/v7fs/v7fs_datablock.c \
    'inode->addr[block] = 0;' \
    'if (block == old_blocks) inode->addr[block] = 0;'
got=0
"$work/stale-multiple-slots.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-stale-multiple:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "multiple stale slots mutant"; }
echo "v7fs_core: multiple stale slots mutant rejected"

mutant stale-successive-slots sys/fs/v7fs/v7fs_datablock.c \
    'for (block = old_blocks; new_blocks > old_blocks &&\n\t    block < V7FS_NADDR_DIRECT; block++) {' \
    'for (block = old_blocks; block < new_blocks &&\n\t    block < V7FS_NADDR_DIRECT; block++) {'
got=0
"$work/stale-successive-slots.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-stale-successive:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "successive stale slots mutant"; }
echo "v7fs_core: successive stale slots mutant rejected"

mutant inode-data-comparison sys/fs/v7fs/v7fs_inode.h \
    'if (left->filesize != right->filesize)' \
    'if (left->filesize != right->filesize || left->mode != right->mode)'
got=0
"$work/inode-data-comparison.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-inode-data-equal:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "inode data comparison mutant"; }
echo "v7fs_core: unrelated inode field mutant rejected"

mutant reformat-old-geometry sbin/newfs_v7fs/main.c \
    'if (!(buffer = scratch_read(fs, V7FS_SUPERBLOCK_SECTOR))) {' \
    'if (v7fs_superblock_load(fs) == EIO ||\n\t    !(buffer = scratch_read(fs, V7FS_SUPERBLOCK_SECTOR))) {'
build_fixed "$work/reformat-old-geometry" "$work/reformat-old-geometry-fixed.bin"
got=0
"$work/reformat-old-geometry-fixed.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-reformat-smaller:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "old geometry mutant"; }
echo "v7fs_core: old formatter geometry mutant rejected"

mutant shrink-size-restore sys/fs/v7fs/v7fs_datablock.c \
    'if (error != 0 && !detached) {' \
    'if (error != 0 && false) {'
got=0
"$work/shrink-size-restore.bin" "$work/mutant.img" little regress \
    >"$work/mutant.log" 2>&1 || got=$?
[ "$got" -eq 1 ] && grep -q '^FAIL regress-shrink-retry:' \
    "$work/mutant.log" && grep -q '^FAIL regress-shrink-root-retry:' \
    "$work/mutant.log" || { cat "$work/mutant.log" >&2; fail "shrink size restore mutant"; }
echo "v7fs_core: shrink size restore mutant rejected"

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
	cat >"$work/offset.c" <<'EOF'
#include "v7fs_io_fd.h"
_Static_assert(sizeof(off_t) == 4, "target off_t width");
_Static_assert(V7FS_FD_MAX_SECTORS == 4194303, "target sector bound");
_Static_assert(V7FS_FD_MAX_SECTORS * 512 <= V7FS_FD_OFF_MAX,
    "last accepted byte offset");
_Static_assert((V7FS_FD_MAX_SECTORS + 1) * 512 > V7FS_FD_OFF_MAX,
    "first rejected byte offset");
EOF
	arm-none-eabi-gcc -std=c17 -mcpu=cortex-m0plus -mthumb -fsyntax-only \
	    -Wall -Wextra -Werror -nostdinc -I"$topsrc/include" -I"$compat" \
	    "$work/offset.c" || fail "target byte-offset bounds"
	echo "v7fs_core: target byte-offset bounds verified"
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
    'memcpy((char *)dir[slot].name, filename, V7FS_NAME_MAX);' \
    'memcpy((char *)dir[slot].name, filename + 1, V7FS_NAME_MAX - 1);'
rejects "$work/dirent-name.bin" pdp core-hello reader-hello

# newfs records one block fewer per free-list block than it lists.
mutant freelist-count sbin/newfs_v7fs/main.c \
    'fb->nfreeblock = (*val16)(k);' \
    'fb->nfreeblock = (*val16)(k - 1);'
rejects "$work/freelist-count.bin" pdp freelist accounting

# usr.sbin/v7fs: the tool's host build, its commands, and its image.
"${MAKE:?set MAKE to bmake}" -C "$topsrc/usr.sbin/v7fs" HOSTCC="${CC:-cc}" \
    HOSTCFLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined" \
    host >"$work/tool-build.log" 2>&1 ||
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
cp "$work/pat/hello" "$work/refused.img"
status 1 "$tool" newfs "$work/refused.img" 127
cmp -s "$work/refused.img" "$work/pat/hello" ||
    fail "newfs modified a target with a refused geometry"
status 0 "$tool" newfs "$work/minimum.img" 128
status 0 "$tool" newfs "$work/reformat.img" 2048 64
status 0 "$tool" newfs "$work/reformat.img" 1024 64
status 0 "$tool" df "$work/reformat.img"
[ "$(awk '{print $2}' "$work/cmd.out")" = 1024 ] ||
    fail "newfs did not replace the larger superblock"
status 0 "$tool" newfs "$work/inode-limit.img" 128 600
cp "$work/pat/hello" "$work/inode-refused.img"
status 1 "$tool" newfs "$work/inode-refused.img" 128 608
cmp -s "$work/inode-refused.img" "$work/pat/hello" ||
    fail "newfs modified a target with an oversized inode list"

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

# A parent at an indirect-address boundary needs index blocks as well as its
# next data block. The capacity preflight must account for those blocks before
# the command allocates an inode or changes persistent bytes.
"$PYTHON" - "$topsrc/usr.sbin/v7fs/v7fs.c" \
    "$work/v7fs-parent-under-count.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = ("\tparent_size = v7fs_inode_filesize(&parent);\n"
       "\tneeded += blocks_for(parent_size + sizeof(struct v7fs_dirent)) -\n"
       "\t    blocks_for(parent_size);")
new = ("\tparent_size = v7fs_inode_filesize(&parent);\n"
       "\tif (parent_size % V7FS_BSIZE == 0)\n"
       "\t\tneeded++;")
if source.count(old) != 1:
    raise SystemExit("parent capacity mutation anchor count")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
compile -D_DEFAULT_SOURCE -DSTATIC_BUFFER \
    -include "$compat/netbsd_compat.h" -I"$compat/compat" \
    -I"$compat" -isystem "$v7fs" -isystem "$newfs" \
    -c "$work/v7fs-parent-under-count.c" \
    -o "$work/v7fs-parent-under-count.o" ||
    fail "parent capacity mutant compilation"
set --
for object in "$topsrc/usr.sbin/v7fs/host.obj/"*.o; do
	[ "$(basename "$object")" = v7fs.o ] || set -- "$@" "$object"
done
${CC:-cc} -fsanitize=address,undefined -o "$work/v7fs-parent-under-count" \
    "$@" "$work/v7fs-parent-under-count.o" ||
    fail "parent capacity mutant link"
for blocks in 10 138; do
	case "$blocks" in
	10)	expected_free=2028; payload_blocks=2010 ;;
	138)	expected_free=1899; payload_blocks=1882 ;;
	esac
	dd if=/dev/zero of="$work/pat/parent-$blocks" bs=512 \
	    count="$payload_blocks" 2>/dev/null
	for order in little pdp; do
		flag=
		[ "$order" = pdp ] && flag=-p
		img=$work/parent-$blocks-$order.img
		"$work/v7fs_core" "$img" "$order" parent "$blocks" ||
		    fail "parent boundary fixture ($blocks, $order)"
		# shellcheck disable=SC2086  # $flag is empty or one word.
		status 0 "$tool" $flag df "$img"
		[ "$(awk '{print $6}' "$work/cmd.out")" -eq "$expected_free" ] ||
		    fail "parent boundary free count ($blocks, $order)"
		cp "$img" "$work/parent-$blocks-$order.ref"
		# shellcheck disable=SC2086  # $flag is empty or one word.
		status 1 "$tool" $flag put "$img" "$work/pat/parent-$blocks" /boundary
		cmp -s "$img" "$work/parent-$blocks-$order.ref" ||
		    fail "parent capacity refusal changed the image ($blocks, $order)"
		cp "$work/parent-$blocks-$order.ref" \
		    "$work/parent-$blocks-$order-mutant.img"
		# shellcheck disable=SC2086  # $flag is empty or one word.
		status 1 "$work/v7fs-parent-under-count" $flag put \
		    "$work/parent-$blocks-$order-mutant.img" \
		    "$work/pat/parent-$blocks" /boundary
		cmp -s "$work/parent-$blocks-$order-mutant.img" \
		    "$work/parent-$blocks-$order.ref" &&
		    fail "parent capacity under-count mutant survived ($blocks, $order)"
	done
done
echo "v7fs_core: parent capacity includes direct and indirect growth"

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
ln -s self.img "$work/self.symlink"
status 1 "$tool" get "$work/self.img" /hello "$work/self.symlink"
cmp -s "$work/self.img" "$work/self.ref" || fail "get overwrote its image by a symlink"
status 0 "$tool" get "$work/self.img" /hello "$work/keep"
cmp -s "$work/keep" "$work/pat/hello" || fail "get failed to overwrite an ordinary file"
status 0 "$tool" get "$work/self.img" /hello "$work/created"
cmp -s "$work/created" "$work/pat/hello" || fail "get failed to create a destination"
cmp -s "$work/self.img" "$work/self.ref" || fail "get modified its image"
echo "v7fs_core: get checks aliases and copies to ordinary destinations"

if [ "$(uname -s)" = Linux ]; then
	cat >"$work/flush_wrap.c" <<'EOF'
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int __real_fsync(int);
int __real_close(int);
ssize_t __real_read(int, void *, size_t);
ssize_t __real_write(int, const void *, size_t);

ssize_t
__wrap_read(int descriptor, void *buffer, size_t size)
{
	if (getenv("V7FS_TEST_FAIL_SUPER_READ") != NULL &&
	    lseek(descriptor, 0, SEEK_CUR) == 512) {
		errno = EIO;
		return -1;
	}
	return __real_read(descriptor, buffer, size);
}

ssize_t
__wrap_write(int descriptor, const void *buffer, size_t size)
{
	const char *sector_text = getenv("V7FS_TEST_FAIL_SECTOR");
	const char *occurrence_text = getenv("V7FS_TEST_FAIL_OCCURRENCE");
	static long matching_writes;

	if (sector_text != NULL && occurrence_text != NULL &&
	    lseek(descriptor, 0, SEEK_CUR) == (off_t)strtol(sector_text, NULL, 10) * 512 &&
	    ++matching_writes == strtol(occurrence_text, NULL, 10)) {
		errno = EIO;
		return -1;
	}
	return __real_write(descriptor, buffer, size);
}

int
__wrap_fsync(int descriptor)
{
	if (getenv("V7FS_TEST_FAIL_FSYNC") != NULL) {
		errno = EIO;
		return -1;
	}
	return __real_fsync(descriptor);
}

int
__wrap_close(int descriptor)
{
	int result = __real_close(descriptor);

	if (getenv("V7FS_TEST_FAIL_CLOSE") != NULL) {
		errno = EBADF;
		return -1;
	}
	return result;
}
EOF
	${CC:-cc} -std=c17 -Wall -Wextra -Werror -c "$work/flush_wrap.c" \
	    -o "$work/flush_wrap.o" || fail "flush fault wrapper compilation"
	${CC:-cc} -fsanitize=address,undefined -Wl,--wrap=fsync -Wl,--wrap=close \
	    -Wl,--wrap=write -Wl,--wrap=read \
	    -o "$work/v7fs-flush" "$topsrc/usr.sbin/v7fs/host.obj/"*.o \
	    "$work/flush_wrap.o" || fail "flush fault wrapper link"
	cp "$work/reformat.img" "$work/super-read-fail.img"
	cp "$work/super-read-fail.img" "$work/super-read-fail.ref"
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SUPER_READ=1 "$work/v7fs-flush" \
	    newfs "$work/super-read-fail.img" 1024 64
	grep -q 'Input/output error' "$work/cmd.err" || fail "superblock read error"
	cmp -s "$work/super-read-fail.img" "$work/super-read-fail.ref" ||
	    fail "failed superblock read modified the image"
	for point in 2:1 2:2 2:3 2:4 56:1; do
		sector=${point%:*}
		occurrence=${point#*:}
		status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR="$sector" \
		    V7FS_TEST_FAIL_OCCURRENCE="$occurrence" "$work/v7fs-flush" \
		    newfs "$work/format-fail-$sector-$occurrence.img" 128
		grep -q 'Input/output error' "$work/cmd.err" ||
		    fail "formatter write failure $point"
	done
	echo "v7fs_core: formatter reports injected inode and free-list write errors"
	for occurrence in 1 2; do
		img=$work/put-data-fail-$occurrence.img
		status 0 "$tool" newfs "$img" 128
		status 0 "$tool" df "$img"
		before=$(cat "$work/cmd.out")
		status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=8 \
		    V7FS_TEST_FAIL_OCCURRENCE="$occurrence" "$work/v7fs-flush" \
		    put "$img" "$work/pat/hello" /fresh
		grep -q 'Input/output error' "$work/cmd.err" || fail "put data error"
		status 0 "$tool" df "$img"
		[ "$(cat "$work/cmd.out")" = "$before" ] ||
		    fail "put data failure $occurrence changed free counts"
		status 1 "$tool" cat "$img" /fresh
	done
	echo "v7fs_core: failed put data allocation and write leave no file"
	for occurrence in 3 4; do
		img=$work/rm-inode-fail-$occurrence.img
		status 0 "$tool" newfs "$img" 128
		status 0 "$tool" put "$img" "$work/pat/hello" /fresh
		status 0 "$tool" df "$img"
		before=$(cat "$work/cmd.out")
		status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 \
		    V7FS_TEST_FAIL_OCCURRENCE="$occurrence" "$work/v7fs-flush" \
		    rm "$img" /fresh
		grep -q 'Input/output error' "$work/cmd.err" || fail "rm inode error"
		status 0 "$tool" df "$img"
		[ "$(cat "$work/cmd.out")" = "$before" ] ||
		    fail "rm inode failure $occurrence changed free counts"
		status 0 "$tool" cat "$img" /fresh
		cmp -s "$work/cmd.out" "$work/pat/hello" ||
		    fail "rm inode failure $occurrence lost the file"
	done
	echo "v7fs_core: failed rm inode contraction preserves the file"
	img=$work/rm-inode-clear-fail.img
	status 0 "$tool" newfs "$img" 128
	status 0 "$tool" df "$img"
	empty_counts=$(cat "$work/cmd.out")
	status 0 "$tool" put "$img" "$work/pat/hello" /fresh
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 V7FS_TEST_FAIL_OCCURRENCE=5 \
	    "$work/v7fs-flush" rm "$img" /fresh
	grep -q 'Input/output error' "$work/cmd.err" || fail "rm inode clear error"
	status 0 "$tool" df "$img"
	[ "$(cat "$work/cmd.out")" = "$empty_counts" ] ||
	    fail "rm inode clear retry leaked ownership"
	status 1 "$tool" cat "$img" /fresh
	echo "v7fs_core: failed rm inode clear retries reclamation"
	img=$work/rm-partial-fail.img
	status 0 "$tool" newfs "$img" 2048 64
	status 0 "$tool" df "$img"
	empty_counts=$(cat "$work/cmd.out")
	status 0 "$tool" put "$img" "$work/pat/big" /fresh
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 V7FS_TEST_FAIL_OCCURRENCE=4 \
	    "$work/v7fs-flush" rm "$img" /fresh
	grep -q 'Input/output error' "$work/cmd.err" || fail "rm partial contraction error"
	status 0 "$tool" df "$img"
	[ "$(cat "$work/cmd.out")" = "$empty_counts" ] ||
	    fail "rm partial contraction retry leaked ownership"
	status 1 "$tool" cat "$img" /fresh
	echo "v7fs_core: failed partial rm contraction retries reclamation"
	compile -DSTATIC_BUFFER -DNDEBUG \
	    -I"$v7fs" -I"$newfs" -c -o "$work/newfs-original.o" \
	    "$work/no-newfs-patch/sbin/newfs_v7fs/main.c" ||
	    fail "original formatter compilation"
	set --
	for object in "$topsrc/usr.sbin/v7fs/host.obj/"*.o; do
		[ "$(basename "$object")" = newfs_main.o ] || set -- "$@" "$object"
	done
	${CC:-cc} -fsanitize=address,undefined -Wl,--wrap=fsync -Wl,--wrap=close \
	    -Wl,--wrap=write -Wl,--wrap=read -o "$work/v7fs-original-newfs" "$@" \
	    "$work/newfs-original.o" "$work/flush_wrap.o" ||
	    fail "original formatter link"
	status 0 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 V7FS_TEST_FAIL_OCCURRENCE=1 \
	    "$work/v7fs-original-newfs" newfs "$work/original-format-fail.img" 128
	echo "v7fs_core: original formatter accepted a failed inode-list write"
	status 1 env LC_ALL=C V7FS_TEST_FAIL_FSYNC=1 V7FS_TEST_FAIL_CLOSE=1 \
	    "$work/v7fs-flush" newfs "$work/flush-new.img" 128
	grep -q 'Input/output error' "$work/cmd.err" || fail "newfs flush error"
	for command in put mkdir rm; do
		cp "$work/tool-little.img" "$work/flush-$command.img"
		case "$command" in
		put) set -- put "$work/flush-$command.img" "$work/pat/hello" /fresh ;;
		mkdir) set -- mkdir "$work/flush-$command.img" /fresh ;;
		rm) set -- rm "$work/flush-$command.img" /hello ;;
		esac
		status 1 env LC_ALL=C V7FS_TEST_FAIL_FSYNC=1 \
		    "$work/v7fs-flush" "$@"
		grep -q 'Input/output error' "$work/cmd.err" || fail "$command flush error"
	done
	cp "$work/tool-little.img" "$work/flush-close.img"
	status 1 env LC_ALL=C V7FS_TEST_FAIL_CLOSE=1 "$work/v7fs-flush" \
	    put "$work/flush-close.img" "$work/pat/hello" /fresh
	grep -q 'Bad file descriptor' "$work/cmd.err" || fail "put source close error"
	cp "$work/tool-little.img" "$work/flush-existing.img"
	status 1 env LC_ALL=C V7FS_TEST_FAIL_FSYNC=1 V7FS_TEST_FAIL_CLOSE=1 \
	    "$work/v7fs-flush" put "$work/flush-existing.img" "$work/pat/hello" /hello
	grep -q 'File exists' "$work/cmd.err" || fail "operation error lost during flush"
	echo "v7fs_core: injected flush and close failures preserve the first error"
	"$PYTHON" - "$topsrc/usr.sbin/v7fs/v7fs.c" "$work/v7fs-no-flush.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
for old in ("if (fsync(im->fd) != 0 && error == 0)",
            "if (error == 0 && fsync(fd) != 0)"):
    if source.count(old) != 1:
        raise SystemExit(f"flush mutation anchor count: {old}")
    source = source.replace(old, old.replace("if (", "if (false && ", 1))
Path(sys.argv[2]).write_text(source, encoding="utf-8")
PY
	${CC:-cc} -O1 -g -std=c17 -Wall -Wextra -Werror -Wpedantic \
	    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	    -D_DEFAULT_SOURCE -DV7FS_EI -DSTATIC_BUFFER \
	    -include "$compat/netbsd_compat.h" -I"$compat/compat" \
	    -I"$compat" -isystem "$v7fs" -isystem "$newfs" \
	    -c "$work/v7fs-no-flush.c" -o "$work/v7fs-no-flush.o" ||
	    fail "flush mutant compilation"
	set --
	for object in "$topsrc/usr.sbin/v7fs/host.obj/"*.o; do
		[ "$(basename "$object")" = v7fs.o ] || set -- "$@" "$object"
	done
	${CC:-cc} -fsanitize=address,undefined -Wl,--wrap=fsync -Wl,--wrap=close \
	    -Wl,--wrap=write -Wl,--wrap=read \
	    -o "$work/v7fs-no-flush" "$@" "$work/v7fs-no-flush.o" \
	    "$work/flush_wrap.o" || fail "flush mutant link"
	status 0 env LC_ALL=C V7FS_TEST_FAIL_FSYNC=1 \
	    "$work/v7fs-no-flush" newfs "$work/mutant-flush-new.img" 128
	for command in put mkdir rm; do
		cp "$work/tool-little.img" "$work/mutant-flush-$command.img"
		case "$command" in
		put) set -- put "$work/mutant-flush-$command.img" "$work/pat/hello" /fresh ;;
		mkdir) set -- mkdir "$work/mutant-flush-$command.img" /fresh ;;
		rm) set -- rm "$work/mutant-flush-$command.img" /hello ;;
		esac
		status 0 env LC_ALL=C V7FS_TEST_FAIL_FSYNC=1 "$work/v7fs-no-flush" "$@"
	done
	echo "v7fs_core: suppressed flush failures rejected for all four commands"
	"$PYTHON" - "$topsrc/usr.sbin/v7fs/v7fs.c" "$work/v7fs-no-cleanup.c" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
mutations = {
    "cleanup_error = cmd_rm(im, path);": "cleanup_error = 0;",
    "restore_error = v7fs_directory_add_entry(im->fs, &parent, ino,\n"
    "\t\t    leaf, strlen(leaf));": "restore_error = 0;",
    "if (!retryable)\n\t\t\t\treturn error;":
    "if (true)\n\t\t\t\treturn error;",
}
for old, new in mutations.items():
    if source.count(old) != 1:
        raise SystemExit(f"cleanup mutation anchor count: {old}")
    source = source.replace(old, new)
Path(sys.argv[2]).write_text(source, encoding="utf-8")
PY
	${CC:-cc} -O1 -g -std=c17 -Wall -Wextra -Werror -Wpedantic \
	    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	    -D_DEFAULT_SOURCE -DV7FS_EI -DSTATIC_BUFFER \
	    -include "$compat/netbsd_compat.h" -I"$compat/compat" \
	    -I"$compat" -isystem "$v7fs" -isystem "$newfs" \
	    -c "$work/v7fs-no-cleanup.c" -o "$work/v7fs-no-cleanup.o" ||
	    fail "cleanup mutant compilation"
	set --
	for object in "$topsrc/usr.sbin/v7fs/host.obj/"*.o; do
		[ "$(basename "$object")" = v7fs.o ] || set -- "$@" "$object"
	done
	${CC:-cc} -fsanitize=address,undefined -Wl,--wrap=fsync -Wl,--wrap=close \
	    -Wl,--wrap=write -Wl,--wrap=read -o "$work/v7fs-no-cleanup" "$@" \
	    "$work/v7fs-no-cleanup.o" "$work/flush_wrap.o" ||
	    fail "cleanup mutant link"
	img=$work/mutant-put-cleanup.img
	status 0 "$tool" newfs "$img" 128
	status 0 "$tool" df "$img"
	before=$(cat "$work/cmd.out")
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=8 V7FS_TEST_FAIL_OCCURRENCE=1 \
	    "$work/v7fs-no-cleanup" put "$img" "$work/pat/hello" /fresh
	status 0 "$tool" df "$img"
	[ "$(cat "$work/cmd.out")" != "$before" ] || fail "put cleanup mutant survived"
	img=$work/mutant-rm-cleanup.img
	status 0 "$tool" newfs "$img" 128
	status 0 "$tool" put "$img" "$work/pat/hello" /fresh
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 V7FS_TEST_FAIL_OCCURRENCE=3 \
	    "$work/v7fs-no-cleanup" rm "$img" /fresh
	status 1 "$tool" cat "$img" /fresh
	img=$work/mutant-rm-partial-cleanup.img
	status 0 "$tool" newfs "$img" 2048 64
	status 0 "$tool" df "$img"
	before=$(cat "$work/cmd.out")
	status 0 "$tool" put "$img" "$work/pat/big" /fresh
	status 1 env LC_ALL=C V7FS_TEST_FAIL_SECTOR=2 V7FS_TEST_FAIL_OCCURRENCE=4 \
	    "$work/v7fs-no-cleanup" rm "$img" /fresh
	status 0 "$tool" df "$img"
	[ "$(cat "$work/cmd.out")" != "$before" ] ||
	    fail "partial rm cleanup mutant survived"
	echo "v7fs_core: skipped put and rm cleanup mutants rejected"
else
	echo "v7fs_core: linker-wrapped flush fault tests skipped on $(uname -s)"
fi

echo "v7fs_core: PASS (both byte orders, both backends, calibrated regressions)"
