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
# Kernel stack chains of the rp2040 kernels.
#
# Usage: stack-chain.sh OUTDIR ROM_ELF
#
# Run from a worktree whose host tools are built (bmake MACHINE=rp2040
# tools) and whose kernel compile directories may be rebuilt: the script
# cleans each directory before it builds there and once more when it ends,
# and it refuses a directory that already holds call-graph (*.ci) files.
# ROM_ELF is the boot ROM image with symbols, b2.elf of the
# raspberrypi/pico-bootrom-rp2040 release B2.
#
# PICO is compiled under each CSIZEFLAGS variant with -fstack-usage
# -fcallgraph-info=su, and stack_chain.py with stack-model.json writes
# OUTDIR/<variant>.txt (the roots, their paths and the stack totals) and
# OUTDIR/<variant>.size.txt. Every other configuration is compiled with the
# tree's default flags and gives OUTDIR/<CONFIG>.txt and .size.txt. The
# script then writes OUTDIR/syscalls.tsv, the deepest chain under each
# system call handler per PICO variant; OUTDIR/frames.tsv, the frame of
# each linked function per PICO variant ("-" where a variant links no
# function of that name); OUTDIR/configs.tsv, the totals of each
# configuration under the default flags; and OUTDIR/bootrom.txt, the depth
# of each boot ROM routine and of the second-stage boot code, which
# bootrom_frames.py holds the model's figures to. OUTDIR must be absent or
# empty.
#
# Variants: base (no CSIZEFLAGS), mi (the tree's default: the three flags
# outside sys/arch), all (the three flags on every C source) and ext (mi
# plus five further flags outside sys/arch).
set -eu

PYTHON=${PYTHON:?set PYTHON to the intended interpreter}
export PYTHON
[ $# -eq 2 ] || { echo "usage: stack-chain.sh OUTDIR ROM_ELF" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
compile=$top/sys/arch/rp2040/compile
others='PICO_UART PICO_DIAG PICOCALC PICOCALC_LEAN PICOCALC_LEAN_DIAG'
out=$1
rom=$2
[ -r "$rom" ] || { echo "stack-chain.sh: $rom is not readable" >&2; exit 2; }
if [ -e "$out" ] && [ -n "$(ls -A "$out")" ]; then
	echo "stack-chain.sh: $out is not empty" >&2
	exit 2
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)

three='-fno-move-loop-invariants -fno-tree-loop-im -fno-tree-dominator-opts'
five='-fno-tree-ter -fno-tree-dce --param=uninlined-function-insns=0'
five="$five -fno-inline-small-functions -fno-ipa-modref"

# A compile directory's clean rule does not know the call-graph files, so
# the script removes the ones its own builds wrote, through the trash. Files
# of that kind present before the first build belong to someone else.
command -v trash-put >/dev/null 2>&1 || {
	echo "stack-chain.sh: not run: trash-put is absent (package trash-cli)" >&2
	exit 2
}
for config in PICO $others; do
	if ls "$compile/$config"/*.ci >/dev/null 2>&1; then
		echo "stack-chain.sh: $compile/$config holds call-graph files" \
		    "from another build" >&2
		exit 2
	fi
done

# sweep CONFIG
sweep() {
	(cd "$compile/$1" && bmake MACHINE=rp2040 clean >/dev/null &&
	    for graph in *.ci; do
		[ -e "$graph" ] && trash-put -- "$graph"
	    done; :)
}
sweep_all() {
	for config in PICO $others; do
		sweep "$config"
	done
}
trap sweep_all EXIT

# variant NAME CONFIG [BMAKE-ASSIGNMENT]
variant() {
	name=$1
	build=$compile/$2
	shift 2
	sweep "${build##*/}"
	(cd "$build" && bmake MACHINE=rp2040 \
	    'DEBUG=-g -gdwarf-4 -fstack-usage -fcallgraph-info=su' "$@" all) \
	    >"$out/$name.build.log" 2>&1 || {
		echo "stack-chain.sh: $name build failed, see $out/$name.build.log" >&2
		exit 1
	}
	(cd "$build" && arm-none-eabi-size unix.elf) >"$out/$name.size.txt"
	"$PYTHON" "$here/stack_chain.py" --json "$out/$name.json" \
	    --from-table syscall_handlers "$here/stack-model.json" "$build" \
	    >"$out/$name.all.txt"
	# The handlers the table names are reported in syscalls.tsv.
	"$PYTHON" "$here/stack_chain.py" "$here/stack-model.json" "$build" \
	    >"$out/$name.txt"
	trash-put -- "$out/$name.all.txt"
	grep '^stack ' "$out/$name.txt" | sed "s/^/$name: /"
}

variant base PICO CSIZEFLAGS_WANT=
variant all PICO "COPTS=-Os $three"
variant ext PICO "CSIZEFLAGS_WANT=$three $five"
variant mi PICO
# PICO's default build is still in place: its second stage is the one the
# flash image carries.
"$PYTHON" "$here/bootrom_frames.py" --boot2 "$compile/PICO/boot2.elf" \
    --check "$here/stack-model.json" "$rom" >"$out/bootrom.txt"
for config in $others; do
	variant "$config" "$config"
done

"$PYTHON" - "$out" $others <<'EOF'
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
names = ("base", "mi", "all", "ext")
configs = ("mi", *sys.argv[2:])
depth, frames, rows = {}, {}, []
for name in dict.fromkeys(names + configs):
    data = json.loads((out / (name + ".json")).read_text(encoding="utf-8"))
    depth[name] = {root["name"]: root["bytes"] for root in data["roots"]
                   if root["requested"]}
    frames[name] = data["frames"]
    (out / (name + ".json")).unlink()
    if name not in configs:
        continue
    sizes = (out / (name + ".size.txt")).read_text(encoding="utf-8").split("\n")[1]
    for stack in data["stacks"]:
        # A root that does not run on this stack has no figure here.
        plain = {root["name"]: root["bytes"] for root in data["roots"]
                 if stack["stack"] in root["stacks"]}
        rows.append(["PICO" if name == "mi" else name, stack["stack"],
                     *sizes.split()[:3], stack["base"], stack["base_bytes"],
                     plain.get("boot", "-"), plain.get("fault", "-"),
                     " ".join(f"{level}={entry['bytes']}"
                              for level, entry in stack["levels"].items()),
                     stack["total"], stack["budget"],
                     *(stack["overlays"][key] for key in sorted(stack["overlays"]))])
        over = sorted(stack["overlays"])
(out / "configs.tsv").write_text("\n".join(
    ["\t".join(["config", "stack", "text", "data", "bss", "base", "base_bytes",
                "boot", "fault", "levels", "total", "budget",
                *(key + "_total" for key in over)])] +
    ["\t".join(str(item) for item in row) for row in rows]) + "\n", encoding="utf-8")
rows = sorted(depth["base"], key=lambda key: (-depth["base"][key], key))
lines = ["function\t" + "\t".join(names)]
for key in rows:
    lines.append(key + "\t" + "\t".join(str(depth[name].get(key, "-")) for name in names))
(out / "syscalls.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
lines = ["function\t" + "\t".join(names)]
for key in sorted(set().union(*(frames[name] for name in names))):
    lines.append(key + "\t" + "\t".join(str(frames[name].get(key, "-")) for name in names))
(out / "frames.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
EOF
for log in "$out"/*.build.log; do
	trash-put -- "$log"
done
