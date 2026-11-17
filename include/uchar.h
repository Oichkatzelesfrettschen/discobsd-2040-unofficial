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

#ifndef _UCHAR_H_
#define _UCHAR_H_

#include <_featuretest.h>

/*
 * C17 7.28.  char16_t and char32_t are the compiler's types for u"" and
 * U"" literals.  The system's one encoding gives every byte the code of
 * its value, so each conversion handles exactly one code unit and keeps
 * no state.
 */
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned	size_t;
#endif

#ifndef _MBSTATE_T
#define _MBSTATE_T
typedef struct {
	char	__stateless;
} mbstate_t;
#endif

typedef __CHAR16_TYPE__	char16_t;
typedef __CHAR32_TYPE__	char32_t;

size_t	mbrtoc16(char16_t *_DISCOBSD_RESTRICT, const char *_DISCOBSD_RESTRICT,
	    size_t, mbstate_t *_DISCOBSD_RESTRICT);
size_t	c16rtomb(char *_DISCOBSD_RESTRICT, char16_t,
	    mbstate_t *_DISCOBSD_RESTRICT);
size_t	mbrtoc32(char32_t *_DISCOBSD_RESTRICT, const char *_DISCOBSD_RESTRICT,
	    size_t, mbstate_t *_DISCOBSD_RESTRICT);
size_t	c32rtomb(char *_DISCOBSD_RESTRICT, char32_t,
	    mbstate_t *_DISCOBSD_RESTRICT);

#endif /* !_UCHAR_H_ */
