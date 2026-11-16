#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

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

# The rows the oracle must find in cells.tsv: "ID run" for a row it executes
# (status host or defect) and "ID skip" for a row it reports only. A literal,
# never derived from the table, so that a table that lost, repeated or gained
# a row differs from it. A row whose status flips between host and defect
# (the checkpoint-tear fix) keeps its line; adding, removing or skipping a row
# edits this list.
manifest=$work/manifest.txt
cat >"$manifest" <<'MANIFEST'
P-H0 skip
E-H0 run
Kf-H0 run
Kg-H0 skip
G-H0 skip
Gk-H0 skip
Ed-H0 skip
F-H0 run
S-H0 run
T-H0 run
P-H1 run
E-H1 run
Kf-H1 run
Kg-H1 run
G-H1 run
Gk-H1 run
Ed-H1 run
F-H1 run
S-H1 run
T-H1 run
P-H2 run
E-H2 run
Kf-H2 run
Kg-H2 run
G-H2 run
Gk-H2 run
Ed-H2 run
F-H2 run
S-H2 run
T-H2 run
P-H3 run
E-H3 run
Kf-H3 run
Kg-H3 run
G-H3 run
Gk-H3 run
Ed-H3 run
F-H3 run
S-H3 run
T-H3 run
P-H4 run
E-H4 run
Kf-H4 run
Kg-H4 run
G-H4 run
Gk-H4 run
Ed-H4 run
F-H4 run
S-H4 run
T-H4 run
Kf-min run
U-resume skip
U-fsck-ufs skip
E-boot skip
K-torn skip
MANIFEST

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
	    -Wstrict-prototypes -Wold-style-definition -Wcast-qual \
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
	"$binary" "$cells" "$manifest" >"$work/mutant.log" 2>&1 || status=$?
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

# rejects_cell BINARY CELL: as rejects, for the one row CELL alone.
rejects_cell()
{
	status=0
	"$1" "$cells" "$manifest" "$2" >"$work/mutant.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ]; then
		cat "$work/mutant.log" >&2
		fail "$1 exited $status, not the assertion status 1"
	fi
	grep -q "^FAIL $2:" "$work/mutant.log" || {
		cat "$work/mutant.log" >&2
		fail "$1 did not fail $2"
	}
}

# rejects_table TABLE PATTERN: the oracle refuses TABLE before it runs a row,
# with the assertion status 1 and a "FAIL table:" line matching PATTERN. A
# sanitizer abort or a crash is another status and does not count.
rejects_table()
{
	status=0
	"$work/oracle" "$1" "$manifest" >"$work/table.log" 2>&1 || status=$?
	if [ "$status" -ne 1 ] || ! grep -q "^FAIL table: $2" "$work/table.log" ||
	    grep -q '^cell ' "$work/table.log"; then
		cat "$work/table.log" >&2
		fail "the oracle did not refuse the table with '$2' (status $status)"
	fi
}

build "$flash" "$dhara/journal.c" "$swap" "$work/oracle"
"$work/oracle" "$cells" "$manifest" >"$work/oracle.log" 2>&1 || {
	cat "$work/oracle.log" >&2
	fail "the shipped driver breaks a cell the table permits"
}
cat "$work/oracle.log"
# A targeted run names one cell; an id the table lacks must fail rather than
# run nothing and pass.
if "$work/oracle" "$cells" "$manifest" NO-SUCH-CELL \
    >"$work/unknown-id.log" 2>&1 ||
    ! grep -q "no cell named 'NO-SUCH-CELL'" "$work/unknown-id.log"; then
	cat "$work/unknown-id.log" >&2
	fail "an unknown cell id passes"
fi

# Known-good calibration: with a checkpoint page programmed as one command,
# every row, including the recorded defects, holds. The defect rows fail only
# through the cuts inside a checkpoint page program.
FLASH_RESET_CELLS_ATOMIC_CKPT=1 "$work/oracle" "$cells" "$manifest" \
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


# A table that lost, repeated, gained or reclassified a row, or that ends
# mid-row, runs a smaller matrix than the manifest names. Each defect is
# refused before any row runs.
tab=$(printf '\t')
bad=$work/bad.tsv
: >"$bad"
rejects_table "$bad" '0 rows, the manifest names'
sed -n 1p "$cells" >"$bad"
rejects_table "$bad" '0 rows, the manifest names'
head -n 30 "$cells" >"$bad"
rejects_table "$bad" '[0-9]* rows, the manifest names'
head -c 5000 "$cells" >"$bad"
[ -n "$(tail -c 1 "$bad")" ] || fail "the mid-row truncation ends at a row boundary"
rejects_table "$bad" 'line [0-9]*: no final newline'
sed '$d' "$cells" >"$bad"
printf 'Kf-min\tKf\tH2\n' >>"$bad"
rejects_table "$bad" 'line [0-9]*: 3 fields, 10 required'
grep -v "^Kf-min$tab" "$cells" >"$bad"
rejects_table "$bad" 'row Kf-min is missing'
{ cat "$cells"; grep "^P-H0$tab" "$cells"; } >"$bad"
rejects_table "$bad" 'row P-H0 appears 2 times'
{ cat "$cells"; grep "^P-H0$tab" "$cells" | sed 's/^P-H0/NEW-ROW/'; } >"$bad"
rejects_table "$bad" 'row NEW-ROW is not in the manifest'
sed "/^E-H0$tab/s/${tab}host${tab}/${tab}n\/a${tab}/" "$cells" >"$bad"
cmp -s "$bad" "$cells" && fail "the status mutation changed nothing"
rejects_table "$bad" "row E-H0 has status 'n/a', the manifest says run"
{ cat "$cells"; printf 'LONG\t'; head -c 3000 /dev/zero | tr '\0' x; printf '\n'; } \
    >"$bad"
