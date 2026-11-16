#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Calibration of the reboot contract: each broken copy of reboot.c must make
# the harness exit nonzero, and each sed must change the source, so a
# pattern that stops matching fails here instead of passing silently.
# usage: mutants.sh HOSTCC HOST_CFLAGS RENAMES REBOOT_C DIR
set -eu

cc=$1
cflags=$2
renames=$3
src=$4
dir=$5

fail=0
n=0
mutate() {
	name=$1
	expr=$2
	n=$((n + 1))
	sed -e "$expr" "$src" > "$dir/mutant_$name.c"
	if cmp -s "$src" "$dir/mutant_$name.c"; then
		echo "mutants: $name did not change the source" >&2
		fail=1
		return
	fi
	# shellcheck disable=SC2086
	"$cc" $cflags -Wno-error $renames -c -o "$dir/mutant_$name.o" "$dir/mutant_$name.c"
	# shellcheck disable=SC2086
	"$cc" $cflags -o "$dir/mutant_$name" "$dir/reboot_contract_test.c" \
	    "$dir/mutant_$name.o"
	if "$dir/mutant_$name" > "$dir/mutant_$name.out" 2>&1; then
		echo "mutants: $name survived" >&2
		fail=1
	else
		echo "mutants: $name rejected"
	fi
}

mutate no-root-check 's/geteuid() != 0/0/'
mutate no-confirm-check 's/if (!confirmed)/if (!confirmed \&\& 0)/'
mutate always-confirmed 's/int confirmed = 0;/int confirmed = 1;/'
mutate lost-y-option "s/case 'y':  confirmed = 1;/case 'y':  confirmed = 0;/"
mutate lost-B-option "s/case 'B':  howto |= RB_HALT|RB_BOOTLOADER;/case 'B':  howto |= RB_HALT;/"
[ "$fail" -eq 0 ] || exit 1
echo "mutants: all $n rejected"
