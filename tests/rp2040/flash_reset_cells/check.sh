#!/bin/sh

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
here=$topsrc/tests/rp2040/flash_reset_cells
model=$topsrc/tests/rp2040/flash_checkpoint
dhara=$topsrc/sys/arch/rp2040/dhara
flash=$topsrc/sys/arch/rp2040/dev/flash.c
swap=$topsrc/sys/arch/rp2040/dev/flash_swap.c
cells=$here/cells.tsv
work=$(mktemp -d "${TMPDIR:-/tmp}/flash-reset-cells.XXXXXX")
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
	echo "flash_reset_cells: FAIL: $*" >&2
	exit 1
}

# The shims of tests/rp2040/flash_checkpoint: the kernel's <sys/stdint.h> is
# the host's <stdint.h>, and <sys/ioctl.h> gives the command encoding alone.
mkdir -p "$work/shim/sys"
echo '#include <stdint.h>' >"$work/shim/sys/stdint.h"
cat >"$work/shim/sys/ioctl.h" <<'IOCTL'
#define	_IO(g, n)	(0x20000000u | ((g) << 8) | (n))
#define	_IOR(g, n, t)	(0x40000000u | ((sizeof(t) & 0xffffu) << 16) | \
			    ((g) << 8) | (n))
#define	_IOW(g, n, t)	(0x80000000u | ((sizeof(t) & 0xffffu) << 16) | \
			    ((g) << 8) | (n))
IOCTL

