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

#include <stdlib.h>

/*
 * C17 7.22p2 and 7.22.6: the three division result types carry quot and
 * rem in the type of the operands, and the four functions have exactly the
 * prototypes the standard gives.  A function pointer of the exact type
 * accepts only a matching declaration, so each initializer measures one
 * prototype; _Generic measures each member's type without converting it.
 */
#define IS_TYPE(expression, type) \
	_Generic((expression), type: 1, default: 0)

_Static_assert(IS_TYPE(((div_t *)0)->quot, int), "div_t.quot is int");
_Static_assert(IS_TYPE(((div_t *)0)->rem, int), "div_t.rem is int");
_Static_assert(IS_TYPE(((ldiv_t *)0)->quot, long), "ldiv_t.quot is long");
_Static_assert(IS_TYPE(((ldiv_t *)0)->rem, long), "ldiv_t.rem is long");
_Static_assert(IS_TYPE(((lldiv_t *)0)->quot, long long),
    "lldiv_t.quot is long long");
_Static_assert(IS_TYPE(((lldiv_t *)0)->rem, long long),
    "lldiv_t.rem is long long");

/* Two members and no padding, so lldiv can copy imaxdiv's result. */
_Static_assert(sizeof(div_t) == 2 * sizeof(int), "div_t holds two ints");
_Static_assert(sizeof(ldiv_t) == 2 * sizeof(long), "ldiv_t holds two longs");
_Static_assert(sizeof(lldiv_t) == 2 * sizeof(long long),
    "lldiv_t holds two long longs");

div_t (*const div_contract)(int, int) = div;
ldiv_t (*const ldiv_contract)(long, long) = ldiv;
lldiv_t (*const lldiv_contract)(long long, long long) = lldiv;
long long (*const llabs_contract)(long long) = llabs;
