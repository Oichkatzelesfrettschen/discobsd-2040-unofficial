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

# The race corpus runs when the Makefile has built its shim (Linux only);
# the image-reread mutant needs it
shim=
if [ -f "$here/rewrite-race.so" ]; then
	shim=$here/rewrite-race.so
fi

# mutant NAME SED-EXPRESSION [SOURCE-FILE]
mutant() {
	dir=$work/$1
	file=${3:-ulisp.c}
	mkdir "$dir"
	cp "$src"/*.c "$src"/*.h "$src"/builtins.def "$dir"/
	sed "$2" "$src/$file" > "$dir/$file"
	if cmp -s "$src/$file" "$dir/$file"; then
		echo "ulisp mutants: $1: the substitution matched nothing" >&2
		exit 1
	fi
	$cc -std=c17 -D_DEFAULT_SOURCE -Os -w -I"$src/../../sys/arch/rp2040/dev" \
	    -o "$dir/ulisp" "$dir/ulisp.c" "$dir/unix.c" "$dir/gfx.c" \
	    "$dir/dev_host.c" "$src/../../sys/arch/rp2040/dev/lcdfont.c" -lm
	if ULISP_REWRITE_SHIM=$shim sh "$here/check.sh" host "$doc" timeout 60 "$dir/ulisp" \
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
# Integer division of the most negative int by -1 reaches the remainder.
mutant divide-min '/(result == INT_MIN) \&\& (i == -1)/d'
# A left shift by the width of the type reaches the C shift.
mutant ash-width 's/return number((count >= 32) ? 0 : (int)((unsigned int)value << count));/return number(value << count);/'
# A float outside the int range converts to an int.
mutant float-to-int 's/if (f >= -2147483648.0f \&\& f < 2147483648.0f) return number((int)f);/return number((int)f);/'
# The loader trusts its first check of the file and installs a second read.
mutant image-reread 's/^  fault = checkimage(header, imagesize);$/  fault = NULL;/'
# A loaded image is not checked against its own span.
mutant image-unchecked 's/const char \*fault = checkimage(header, imagesize);/const char *fault = NULL;/'
# A pair may point into a string chain, whose cells hold characters.
mutant image-chain-target 's/return chainbit(chains, index) ? imagelinked : NULL;/return NULL;/'
# A random limit of zero reaches the modulus.
mutant random-zero 's/if (arg->integer <= 0) error("limit must be positive", arg);//'
# The remainder of INT_MIN by -1 reaches the division.
mutant rem-overflow 's/(divisor == -1) ? 0 : dividend % divisor/dividend % divisor/'
# The negation of INT_MIN happens in int.
mutant negate-int-min 's/makefloat(-(float)result)/makefloat(-result)/'
# Array dimensions are multiplied without a check against INT_MAX.
mutant array-overflow 's/else if (size > INT_MAX \/ d) toobig = true;/else if (0) toobig = true;/'
# An array larger than the workspace reaches the power-of-two rounding.
mutant array-bound 's/if (toobig || (unsigned int)size > WORKSPACESIZE)/if (toobig)/'
# float takes a symbol or nil for a number.
mutant float-type '/^static object \*fn_floatfn/,/^}/{/!integerp(arg)/d}'
# documentation reads the head of a variable's list without checking it.
mutant doc-guard 's/if (val != NULL \&\& listp(val) \&\& car(val) != NULL \&\& symbolp(car(val)) \&\&/if (listp(val) \&\&/'
# A line is not clipped to the panel before it is walked.
mutant line-clip 's/if (!clipline(&x0, &y0, &x1, &y1))/if (0)/' gfx.c
# A clipped line takes its entry point from a truncating division.
mutant line-clip-round 's/qx - \*px, true)/qx - *px, false)/' gfx.c
# A circle larger than the midpoint walk takes is not drawn.
mutant circle-far 's/bigarcs(x0, y0, x1, y1, r, color, filled);/return;/' gfx.c
# A triangle with a corner far off the panel is not drawn.
mutant triangle-far 's/^	int t;$/	int t;\n\n	if (x0 < -(1 << 24) || x2 > (1 << 24))\n		return;/' gfx.c
# A name that is not a symbol reaches the variable lookup.
mutant find-nonsymbol '/^static object \*findvalue (object \*var, object \*env) {$/,/^}/{/notasymbol/d}'
# let binds an object that is not a symbol.
mutant let-nonsymbol '/if ((name == LET) || (name == LETSTAR)) {/,/goto EVAL/{/notasymbol/d}'
# A stream with no reader is read.
mutant stream-unreadable '/^static gfun_t gstreamfun (object \*args) {$/,/^}/{/streamfunction == NULL/d}'
# A dimension that is not an integer reaches the product.
mutant array-dimension-type '/^static object \*makearray (object \*dims, object \*def, bool bitp) {$/,/^}/{/!integerp(car(dims))/d}'
# write-string takes any object for a string.
mutant write-string-type '/^static object \*fn_writestring (object \*args, object \*env) {$/,/^}/s/checkstring(first(args))/first(args)/'
# sleep without an argument reads the empty list.
mutant sleep-noargument '/^static object \*fn_sleep (object \*args, object \*env) {$/,/^}/{/args == NULL/d}'