# build FLASH JOURNAL SWAP BINARY [TEST]: the oracle over the driver text extracted
# from FLASH, the Dhara journal source JOURNAL and the swap source SWAP, as
# strict C17 under the address and undefined-behavior sanitizers.
build()
{
	test_source=${5:-$here/flash_reset_cells_test.c}
	"$PYTHON" "$model/extract.py" --source "$1" --output "$4.driver.c" ||
		fail "extraction from $1"
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined \
	    -DFLASH_CHECKPOINT_DRIVER="\"$4.driver.c\"" \
	    -I"$work/shim" -iquote "$model" -iquote "$topsrc/sys/sys" \
	    -iquote "$dhara" -I"$topsrc/sys/arch" \
	    -o "$4" "$test_source" \
	    "$dhara/error.c" "$2" "$dhara/map.c" "$3" ||
		fail "strict C17 sanitizer build of $4"
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

# rejects BINARY CELL...: the oracle exits 1, the assertion status, and
# reports a failure for every CELL. A sanitizer abort, a panic (status 2) or
# another status is a crash, which does not count as calibration.
rejects()
{
	binary=$1
	shift
	status=0
	"$binary" "$cells" >"$work/mutant.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ]; then
		cat "$work/mutant.log" >&2
		fail "$binary exited $status, not the assertion status 1"
	fi
	for label in "$@"; do
		grep -q "^FAIL $label:" "$work/mutant.log" ||
			fail "$binary did not fail $label"
	done
}

ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=70
export ASAN_OPTIONS UBSAN_OPTIONS

build "$flash" "$dhara/journal.c" "$swap" "$work/oracle"
"$work/oracle" "$cells" >"$work/oracle.log" 2>&1 || {
	cat "$work/oracle.log" >&2
	fail "the shipped driver breaks a cell the table permits"
}
cat "$work/oracle.log"

# Known-good calibration: with a checkpoint page programmed as one command,
# every row, including the recorded defects, holds. The defect rows fail only
# through the cuts inside a checkpoint page program.
FLASH_RESET_CELLS_ATOMIC_CKPT=1 "$work/oracle" "$cells" \
    >"$work/atomic.log" 2>&1 || {
	cat "$work/atomic.log" >&2
	fail "a row fails with atomic checkpoint programs"
}
if grep -q '^DEFECT ' "$work/atomic.log"; then
	fail "a defect row fails with atomic checkpoint programs"
fi

# A sync that never reaches Dhara acknowledges nothing durable.
mutate "$flash" "$work/flash-no-sync.c" \
	'	if (dhara_map_sync(&flmap, &err) < 0)
		error = EIO;' \
	'	if (0 && dhara_map_sync(&flmap, &err) < 0)
		error = EIO;'
build "$work/flash-no-sync.c" "$dhara/journal.c" "$swap" "$work/m-no-sync"
rejects "$work/m-no-sync" E-H2 P-H2 Kf-H2

# A checkpoint that reports success without programming its last page.
mutate "$dhara/journal.c" "$work/journal-no-ckpt.c" \
	'	if (dhara_nand_prog(j->nand, j->head + 1, j->page_buf, &my_err) < 0)
		return recover_from(j, my_err, err);' \
	'	if (0 && dhara_nand_prog(j->nand, j->head + 1, j->page_buf, &my_err) < 0)
		return recover_from(j, my_err, err);'
build "$flash" "$work/journal-no-ckpt.c" "$swap" "$work/m-no-ckpt"
rejects "$work/m-no-ckpt" E-H2 Kf-H2

# A block erase that covers one of its two sectors leaves the other holding
# programmed bytes. Only a block that held pages of an earlier lap shows it,
# which the wrapped-journal rows (Ed) reach.
mutate "$flash" "$work/flash-half-erase.c" \
	'	if (flash_erase(FLASH_FS_OFFSET + b * FLASH_ERASE_BYTES,
	    FLASH_ERASE_BYTES) != 0) {' \
	'	if (flash_erase(FLASH_FS_OFFSET + b * FLASH_ERASE_BYTES,
	    FLASH_SECTOR_BYTES) != 0) {'
build "$work/flash-half-erase.c" "$dhara/journal.c" "$swap" "$work/m-half-erase"
rejects "$work/m-half-erase" Ed-H1

# A sanitizer failure is not a verdict: UBSan exits 70 and ASan aborts, so
# neither can pass for the assertion status 1. The oracle below adds signed
# overflow after its verdicts, and the mutation check must refuse it.
mutate "$here/flash_reset_cells_test.c" "$work/test-ub.c" \
	'	if (failures != 0) {
		printf("flash_reset_cells: %d expectations failed\n", failures);' \
	'	{
		volatile int top = 2147483647;

		failures += top + failures;
	}
	if (failures != 0) {
		printf("flash_reset_cells: %d expectations failed\n", failures);'
build "$work/flash-no-sync.c" "$dhara/journal.c" "$swap" "$work/m-ub" \
	"$work/test-ub.c"
if (rejects "$work/m-ub" E-H2) >/dev/null 2>&1; then
	fail "a sanitizer abort after FAIL lines passes for a rejection"
fi

# A page copy that stops one chip page short moves a torn page; only the
# rows whose copies reach a checkpoint (Gk) read the moved page back.

mutate "$flash" "$work/flash-short-copy.c" \
	'	for (offset = 0; offset < FLASH_UNIT_BYTES;
	    offset += FLASH_PROG_BYTES) {
		if (dhara_nand_read(n, src, offset, FLASH_PROG_BYTES,' \
	'	for (offset = 0; offset < FLASH_UNIT_BYTES - FLASH_PROG_BYTES;
	    offset += FLASH_PROG_BYTES) {
		if (dhara_nand_read(n, src, offset, FLASH_PROG_BYTES,'
build "$work/flash-short-copy.c" "$dhara/journal.c" "$swap" "$work/m-short-copy"
rejects "$work/m-short-copy" Gk-H1

# A swap rewrite that skips the copy into the spare sector loses the pages
# it leaves alone once the destination is erased.
mutate "$swap" "$work/swap-no-spare.c" \
	'			    ops->program(spare_sector + page, scratch,
			    FLASH_PROG_BYTES) != 0)
				return -1;' \
	'			    0)
				return -1;'
build "$flash" "$dhara/journal.c" "$work/swap-no-spare.c" "$work/m-no-spare"
rejects "$work/m-no-spare" T-H1

# A swap append that erases a sector far outside its own run; only the
# whole-extent isolation check sees sector 20 of the 96.
mutate "$swap" "$work/swap-stray.c" \
	'	}
	return 0;
}

/*
 * Rewrite an arbitrary' \
	'	}
	return ops->erase(FLASH_SWAP_OFFSET + 20 * FLASH_SECTOR_BYTES,
	    FLASH_SECTOR_BYTES);
}

/*
 * Rewrite an arbitrary'
build "$flash" "$dhara/journal.c" "$work/swap-stray.c" "$work/m-stray"
rejects "$work/m-stray" S-H1

echo "flash_reset_cells: PASS (the shipped driver meets every host row and" \
	"reproduces the recorded checkpoint-tear defect; atomic checkpoint" \
	"programs meet every row; no-sync, unprogrammed-checkpoint," \
	"half-erase, short-copy, no-spare-copy and stray-swap-erase mutations" \
	"rejected; a sanitizer abort is not a rejection)"
