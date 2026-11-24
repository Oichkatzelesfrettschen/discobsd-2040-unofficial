#!/bin/sh
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Produce the three inputs of check-renode-xip-process from the checkout:
# the compact slot ELF, the packaged provider and the partitioned 960 KiB
# root. A clean libc build runs under the C17 command recorder against a
# tracked-source snapshot; tools/libc_pic_replay.py recompiles its members
# as r9 PIC; link-pic-envelope.py writes the startup fixture and linker
# template; relink_envelope.py --provider links the slot image and packs
# the provider, refusing an archive, template, startup object or package
# whose SHA-256 differs from docs/research/xip-shared-libc. LC_ALL=C keeps
# shell globs and sorts in byte order, which the recorded digests assume.
# The root is the shipped manifest in one 960 KiB fs partition, the layout
# the PICO_UART_XIP kernel mounts. Each run writes a fresh directory under
# .build-logs and points .build-logs/xip-gate-inputs at it.
#
# Usage: tools/xip_gate_inputs.sh TOPSRC MAKE
# Requires a completed build (logged-build) for tools/bin and DESTDIR.
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
LC_ALL=C
export LC_ALL

root=$1
make=$2
cd "$root"
mkdir -p .build-logs
out=$(mktemp -d "$root/.build-logs/xip-gate-inputs.XXXXXX")

"$PYTHON" -c 'import json, sys
from pathlib import Path
sys.path.insert(0, "tools/c17")
from shipped_closure import source_snapshot
print(json.dumps(source_snapshot(Path(".").resolve())))' > "$out/pic-source.json"

(cd lib/libc && "$make" MACHINE=rp2040 clean >/dev/null &&
    C17_SOURCE_SNAPSHOT="$out/pic-source.json" "$make" MACHINE=rp2040 \
    C17_CAPTURE_DIR="$out/pic-capture" >"$out/libc-capture.log" 2>&1)
"$PYTHON" tools/libc_pic_replay.py --root . --records "$out/pic-capture" \
    --snapshot "$out/pic-source.json" --archive lib/libc.a \
    --output "$out/pic-replay"
"$PYTHON" docs/research/xip-shared-libc/export-closure/link-pic-envelope.py \
    --root . --replay "$out/pic-replay" --output "$out/pic-link"
relink=$("$PYTHON" tests/libc_xip_slots/relink_envelope.py --provider \
    "$out/pic-replay/libc-pic.a" "$out/pic-link" | tail -n 1)
cp "$relink/slot.elf" "$relink/provider.bin" "$out/"
"$make" MACHINE=rp2040 fs FS_KBYTES=960 FSIMG="$out/root.img" \
    >"$out/root.log" 2>&1
ln -sfn "${out##*/}" "$root/.build-logs/xip-gate-inputs"
echo "xip-gate-inputs: $out"
