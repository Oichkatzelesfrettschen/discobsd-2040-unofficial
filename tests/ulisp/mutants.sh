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
# A negative remainder of zero takes the divisor, as (mod 5 -1) = -1.
mutant mod-sign 's/if (mod \&\& remainder != 0 \&\& (remainder<0)/if (mod \&\& (remainder<0)/'
# rem corrects its sign like mod.
mutant rem-sign 's/if (mod \&\& remainder != 0/if (remainder != 0/'
# round takes a half to the next integer, not the even one.
mutant round-even 's/d == 0.5 \&\& fmodf(f, 2.0) != 0.0/d == 0.5/'
# floor, ceiling, truncate and round divide by an integer zero.
mutant divide-zero 's/if (den == 0) error2(divisionbyzero);//'
# A float quotient beyond the integers is cast to int.
mutant float-range 's/if (!(f >= -2147483648.0 \&\& f < 2147483648.0)) error2(outofrange);//'
# ash shifts by the count as given.
mutant ash-width 's/count > 31 ? 0 : //'
# A decimal literal wider than the integers wraps.
mutant literal-wrap 's/if (big) return makefloat(fresult\*sign);//'
# A semicolon comment ends at the next opening parenthesis.
mutant comment-end 's/while(ch != .\\n. \&\& ch != -1);/while(ch != 0x28 \&\& ch != -1);/'
# A block comment does not nest.
mutant block-nest 's/else if (last == .#. \&\& ch == .|.) { depth++; ch = 0; }//'
# A three-letter character name is read as three decimal digits.
mutant char-digits 's/if (index == 3 \&\& digitvalue(buffer\[0\]) < 10 \&\& digitvalue(buffer\[1\]) < 10 \&\& digitvalue(buffer\[2\]) < 10)/if (index == 3)/'
# A bit array leaves the character after it for the next read.
mutant bitarray-lastchar 's/if (!issp(ch)) LastChar = ch;/LastChar = ch;/'
# pop of an empty place is an error.
mutant pop-nil 's/if (\*loc == NULL) return nil;//'
# A float zero prints as an integer.
mutant float-zero 's/pfstring("0.0", pfun); return;/pfun(0x30); return;/'
