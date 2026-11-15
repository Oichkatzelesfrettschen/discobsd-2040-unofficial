#!/bin/sh
#
# Gate for the sparse flash image (docs/research/flash-storage-expectation-
# plan.md, P8b). tools/flashimg leaves the free blocks of each UFS partition
# out of the Dhara map, except the blocks that chain the free list. Over an
# image fsutil builds, the region flashimg writes is read back through the
# vendored Dhara map (readback.c) and compared with the image by an
# independent free-list walk (compare.py): every block outside the free
# list and every chain block reads back unchanged, every other free block
# reads 0xff, the map holds exactly the written blocks, and fsutil --check
# reports the readback exactly as it reports the image. fsutil --check exits
# 0 whatever it finds and prints its findings between the phase lines, so
# the comparison is of its whole report. A flashimg that also skips chain blocks must fail
# the chain comparison and the checker; a flashimg that writes every block,
# the earlier behavior, must fail the live-share and unwritten-block checks.
#
# Usage: check.sh FSUTIL

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

fsutil=${1:?usage: check.sh FSUTIL}
topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
here=$topsrc/tests/rp2040/flash_sparse
dhara=$topsrc/sys/arch/rp2040/dhara
flashimg=$topsrc/tools/flashimg/flashimg.c
work=$(mktemp -d "${TMPDIR:-/tmp}/flash-sparse.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	find "$work" -depth -type f -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

fail()
{
	echo "flash_sparse: FAIL: $*" >&2
	exit 1
}

# build SOURCE BINARY: SOURCE linked with the vendored Dhara, as strict C17
# under the address and undefined-behavior sanitizers.
build()
{
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -fsanitize=address,undefined \
	    -I"$dhara" -I"$topsrc/sys/arch" -o "$2" "$1" \
	    "$dhara/error.c" "$dhara/journal.c" "$dhara/map.c" ||
		fail "strict C17 sanitizer build of $1"
}

# mutate SOURCE OUTPUT OLD NEW: OUTPUT is SOURCE with its one OLD replaced.
mutate()
{
	"$PYTHON" - "$1" "$2" "$3" "$4" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old, new = sys.argv[3], sys.argv[4]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count differs: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

# The image: a 600 KiB filesystem, so its free list runs through chain
# blocks, holding small files and one large enough for an indirect block.
mkdir "$work/root" "$work/root/dir"
"$PYTHON" - "$work/root" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
for index in range(12):
    (root / "dir" / f"f{index}").write_bytes(bytes([65 + index]) * (300 * index + 7))
(root / "big").write_bytes(bytes(range(256)) * 160)
PY
FSUTIL_CLOCK=1700000000 "$fsutil" --repartition=fs=600k "$work/disk.img" \
    >/dev/null || fail "fsutil --repartition"
(cd "$work/root" && FSUTIL_CLOCK=1700000000 "$fsutil" --new --partition=1 \
    "$work/disk.img" .) >/dev/null || fail "fsutil --new"
"$fsutil" --check --partition=1 "$work/disk.img" >"$work/disk.check" 2>&1 ||
	fail "the source image does not pass fsutil --check"

build "$here/readback.c" "$work/readback"

# trace FLASHIMG LABEL: flashimg, readback and comparison; the comparison's
# status, then the checker's, land in LABEL.status.
trace()
{
	"$1" "$work/disk.img" "$work/$2.region" >"$work/$2.flashimg" 2>&1 ||
		{ cat "$work/$2.flashimg" >&2; fail "$2: flashimg failed"; }
	units=$(( ($(wc -c <"$work/disk.img") + 1023) / 1024 ))
	capacity=$(sed -n 's/.*capacity \([0-9]*\) sectors.*/\1/p' \
	    "$work/$2.flashimg" | head -n 1)
	"$work/readback" "$work/$2.region" "$units" "$work/$2.readback" \
	    >"$work/$2.live" || fail "$2: readback failed"
	live=$(sed -n 's/^live //p' "$work/$2.live")
	compare_status=0
	"$PYTHON" "$here/compare.py" "$work/disk.img" "$work/$2.readback" \
	    "$live" "$capacity" >"$work/$2.compare" 2>&1 || compare_status=$?
	check_status=0
	"$fsutil" --check --partition=1 "$work/$2.readback" \
	    >"$work/$2.fsck" 2>&1 || check_status=$?
	cmp -s "$work/disk.check" "$work/$2.fsck" || check_status=1
	echo "$compare_status $check_status" >"$work/$2.status"
}

build "$flashimg" "$work/flashimg"
trace "$work/flashimg" sparse
read -r compare_status check_status <"$work/sparse.status"
cat "$work/sparse.flashimg" "$work/sparse.compare"
[ "$compare_status" -eq 0 ] || fail "sparse image readback differs"
[ "$check_status" -eq 0 ] ||
	{ diff "$work/disk.check" "$work/sparse.fsck" >&2 || true
	  fail "fsutil --check reports the readback differently"; }
grep -q "chain blocks" "$work/sparse.compare" &&
	! grep -q " 0 chain blocks" "$work/sparse.compare" ||
	fail "the image has no free-list chain block to exercise"

# A flashimg that leaves the chain blocks out as well loses the free list.
mutate "$flashimg" "$work/skip-chain.c" \
	'			state[base + b] = i == 0 ? UNIT_CHAIN : UNIT_FREE;' \
	'			state[base + b] = UNIT_FREE;'
build "$work/skip-chain.c" "$work/flashimg-skip-chain"
trace "$work/flashimg-skip-chain" skip-chain
read -r compare_status check_status <"$work/skip-chain.status"
[ "$compare_status" -eq 1 ] && grep -q "^FAIL chain-readback:" \
    "$work/skip-chain.compare" ||
	{ cat "$work/skip-chain.compare" >&2; fail "skipped chain blocks were accepted"; }
[ "$check_status" -ne 0 ] ||
	fail "fsutil --check accepted a readback without its chain blocks"

# A flashimg that writes every block, as before, keeps the free blocks live.
mutate "$flashimg" "$work/write-all.c" \
	'		if (state[unit] == UNIT_FREE)
			continue;
' ''
build "$work/write-all.c" "$work/flashimg-write-all"
trace "$work/flashimg-write-all" write-all
read -r compare_status check_status <"$work/write-all.status"
[ "$compare_status" -eq 1 ] &&
    grep -q "^FAIL live-share:" "$work/write-all.compare" &&
    grep -q "^FAIL free-unwritten:" "$work/write-all.compare" ||
	{ cat "$work/write-all.compare" >&2; fail "a fully written image passed the live-share check"; }

# A partition entry shorter than its filesystem is refused: the free list
# would name units beyond the declared extent. The entry is the one whose
# type byte is 0xb7; its length is set to 200 sectors.
"$PYTHON" - "$work/disk.img" "$work/short.img" <<'PY'
import sys
from pathlib import Path

image = bytearray(Path(sys.argv[1]).read_bytes())
entries = [446 + 16 * i for i in range(4) if image[446 + 16 * i + 4] == 0xB7]
if len(entries) != 1:
    raise SystemExit(f"expected one UFS partition entry, found {len(entries)}")
image[entries[0] + 12 : entries[0] + 16] = (200).to_bytes(4, "little")
Path(sys.argv[2]).write_bytes(image)
PY
if "$work/flashimg" "$work/short.img" "$work/short.region" \
    >"$work/short.flashimg" 2>&1; then
	fail "a filesystem longer than its partition entry was accepted"
fi
grep -q "does not fit its partition" "$work/short.flashimg" ||
	{ cat "$work/short.flashimg" >&2; fail "short partition: wrong refusal"; }

# A flashimg that bounds the filesystem by the image alone accepts it.
mutate "$flashimg" "$work/image-bound.c" \
	'		extent = word(entry, 12) / 2;
		if (extent > units - base)
			extent = units - base;' \
	'		extent = units - base;'
build "$work/image-bound.c" "$work/flashimg-image-bound"
"$work/flashimg-image-bound" "$work/short.img" "$work/short.region" \
    >"$work/short-mutant.flashimg" 2>&1 ||
	fail "the image-bound mutant refused the short partition"

echo "flash_sparse: PASS (used and chain blocks read back, free blocks" \
	"unwritten, live share, fsutil --check; short partition entry" \
	"refused; chain-skipping, write-everything and image-bound" \
	"mutations rejected)"
