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

# usage: mutants.sh SOURCEDIR DOCFILE HOST_CC
#
# Builds the interpreter from copies of its sources with one defect each
# and requires check.sh to reject every one: a gate that passes a broken
# interpreter reports nothing. Each defect is one substitution in ulisp.c
# that must change the file.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$1
doc=$2
cc=$3

work=$(mktemp -d "${TMPDIR:-/tmp}/ulisp-mutants.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# mutant NAME SED-EXPRESSION
mutant() {
	dir=$work/$1
	mkdir "$dir"
	cp "$src"/*.c "$src"/*.h "$src"/builtins.def "$dir"/
	sed "$2" "$src/ulisp.c" > "$dir/ulisp.c"
	if cmp -s "$src/ulisp.c" "$dir/ulisp.c"; then
		echo "ulisp mutants: $1: the substitution matched nothing" >&2
		exit 1
	fi
	$cc -std=c17 -D_DEFAULT_SOURCE -Os -w -I"$src/../../sys/arch/rp2040/dev" \
	    -o "$dir/ulisp" "$dir/ulisp.c" "$dir/unix.c" "$dir/gfx.c" \
	    "$dir/dev_host.c" "$src/../../sys/arch/rp2040/dev/lcdfont.c" -lm
	if sh "$here/check.sh" host "$doc" timeout 60 "$dir/ulisp" \
	    > "$dir/log" 2>&1; then
		echo "ulisp mutants: $1 passed the check" >&2
		exit 1
	fi
	echo "ulisp mutants: $1 rejected"
}

# The collector leaves its trace bit in a restored pair.
mutant mark-restore 's/if (state != (ARRAY | MARKBIT)) car(above)/if (0) car(above)/'
# A loaded image keeps an array's old address.
mutant rebase-array 's/ || type == ZZERO || type == ARRAY) cdr(obj) = rebase/ || type == ZZERO) cdr(obj) = rebase/'
# The printer's power of ten is inexact, as a library pow() may be.
mutant power-of-ten 's/double result = 1.0, power = 10.0;/double result = 1.0, power = 9.999999;/'
# The reader takes the end of a file as a symbol character.
mutant reader-end 's/ \&\& ch != -1 \&\& index < bufmax/ \&\& index < bufmax/'
# cdr returns the car.
mutant cdr-is-car 's/^  return cdrx(first(args));$/  return carx(first(args));/'
# String comparison shifts a word wider than the four characters it holds.
mutant wide-chars 's/int m = 0; chars_t a = 0, b = 0;/int m = 0; unsigned long long a = 0, b = 0;/'
# analogread returns the 12-bit count where upstream's default is 10 bits.
mutant analogread-scale 's/sys_analogread(pin) >> 2/sys_analogread(pin)/'
# analogread accepts the pin after GP29.
mutant analogread-range 's/if (!(pin>=26 \&\& pin<=29)) error(invalidpin/if (!(pin>=26 \&\& pin<=30)) error(invalidpin/'
# analogwrite accepts the pin after GP29.
mutant analogwrite-range 's/if (!(pin>=0 \&\& pin<=29)) error(invalidpin/if (!(pin>=0 \&\& pin<=30)) error(invalidpin/'
# analogwrite hands the device the next pin.
mutant analogwrite-pin 's/sys_analogwrite(pin, /sys_analogwrite(pin + 1, /'
# analogwrite passes a duty above 255 on to the device.
mutant analogwrite-clamp-high 's/duty > 255 ? 255 : duty/duty > 255 ? duty : duty/'
# analogwrite passes a negative duty on to the device.
mutant analogwrite-clamp-low 's/duty < 0 ? 0 : duty/duty < 0 ? duty : duty/'
