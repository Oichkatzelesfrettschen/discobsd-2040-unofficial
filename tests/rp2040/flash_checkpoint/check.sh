#!/bin/sh

set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
here=$topsrc/tests/rp2040/flash_checkpoint
dhara=$topsrc/sys/arch/rp2040/dhara
flash=$topsrc/sys/arch/rp2040/dev/flash.c
work=$(mktemp -d "${TMPDIR:-/tmp}/flash-checkpoint.XXXXXX")
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
	echo "flash_checkpoint: FAIL: $*" >&2
	exit 1
}

# sys/storage_stats.h names the kernel's <sys/stdint.h>; on the host that is
# the C library's <stdint.h>.
mkdir -p "$work/shim/sys"
echo '#include <stdint.h>' >"$work/shim/sys/stdint.h"

# build FLASH_SOURCE BINARY [TEST_SOURCE]: the oracle over the driver text
# extracted from FLASH_SOURCE, as strict C17 under the address and
# undefined-behavior sanitizers. TEST_SOURCE replaces the oracle's own text
# when a mutation targets the NOR model.
build()
{
	"$PYTHON" "$here/extract.py" --source "$1" --output "$2.driver.c" ||
		fail "extraction from $1"
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined \
	    -DFLASH_CHECKPOINT_DRIVER="\"$2.driver.c\"" \
	    -I"$work/shim" -iquote "$topsrc/sys/sys" -I"$topsrc/sys/arch" \
	    -o "$2" "${3:-$here/flash_checkpoint_test.c}" \
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

# rejects BINARY LABEL...: the oracle exits 1, the assertion status, and
# reports a failure for every LABEL. A sanitizer abort, a panic (status 2) or
# another status is a crash, which does not count as calibration.
rejects()
{
	binary=$1
	shift
	status=0
	"$binary" >"$work/mutant.log" 2>&1 || status=$?
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
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

build "$flash" "$work/oracle"
"$work/oracle" >"$work/oracle.log" 2>&1 || {
	cat "$work/oracle.log" >&2
	fail "expectations"
}
cat "$work/oracle.log"

# A checkpoint after every write request, the policy this gate replaced, must
# fail the wear bound and lose the reset and group-fill expectations.
mutate "$flash" "$work/flash-per-request.c" \
	'	flscratch_owner = FLS_FREE;
	clock_catchup();
	splx(s);

done:' \
	'	if (! (bp->b_flags & B_READ) && ! fail &&
	    dhara_map_sync(&flmap, &err) < 0)
		fail = 1;
	flscratch_owner = FLS_FREE;
	clock_catchup();
	splx(s);

done:'
build "$work/flash-per-request.c" "$work/oracle-per-request"
rejects "$work/oracle-per-request" wear reset-before-checkpoint group-fill

# flioctl without DIOCCHECKPOINT gives ufs_sync() nothing to call.
mutate "$flash" "$work/flash-no-ioctl.c" \
	'		*(int *)addr = fl_checkpoint();
		return 0;' \
	'		return EINVAL;'
build "$work/flash-no-ioctl.c" "$work/oracle-no-ioctl"
rejects "$work/oracle-no-ioctl" checkpoint-then-reset flush-without-write

# A checkpoint entry that never reaches dhara_map_sync() makes nothing durable.
mutate "$flash" "$work/flash-noop.c" \
	'	if (dhara_map_sync(&flmap, &err) < 0)
		error = EIO;' \
	'	if (0 && dhara_map_sync(&flmap, &err) < 0)
		error = EIO;'
build "$work/flash-noop.c" "$work/oracle-noop"
rejects "$work/oracle-noop" checkpoint-then-reset flush-without-write

# A checkpoint that ignores the descriptor's flags admits a read-only opener.
mutate "$flash" "$work/flash-any-opener.c" \
	'		if (flag != 0 && (flag & FWRITE) == 0)
			return EBADF;
' '		(void)flag;
'
build "$work/flash-any-opener.c" "$work/oracle-any-opener"
rejects "$work/oracle-any-opener" idle-checkpoint

# The NOR model refuses an erase or a program in the swap region. Comparing
# the offset after the subtraction, which wraps past the end of the root
# region, admits one; the model-range scenario must catch it for each entry.
test_source=$here/flash_checkpoint_test.c
mutate "$test_source" "$work/model-erase.c" \
	'len % FLASH_SECTOR_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET || offset >= FLASH_SWAP_OFFSET ||' \
	'len % FLASH_SECTOR_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET ||'
build "$flash" "$work/oracle-model-erase" "$work/model-erase.c"
rejects "$work/oracle-model-erase" model-range
mutate "$test_source" "$work/model-program.c" \
	'len % FLASH_PROG_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET || offset >= FLASH_SWAP_OFFSET ||' \
	'len % FLASH_PROG_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET ||'
build "$flash" "$work/oracle-model-program" "$work/model-program.c"
rejects "$work/oracle-model-program" model-range

echo "flash_checkpoint: PASS (reset, checkpoint, group fill, flush without" \
	"a write, wear bound, idle checkpoint; per-request, missing-ioctl," \
	"no-op checkpoint, read-only-opener and swap-range model mutations" \
	"rejected)"
