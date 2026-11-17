#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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
# Run the Renode asynchronous-system-trap gate, or say exactly what is
# missing and stop.
#
# The gate adds tests/rp2040/preempt/preempt to a copy of the root disk
# image under /tmp, wraps the copy as a flash region image the way the flash
# target does, and boots the UART0-console kernel on it once per case
# (check-preempt.py). The shipped images are read and never written; the
# composed image lives under tools/renode/vendor/results/preempt, which git
# ignores.
#
# Usage: sh tools/renode/check-preempt.sh [DISK_IMAGE [CASE...]]
#
# DISK_IMAGE defaults to distrib/rp2040/sdcard.img; the root Makefile passes
# ${FSIMG}. CASE selects among alarm, share, intr, regs and divide; all
# five run when none is named.

set -eu

if [ -z "${PYTHON:-}" ]; then
	echo "check-preempt: PYTHON must name the interpreter" >&2
	exit 2
fi
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$SCRIPT_DIR/../.." && pwd)

VENDOR="$SCRIPT_DIR/vendor/Renode_RP2040"
KERNEL="$TOP/sys/arch/rp2040/compile/PICO_UART/unix.bin"
DISK=${1:-$TOP/distrib/rp2040/sdcard.img}
[ $# -gt 0 ] && shift
MODELS="$VENDOR/emulation/bin/Release/net8.0/Peripherals.dll"
PROGRAM="$TOP/tests/rp2040/preempt/preempt"
FSUTIL="$TOP/tools/bin/rp2040/fsutil"
FLASHIMG="$TOP/tools/bin/rp2040/flashimg"
OUTPUT="$SCRIPT_DIR/vendor/results/preempt"

missing() {
	echo "check-preempt: $1" >&2
	exit 2
}

command -v renode >/dev/null 2>&1 ||
	missing "no renode in PATH; install Renode 1.17 or later"
[ -r "$MODELS" ] ||
	missing "no $MODELS; run sh tools/renode/fetch-renode-rp2040.sh"
[ -r "$KERNEL" ] ||
	missing "no $KERNEL; run bmake MACHINE=rp2040 kernel"
[ -r "$DISK" ] ||
	missing "no $DISK; run bmake MACHINE=rp2040 fs"
[ -x "$FSUTIL" ] && [ -x "$FLASHIMG" ] ||
	missing "no fsutil or flashimg under tools/bin/rp2040; run bmake MACHINE=rp2040 tools"
[ -r "$PROGRAM" ] ||
	missing "no $PROGRAM; run bmake -C tests/rp2040/preempt MACHINE=rp2040"

RUNNING_RENODE=$(renode --version 2>/dev/null | sed -n 's/.*build: *//p' | head -1)
echo "check-preempt: Renode ${RUNNING_RENODE:-unknown}"

mkdir -p "$OUTPUT"
STAGE=$(mktemp -d "$OUTPUT/stage.XXXXXX")
trap 'rm -f "$STAGE/tmp/preempt"; rmdir "$STAGE/tmp" "$STAGE" 2>/dev/null || :' EXIT HUP INT TERM
mkdir "$STAGE/tmp"
cp "$PROGRAM" "$STAGE/tmp/preempt"
chmod 755 "$STAGE/tmp/preempt"
cp "$DISK" "$OUTPUT/disk.img"
(cd "$STAGE" && "$FSUTIL" --add --partition=1 "$OUTPUT/disk.img" tmp/preempt)
"$FSUTIL" --check --partition=1 "$OUTPUT/disk.img" >"$OUTPUT/fsck.txt" 2>&1 ||
	missing "the composed image fails fsutil --check; see $OUTPUT/fsck.txt"
"$FLASHIMG" "$OUTPUT/disk.img" "$OUTPUT/flash.bin" >"$OUTPUT/flashimg.txt"

"$PYTHON" -I "$SCRIPT_DIR/renode_monitor.py" --self-test
# -u: tools/run_logged_gate.py ends a run that prints nothing for its idle
# limit, and Python block-buffers a piped stdout until exit.
"$PYTHON" -I -u "$SCRIPT_DIR/check-preempt.py" renode "$KERNEL" "$OUTPUT/flash.bin" "$OUTPUT" "$@"
