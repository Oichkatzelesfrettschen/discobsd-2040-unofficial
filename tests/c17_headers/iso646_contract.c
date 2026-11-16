/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <iso646.h>

/*
 * C17 7.9: each of the eleven spellings is the operator it names.  Every
 * assertion picks operands on which the intended operator and its nearest
 * neighbors disagree, so a macro bound to the wrong punctuator fails here.
 * The #ifndef chain covers a macro that is missing altogether, which would
 * otherwise parse as an undeclared identifier with a less direct message.
 */
#if !defined(and) || !defined(and_eq) || !defined(bitand) || \
    !defined(bitor) || !defined(compl) || !defined(not) || \
    !defined(not_eq) || !defined(or) || !defined(or_eq) || \
    !defined(xor) || !defined(xor_eq)
#error "<iso646.h> lacks one of the eleven C17 7.9 macros"
#endif

_Static_assert((2 and 4) == 1, "and is logical: 2 & 4 is 0");
_Static_assert((0 or 2) == 1, "or is logical: 0 | 2 is 2");
_Static_assert((not 2) == 0, "not is logical: ~2 is -3");
_Static_assert((6 bitand 3) == 2, "bitand is bitwise: 6 && 3 is 1");
_Static_assert((6 bitor 3) == 7, "bitor is bitwise: 6 || 3 is 1");
_Static_assert((6 xor 3) == 5, "xor differs from both | and &");
_Static_assert((compl 0) == -1, "compl is bitwise: !0 is 1");
_Static_assert((2 not_eq 3) == 1, "not_eq is the inequality operator");
_Static_assert((2 not_eq 2) == 0, "not_eq is the inequality operator");

int iso646_assignments(void);

/* The compound assignments are not constant expressions; 6 becomes 4. */
int
iso646_assignments(void)
{
	int value = 6;

	value and_eq 3;		/* 6 & 3 == 2 */
	value or_eq 5;		/* 2 | 5 == 7 */
	value xor_eq 3;		/* 7 ^ 3 == 4 */
	return value;
}
