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

# Write cflow call trees of one built RP2040 kernel configuration.
#
# usage: cflow_maps.sh COMPILE_DIR OUTPUT_DIR [ROOT ...]
#
# The source list is the set of C files whose objects the configuration's
# build produced, so the trees follow the files that link into that kernel;
# the preprocessor definitions are the PARAM lines of the generated
# Makefile, so each #ifdef arm matches the build. cflow follows direct calls
# only: calls through bdevsw, cdevsw, execsw and other function-pointer
# tables appear as leaves the reader resolves by hand. Paths in the output
# are relative to the repository root.

set -eu

if [ "$#" -lt 2 ]; then
	echo "usage: cflow_maps.sh COMPILE_DIR OUTPUT_DIR [ROOT ...]" >&2
	exit 2
fi
compile=$(CDPATH='' cd "$1" && pwd)
mkdir -p "$2"
output=$(CDPATH='' cd "$2" && pwd)
shift 2
if [ "$#" -eq 0 ]; then
	set -- sched swapout swapin swap_exchange_inplace swapram_reserve \
	    swapram_put swapram_in execve exec_estab procxmt mpu_init \
	    mpu_guard_update
fi
command -v cflow >/dev/null 2>&1 || {
	echo "cflow_maps.sh: cflow not found (GNU cflow package)" >&2
	exit 2
}
top=$(git -C "$compile" rev-parse --show-toplevel)
sys=$top/sys
config=$(basename "$compile")
list=$output/$config.sources
: >"$list"
for object in "$compile"/*.o; do
	[ -f "$object" ] || continue
	name=$(basename "$object" .o)
	case $name in
	vers | ioconf | boot2) continue ;;
	esac
	source=$(find "$sys" -name "$name.c" -not -path '*/test*' | sort | head -n 1)
	if [ -n "$source" ]; then
		printf '%s\n' "$source" >>"$list"
	fi
done
[ -s "$list" ] || {
	echo "cflow_maps.sh: $compile holds no objects; build the kernel first" >&2
	exit 1
}
definitions=$(sed -n 's/^PARAM *+*= *//p' "$compile/Makefile" | tr '\n' ' ' |
    sed 's/ *$//')
printf '%s\n' "$definitions" >"$output/$config.definitions"
preprocessor="cpp -nostdinc -DKERNEL -I$compile -I$sys -I$sys/arch"
preprocessor="$preprocessor -I$sys/arch/rp2040/include"
preprocessor="$preprocessor -I$sys/arch/rp2040/dhara/compat"
preprocessor="$preprocessor -I$sys/arch/rp2040/heatshrink"
preprocessor="$preprocessor -I$sys/arch/rp2040/heatshrink/compat $definitions"
for root in "$@"; do
	# The source list holds repository paths without blanks, so the word
	# split of the command substitution yields one argument per file.
	# shellcheck disable=SC2046
	cflow --cpp="$preprocessor" -d 4 -m "$root" $(cat "$list") \
	    2>/dev/null | sed "s|$top/||g; s|\./machine/|sys/arch/rp2040/include/|g" \
	    >"$output/$config-$root.txt"
done
sed "s|$top/||" "$list" >"$list.tmp"
mv "$list.tmp" "$list"
