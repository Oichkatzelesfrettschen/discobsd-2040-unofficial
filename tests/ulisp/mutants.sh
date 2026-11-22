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
# interpreter is blind. Each defect is one substitution in ulisp.c
# that must change the file.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$1
doc=$2
cc=$3

# check.sh runs the interpreter under env -i, which leaves zero PATH entries to find
# timeout(1) in: a name that resolves in this shell (coreutils installs gtimeout
# on macOS, outside the default path) is passed as a full path. A looping mutant
# needs the bound, so a host with neither is an error.
tmo=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)
if [ -z "$tmo" ]; then
	echo "ulisp mutants: timeout(1) from coreutils is required; a looping mutant would not end" >&2
	exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/ulisp-mutants.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# The race corpus runs when the Makefile has built its shim (Linux only);
# the image-reread mutant needs it
shim=
# A shim left by a Linux build in a tree that macOS now runs is not a shim: the
# platform decides, and the file only says whether the Linux build made it.
if [ "$(uname -s)" = Linux ] && [ -f "$here/rewrite-race.so" ]; then
	shim=$here/rewrite-race.so
fi

# mutant NAME SED-EXPRESSION [SOURCE-FILE [STATUS [CFLAGS]]]
# STATUS is the one status of check.sh that rejects the mutant: 1, a failed
# or differing transcript, unless the defect is a run that never ends, which
# check.sh reports as 3. Any other nonzero status is a failure of the harness.
# CFLAGS are extra compiler options, a word list, for a defect that only some
# hardware turns into a failure (an overflowing division traps on x86 and
# wraps on arm64) and that a sanitizer reports on every host.
mutant() {
	dir=$work/$1
	file=${3:-ulisp.c}
	want=${4:-1}
	extra=${5:-}
	mkdir "$dir"
	cp "$src"/*.c "$src"/*.h "$src"/builtins.def "$dir"/
	sed "$2" "$src/$file" > "$dir/$file"
	if cmp -s "$src/$file" "$dir/$file"; then
		echo "ulisp mutants: $1: the substitution matched nothing" >&2
		exit 1
	fi
	# shellcheck disable=SC2086 # extra is a word list by construction
	$cc -std=c17 -D_DEFAULT_SOURCE -Os -w $extra -I"$src/../../sys/arch/rp2040/dev" \
	    -o "$dir/ulisp" "$dir/ulisp.c" "$dir/unix.c" "$dir/hilite.c" "$dir/gfx.c" \
	    "$dir/dev_host.c" "$src/../../sys/arch/rp2040/dev/lcdfont.c" -lm
	rc=0
	ULISP_REWRITE_SHIM=$shim sh "$here/check.sh" host "$doc" "$tmo" 60 "$dir/ulisp" \
	    > "$dir/log" 2>&1 || rc=$?
	if [ "$rc" -eq 0 ]; then
		echo "ulisp mutants: $1 passed the check" >&2
		exit 1
	fi
	if [ "$rc" -ne "$want" ]; then
		echo "ulisp mutants: $1: status $rc from the check, $want rejects it" >&2
		tail -5 "$dir/log" >&2
		exit 1
	fi
	echo "ulisp mutants: $1 rejected"
}

# hmutant NAME SED-EXPRESSION: the same for hilite.c, which hilite-test must reject
hmutant() {
	dir=$work/$1
	mkdir "$dir"
	sed "$2" "$src/hilite.c" > "$dir/hilite.c"
	if cmp -s "$src/hilite.c" "$dir/hilite.c"; then
		echo "ulisp mutants: $1: the substitution matched nothing" >&2
		exit 1
	fi
	$cc -std=c17 -D_DEFAULT_SOURCE -w -I"$src" -o "$dir/hilite-test" \
	    "$here/hilite-test.c" "$dir/hilite.c"
	rc=0
	"$dir/hilite-test" > "$dir/log" 2>&1 || rc=$?
	if [ "$rc" -eq 0 ]; then
		echo "ulisp mutants: $1 passed the check" >&2
		exit 1
	fi
	if [ "$rc" -ne 1 ]; then
		echo "ulisp mutants: $1: status $rc from hilite-test, 1 rejects it" >&2
		exit 1
	fi
	echo "ulisp mutants: $1 rejected"
}

# pmutant NAME FILE SED-EXPRESSION: the interpreter with one defect in FILE
# that hilite-pty.py must reject
pmutant() {
	dir=$work/$1
	mkdir "$dir"
	cp "$src"/*.c "$src"/*.h "$src"/builtins.def "$dir"/
	sed "$3" "$src/$2" > "$dir/$2"
	if cmp -s "$src/$2" "$dir/$2"; then
		echo "ulisp mutants: $1: the substitution matched nothing" >&2
		exit 1
	fi
	$cc -std=c17 -D_DEFAULT_SOURCE -Os -w -I"$src/../../sys/arch/rp2040/dev" \
	    -o "$dir/ulisp" "$dir/ulisp.c" "$dir/unix.c" "$dir/hilite.c" "$dir/gfx.c" \
	    "$dir/dev_host.c" "$src/../../sys/arch/rp2040/dev/lcdfont.c" -lm
	rc=0
	"${PYTHON:?set PYTHON}" -I "$here/hilite-pty.py" "$dir/ulisp" "$doc" \
	    > "$dir/log" 2>&1 || rc=$?
	if [ "$rc" -eq 0 ]; then
		echo "ulisp mutants: $1 passed the check" >&2
		exit 1
	fi
	if [ "$rc" -ne 1 ]; then
		echo "ulisp mutants: $1: status $rc from hilite-pty, 1 rejects it" >&2
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
# Integer division of the most negative int by -1 reaches the remainder.
mutant divide-min '/(result == INT_MIN) \&\& (i == -1)/d'
# A left shift by the width of the type reaches the C shift.
mutant ash-width 's/return number((count >= 32) ? 0 : (int)((unsigned int)value << count));/return number(value << count);/'
# A float outside the int range converts to an int.
mutant float-to-int 's/if (!(f >= -2147483648.0 \&\& f < 2147483648.0)) return makefloat(f);//'
# The most negative int divided by -1 reaches the C division.
mutant quotient-min 's/if (den == -1 \&\& num == INT_MIN) return makefloat(-(float)num);//'
# The loader trusts its first check of the file and installs a second read.
# Only the race corpus, which rewrites the file between the two reads through
# the LD_PRELOAD shim, rejects it. The shim is built on Linux only: a Linux host
# without it has a failed build, which is an error, and any other host skips
# that corpus and the mutant.
if [ -n "$shim" ]; then
	mutant image-reread 's/^  fault = checkimage(header, imagesize);$/  fault = NULL;/'
elif [ "$(uname -s)" = Linux ]; then
	echo "ulisp mutants: image-reread needs rewrite-race.so, which the Linux build did not produce" >&2
	exit 1
else
	echo "SKIP ulisp mutants: image-reread needs the rewrite-race shim (Linux LD_PRELOAD); no execution occurred"
fi
# A loaded image is not checked against its own span.
mutant image-unchecked 's/const char \*fault = checkimage(header, imagesize);/const char *fault = NULL;/'
# A pair may point into a string chain, whose cells hold characters.
mutant image-chain-target 's/return chainbit(chains, index) ? imagelinked : NULL;/return NULL;/'
# A random limit of zero reaches the modulus.
mutant random-zero 's/if (arg->integer <= 0) error("limit must be positive", arg);//'
# A negative count waits two seconds instead of returning at once; only the
# upper bound of the for-millis rows of the arith corpus notices. The bound
# is 1500 ms above the request: a form the host deschedules for longer fails.
mutant for-millis-negative-wait 's/total = (count < 0) ? 0 : (unsigned long)count;/total = (count < 0) ? 2000 : (unsigned long)count;/'
# The remainder of INT_MIN by -1 reaches the division. x86 traps (SIGFPE);
# arm64 returns 0, which is the right answer, so the unsanitized program
# passes there and the overflow is reported by the sanitizer instead.
mutant rem-overflow 's/(divisor == -1) ? 0 : dividend % divisor/dividend % divisor/' \
    ulisp.c 1 '-fsanitize=signed-integer-overflow -fno-sanitize-recover=all'
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
mutant round-even 's/d == 0.5 \&\& fmodx(f, 2.0) != 0.0/d == 0.5/'
# floor, ceiling, truncate and round divide by an integer zero.
mutant divide-zero 's/if (den == 0) error2(divisionbyzero);//'
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
# The image loader does not check an array against its dimensions.
mutant array-unchecked 's/if (fault == NULL) fault = imagearraycheck(\&ic, cd);//'
# A tree node may be reached twice, so a tree may loop or share nodes.
mutant tree-claim 's/if (chainbit(ic->trees, index)) return imagearray;//'
# A leaf of a bit array need not be a number cell.
mutant bit-leaf 's/else if (bits) fault = imagenumber(ic, child, \&leaf);//'
# A dimension list may loop.
mutant dim-loop 's/if (rank >= ic->cells) return imagearray;//' ulisp.c 3

# The highlighter's tokenizer: a backslash in a string does not escape.
hmutant hl-string-escape 's/if (c == .\\\\. \&\& j < n)/if (0)/'
# A block comment does not nest.
hmutant hl-block-nest 's/else if (c == .#. \&\& c2 == .|.) {/else if (0) {/'
# A semicolon does not end a token.
hmutant hl-semicolon 's/ || (c) == .;.)/)/'
# The sign of an exponent is not a digit.
hmutant hl-exponent-sign 's/else if (exponent \&\& (c == .-. || c == .+.))/else if (0)/'
# A character literal of a delimiter takes the name after it.
hmutant hl-char-delimiter 's/if (j < n \&\& DELIM(s\[j\]))/if (0)/'
# A closing parenthesis with nothing open is a parenthesis.
hmutant hl-unbalanced 's/k = st->depth ? HL_PAREN : HL_ERROR;/k = HL_PAREN;/'
# A radix token with a bad digit is a number.
hmutant hl-radix-digit 's/HL_NUMBER : HL_PLAIN;/HL_NUMBER : HL_NUMBER;/'
# A token that begins with a colon is an ordinary symbol.
hmutant hl-keyword 's/else if (c == .:.)/else if (0)/'

# A special form takes the color of a function.
pmutant hl-table-class ulisp.c 's/return (fntype(x) != OTHER_FORMS/return HL_FUNCTION; return (fntype(x) != OTHER_FORMS/'
# The editor does not move up over the rows of a wrapped line.
pmutant hl-wrap-redraw unix.c 's/if (dr != tr) {/if (0) {/'
# The match highlight stays after the next key.
pmutant hl-match-stays unix.c 's/cls\[m\] |= 0x80;/cls[m] |= 0x00;/'
# The editor colors a dumb terminal.
pmutant hl-dumb unix.c 's/strcmp(term, "dumb") != 0/1/'
# The Left key is ignored.
pmutant ed-left unix.c "s/key == 'D' && pos > 0/0/"
# The Delete key is ignored.
pmutant ed-delete unix.c 's/arg == 3 && pos < n/0/'
# The cursor of a full row is not brought back to the end of the line.
pmutant ed-full-row unix.c 's/if (back != to)/if (0)/'
# The printed value is not colored.
pmutant value-color unix.c 's/vhl = hlok;/vhl = false;/'
# float takes a symbol or nil for a number.
mutant float-type '/^static object \*fn_floatfn/,/^}/{/!integerp(arg)/d;}'
# documentation reads the head of a variable's list without checking it.
mutant doc-guard 's/if (val != NULL \&\& listp(val) \&\& car(val) != NULL \&\& symbolp(car(val)) \&\&/if (listp(val) \&\&/'
# A line is not clipped to the panel before it is walked: the walk of a
# far-off line does not end within the run's time.
mutant line-clip 's/if (!clipline(&x0, &y0, &x1, &y1))/if (0)/' gfx.c 3
# A clipped line takes its entry point from a truncating division.
mutant line-clip-round 's/qx - \*px, true)/qx - *px, false)/' gfx.c
# A circle larger than the midpoint walk takes is not drawn.
mutant circle-far 's/bigarcs(x0, y0, x1, y1, r, color, filled);/return;/' gfx.c
# A triangle with a corner far off the panel is not drawn.
mutant triangle-far 's/^	int t;$/	int t;\
\
	if (x0 < -(1 << 24) || x2 > (1 << 24))\
		return;/' gfx.c
# A name that is not a symbol reaches the variable lookup.
mutant find-nonsymbol '/^static object \*findvalue (object \*var, object \*env) {$/,/^}/{/notasymbol/d;}'
# let binds an object that is not a symbol.
mutant let-nonsymbol '/if ((name == LET) || (name == LETSTAR)) {/,/goto EVAL/{/notasymbol/d;}'
# A stream with no reader is read.
mutant stream-unreadable '/^static gfun_t gstreamfun (object \*args) {$/,/^}/{/streamfunction == NULL/d;}'
# A dimension that is not an integer reaches the product.
mutant array-dimension-type '/^static object \*makearray (object \*dims, object \*def, bool bitp) {$/,/^}/{/!integerp(car(dims))/d;}'
# write-string takes any object for a string.
mutant write-string-type '/^static object \*fn_writestring (object \*args, object \*env) {$/,/^}/s/checkstring(first(args))/first(args)/'
# sleep without an argument reads the empty list.
mutant sleep-noargument '/^static object \*fn_sleep (object \*args, object \*env) {$/,/^}/{/args == NULL/d;}'
# An interrupted read of the console is taken as the end of input.
mutant read-eintr 's/while (n < 0 \&\& errno == EINTR);/while (0);/' unix.c

# A top-level return leaves the return flag set for the next form.
mutant return-flag '/clrflag(RETURNFLAG); \/\/ a return outside a loop/d'
# (expt 0 0) takes the logarithm of zero.
mutant expt-zero 's/(power == 0) ? 0 : //'
# defvar evaluates the form of a variable that is bound.
mutant defvar-bound '/if (pair != NULL) return var;/d'
# ignore-errors yields no value.
mutant ignore-nothing 's/if (signaled) return nil;/if (signaled) return bsymbol(NOTHING);/'
# write-string and write-line return nil.
mutant write-string-nil '/^static object \*fn_writestring (object \*args, object \*env) {$/,/^}/s/return obj;/return nil;/'
mutant write-line-nil '/^static object \*fn_writeline (object \*args, object \*env) {$/,/^}/s/return obj;/return nil;/'
# decf past the smallest integer stays an integer.
mutant decf-min 's/sum < INT_MIN || sum > INT_MAX/sum > INT_MAX/'
# A note below zero does not count down from the octave.
mutant note-floor 's/if (semitone < 0) { semitone = semitone + 12; whole--; }/if (semitone < 0) semitone = semitone + 12;/'
# The float remainder subtracts the divisor once at a time, which for a huge
# quotient does not end in the run's time.
mutant fmodx-linear 's/while (t + t <= ax) t = t + t;//' ulisp.c 3
# The clipping division loses a quotient bit.
mutant muldiv-bit 's/q |= 1ULL << i;/q |= 0;/' gfx.c
