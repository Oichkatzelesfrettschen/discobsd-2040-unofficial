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
# Kernel stack chains of the PICO kernel under each CSIZEFLAGS variant.
#
# Usage: stack-chain.sh OUTDIR
#
# Run from a worktree whose host tools are built (bmake MACHINE=rp2040
# tools) and whose PICO compile directory may be rebuilt: the script cleans
# that directory before each variant and once more when it ends, and it
# refuses a directory that already holds call-graph (*.ci) files. For each
# variant it compiles PICO with -fstack-usage -fcallgraph-info=su, runs
# stack_chain.py with stack-model.json and writes OUTDIR/<variant>.txt (the
# roots, their paths and the stack total) and OUTDIR/<variant>.size.txt. It
# then writes OUTDIR/syscalls.tsv, the deepest chain under each system call
# handler per variant, and OUTDIR/frames.tsv, the frame of each linked
# function per variant ("-" where a variant links no function of that
# name). OUTDIR must be absent or empty.
#
# Variants: base (no CSIZEFLAGS), mi (the tree's default: the three flags
# outside sys/arch), all (the three flags on every C source) and ext (mi
# plus five further flags outside sys/arch).
set -eu

PYTHON=${PYTHON:?set PYTHON to the intended interpreter}
export PYTHON
[ $# -eq 1 ] || { echo "usage: stack-chain.sh OUTDIR" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
build=$top/sys/arch/rp2040/compile/PICO
out=$1
if [ -e "$out" ] && [ -n "$(ls -A "$out")" ]; then
	echo "stack-chain.sh: $out is not empty" >&2
	exit 2
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)

three='-fno-move-loop-invariants -fno-tree-loop-im -fno-tree-dominator-opts'
five='-fno-tree-ter -fno-tree-dce --param=uninlined-function-insns=0'
five="$five -fno-inline-small-functions -fno-ipa-modref"

# The compile directory's clean rule does not know the call-graph files, so
# the script removes the ones its own builds wrote, through the trash. Files
# of that kind present before the first build belong to someone else.
command -v trash-put >/dev/null 2>&1 || {
	echo "stack-chain.sh: not run: trash-put is absent (package trash-cli)" >&2
	exit 2
}
if ls "$build"/*.ci >/dev/null 2>&1; then
	echo "stack-chain.sh: $build holds call-graph files from another build" >&2
	exit 2
fi

sweep() {
	(cd "$build" && bmake MACHINE=rp2040 clean >/dev/null &&
	    for graph in *.ci; do
		[ -e "$graph" ] && trash-put -- "$graph"
	    done; :)
}
trap sweep EXIT

# variant NAME [BMAKE-ASSIGNMENT]
variant() {
	name=$1
	shift
	sweep
	(cd "$build" && bmake MACHINE=rp2040 \
	    'DEBUG=-g -gdwarf-4 -fstack-usage -fcallgraph-info=su' "$@" all) \
	    >"$out/$name.build.log" 2>&1 || {
		echo "stack-chain.sh: $name build failed, see $out/$name.build.log" >&2
		exit 1
	}
	(cd "$build" && arm-none-eabi-size unix.elf) >"$out/$name.size.txt"
	"$PYTHON" "$here/stack_chain.py" "$here/stack-model.json" "$build" \
	    >"$out/$name.txt"
	"$PYTHON" "$here/stack_chain.py" --json "$out/$name.json" \
	    --from-table syscall_handlers "$here/stack-model.json" "$build" \
	    >/dev/null
	tail -n 1 "$out/$name.txt"
}

variant base CSIZEFLAGS_WANT=
variant mi
variant all "COPTS=-Os $three"
variant ext "CSIZEFLAGS_WANT=$three $five"

"$PYTHON" - "$out" <<'EOF'
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
names = ("base", "mi", "all", "ext")
depth, frames = {}, {}
for name in names:
    data = json.loads((out / (name + ".json")).read_text(encoding="utf-8"))
    depth[name] = {root["name"]: root["bytes"] for root in data["roots"]
                   if root["requested"]}
    frames[name] = data["frames"]
    (out / (name + ".json")).unlink()
rows = sorted(depth["base"], key=lambda key: (-depth["base"][key], key))
lines = ["function\t" + "\t".join(names)]
for key in rows:
    lines.append(key + "\t" + "\t".join(str(depth[name].get(key, "-")) for name in names))
(out / "syscalls.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
lines = ["function\t" + "\t".join(names)]
for key in sorted(set().union(*frames.values())):
    lines.append(key + "\t" + "\t".join(str(frames[name].get(key, "-")) for name in names))
(out / "frames.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
EOF
for log in "$out"/*.build.log; do
	trash-put -- "$log"
done
