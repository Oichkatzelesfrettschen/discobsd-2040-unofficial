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
here=$topsrc/tests/rp2040/flash_reset_hook
header=$topsrc/sys/arch/rp2040/dev/flash_reset_hook.h
work=$(mktemp -d "${TMPDIR:-/tmp}/flash-reset-hook.XXXXXX")
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
	echo "flash_reset_hook: FAIL: $*" >&2
	exit 1
}

# build HEADER BINARY: the oracle over HEADER as strict C17 under the address
# and undefined-behavior sanitizers. HEADER is installed as
# rp2040/dev/flash_reset_hook.h ahead of the tree's own include root, so a
# mutated copy replaces the production text and flash.h still resolves.
build()
{
	mkdir -p "$work/$2.inc/rp2040/dev"
	cp "$1" "$work/$2.inc/rp2040/dev/flash_reset_hook.h"
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	    -Wstrict-prototypes -Wold-style-definition \
	    -fsanitize=address,undefined \
	    -I"$work/$2.inc" -I"$topsrc/sys/arch" \
	    -o "$work/$2" "$here/flash_reset_hook_test.c" ||
		fail "strict C17 sanitizer build of $2"
}

# mutate OUTPUT OLD NEW: OUTPUT is the header with its one OLD replaced.
mutate()
{
	"$PYTHON" - "$header" "$1" "$2" "$3" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
old, new = sys.argv[3], sys.argv[4]
if source.count(old) != 1:
    raise SystemExit(f"mutation anchor count differs: {old!r}")
Path(sys.argv[2]).write_text(source.replace(old, new), encoding="utf-8")
PY
}

# rejects NAME LABEL...: the oracle built as NAME exits 1, the assertion
# status, and reports a failure for every LABEL. A sanitizer abort or another
# status is a crash, which does not count as calibration.
rejects()
{
	binary=$work/$1
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

build "$header" oracle
"$work/oracle" >"$work/oracle.log" 2>&1 || {
	cat "$work/oracle.log" >&2
	fail "expectations"
}
cat "$work/oracle.log"

# An off-by-one in the count resets after command N + 1.
mutate "$work/m1.h" \
	'hook->program.count == hook->program.armed' \
	'hook->program.count > hook->program.armed'
build "$work/m1.h" m1
rejects m1 fire fire-first fire-last fire-across
mutate "$work/m2.h" \
	'hook->erase.count == hook->erase.armed' \
	'hook->erase.count > hook->erase.armed'
build "$work/m2.h" m2
rejects m2 fire-erase fire-block

# A reset requested before the Nth command is issued leaves N - 1 on the chip.
mutate "$work/m3.h" \
'		rom(offset, data, n);
		hook->program.count++;' \
'		if (hook->program.count + 1 == hook->program.armed) {
			hook->program.count++;
			hook->program.armed = 0;
			fire(0, offset);
			return 1;
		}
		rom(offset, data, n);
		hook->program.count++;'
build "$work/m3.h" m3
rejects m3 fire fire-first fire-last
mutate "$work/m4.h" \
'		rom(offset, n, FLASH_BLOCK_BYTES, FLASH_BLOCK_ERASE_CMD);
		hook->erase.count++;' \
'		if (hook->erase.count + 1 == hook->erase.armed) {
			hook->erase.count++;
			hook->erase.armed = 0;
			fire(1, offset);
			return 1;
		}
		rom(offset, n, FLASH_BLOCK_BYTES, FLASH_BLOCK_ERASE_CMD);
		hook->erase.count++;'
build "$work/m4.h" m4
rejects m4 fire-erase fire-block

# An unarmed request split into chip commands issues several ROM calls.
mutate "$work/m5.h" \
	'if (hook->program.armed == 0) {' \
	'if (0) {'
build "$work/m5.h" m5
rejects m5 unarmed

# An erase split that ignores the block command issues sector commands where
# the unarmed request issues one block command.
mutate "$work/m6.h" \
	'FLASH_BLOCK_BYTES : FLASH_SECTOR_BYTES;' \
	'FLASH_SECTOR_BYTES : FLASH_SECTOR_BYTES;'
build "$work/m6.h" m6
rejects m6 split fire-block

# An erase counted against the program arm fires on the wrong kind.
mutate "$work/m7.h" \
	'hook->erase.count == hook->erase.armed' \
	'hook->erase.count == hook->program.armed'
build "$work/m7.h" m7
rejects m7 kind

# Arming that keeps the old count resets early.
mutate "$work/m8.h" \
	'	kind->count = 0;
' ''
build "$work/m8.h" m8
rejects m8 rearm

echo "flash_reset_hook: PASS (unarmed one call, split commands equal the" \
	"unarmed run, reset after exactly the Nth command, kind separation," \
	"rearm; count off-by-one (program, erase), reset before issue" \
	"(program, erase), unarmed split, block-erase split, cross-kind count" \
	"and kept-count mutations rejected)"