rejects_table "$bad" 'line [0-9]*: longer than'
# The oracle needs its manifest: without one it exits 2 and runs nothing.
status=0
"$work/oracle" "$cells" >"$work/no-manifest.log" 2>&1 || status=$?
if [ "$status" -ne 2 ] || grep -q '^cell ' "$work/no-manifest.log"; then
	cat "$work/no-manifest.log" >&2
	fail "the oracle runs without a manifest (status $status)"
fi
# A manifest that omits a table row is a manifest defect, not a pass.
grep -v '^Kf-min ' "$manifest" >"$work/manifest-short.txt"
status=0
"$work/oracle" "$cells" "$work/manifest-short.txt" >"$work/table.log" 2>&1 ||
    status=$?
if [ "$status" -ne 1 ] ||
    ! grep -q '^FAIL table: row Kf-min is not in the manifest' "$work/table.log"; then
	cat "$work/table.log" >&2
	fail "a manifest without a table row passes"
fi

# The step-script parser: a repeat count is digits, at least 1 and at most
# MAX_WRITES. E-H1 repeats a step ("W*7"), so it also runs the parser under
# the sanitizers. Converting the address of a const char * to char ** for the
# end pointer of strtoul is a type-punning access that no sanitizer reports;
# -Wcast-qual in the build refuses it.
for count in '' 0 99999999999999999999 x; do
	sed "/^E-H1$tab/s/W\*7/W*$count/" "$cells" >"$bad"
	cmp -s "$bad" "$cells" && fail "the repeat-count mutation changed nothing"
	status=0
	"$work/oracle" "$bad" "$manifest" E-H1 >"$work/count.log" 2>&1 ||
	    status=$?
	if [ "$status" -ne 2 ] ||
	    ! grep -q '^panic: step repeat count' "$work/count.log"; then
		cat "$work/count.log" >&2
		fail "the repeat count '$count' is accepted (status $status)"
	fi
done
mutate "$here/flash_reset_cells_test.c" "$work/test-cast.c" \
	'n = strtoul(p + 1, &end, 10);' \
	'n = strtoul(p + 1, (char **)&p, 10);'
if (build "$flash" "$dhara/journal.c" "$swap" "$work/m-cast" \
    "$work/test-cast.c") >"$work/cast.log" 2>&1 ||
    ! grep -q 'cast-qual' "$work/cast.log"; then
	fail "a const-discarding strtoul end pointer builds"
fi

# A defect row passes only while its failures equal the recorded tear. A
# change inside the checkpoint program, here a recovery that skips a page
# whose header is half written, moves some cuts from the blank root to a
# refused resume: the failure set stays inside the window, so a verdict of
# "no failure outside the window, one cut between commands" would accept it.
mutate "$dhara/journal.c" "$work/journal-refuse.c" \
	'		    (hdr_has_magic(j->page_buf)) &&
		    (hdr_get_epoch(j->page_buf) == j->epoch)) {
			j->root = p - 1;' \
	'		    (hdr_has_magic(j->page_buf)) &&
		    (hdr_get_epoch(j->page_buf) == j->epoch) &&
		    (hdr_get_bb_last(j->page_buf) != (dhara_page_t)~0u ||
		    hdr_get_bb_current(j->page_buf) != (dhara_page_t)~0u)) {
			j->root = p - 1;'
build "$flash" "$work/journal-refuse.c" "$swap" "$work/m-refuse"
rejects_cell "$work/m-refuse" Kf-min
grep -q 'recorded checkpoint-tear signature' "$work/mutant.log" ||
	fail "the half-header mutant failed Kf-min for another reason"

echo "flash_reset_cells: PASS (the shipped driver meets every host row and" \
	"reproduces the recorded checkpoint-tear defect cut by cut; atomic" \
	"checkpoint programs meet every row; no-sync, unprogrammed-checkpoint," \
	"half-erase, short-copy, no-spare-copy and stray-swap-erase mutations," \
	"a changed tear signature, a bad table or manifest and a bad repeat" \
	"count rejected; a sanitizer abort is not a rejection)"
