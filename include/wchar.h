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

#ifndef _WCHAR_H_
#define _WCHAR_H_

#include <_featuretest.h>

/*
 * C17 7.29 over the system's one encoding: each byte is one character and
 * its wide value is the byte value (lib/libc/stdlib/mbtowc.c).  Conversion
 * therefore keeps no state and mbstate_t carries none.  wchar_t and wint_t
 * are the compiler's, so wide literals and promoted arguments match.
 *
 * Not provided: the wide stream functions of 7.29.2 and 7.29.3 (fwprintf,
 * fgetwc and the rest) and wcsftime.  stdio's FILE records no orientation
 * and its formatter has no wide conversions, so those need stdio work of
 * their own; a program that calls one fails to compile rather than link.
 */
#ifndef NULL
#define NULL		((void *)0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned	size_t;
#endif

#ifndef _WCHAR_T
#define _WCHAR_T
typedef __WCHAR_TYPE__	wchar_t;
#endif

#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__	wint_t;
#endif

#ifndef _MBSTATE_T
#define _MBSTATE_T
typedef struct {
	char	__stateless;
} mbstate_t;
#endif

#ifndef WCHAR_MAX
#define WCHAR_MAX	__WCHAR_MAX__
#define WCHAR_MIN	__WCHAR_MIN__
#endif
#ifndef WEOF
#define WEOF		((wint_t)-1)
#endif

struct tm;

wchar_t	*wcscpy(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT);
wchar_t	*wcsncpy(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT, size_t);
wchar_t	*wmemcpy(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT, size_t);
wchar_t	*wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t	*wcscat(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT);
wchar_t	*wcsncat(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT, size_t);
int	 wcscmp(const wchar_t *, const wchar_t *);
int	 wcscoll(const wchar_t *, const wchar_t *);
int	 wcsncmp(const wchar_t *, const wchar_t *, size_t);
size_t	 wcsxfrm(wchar_t *_DISCOBSD_RESTRICT,
	    const wchar_t *_DISCOBSD_RESTRICT, size_t);
int	 wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t	*wcschr(const wchar_t *, wchar_t);
size_t	 wcscspn(const wchar_t *, const wchar_t *);
wchar_t	*wcspbrk(const wchar_t *, const wchar_t *);
wchar_t	*wcsrchr(const wchar_t *, wchar_t);
size_t	 wcsspn(const wchar_t *, const wchar_t *);
wchar_t	*wcsstr(const wchar_t *, const wchar_t *);
wchar_t	*wcstok(wchar_t *_DISCOBSD_RESTRICT, const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT);
wchar_t	*wmemchr(const wchar_t *, wchar_t, size_t);
size_t	 wcslen(const wchar_t *);
wchar_t	*wmemset(wchar_t *, wchar_t, size_t);

wint_t	 btowc(int);
int	 wctob(wint_t);
int	 mbsinit(const mbstate_t *);
size_t	 mbrlen(const char *_DISCOBSD_RESTRICT, size_t,
	    mbstate_t *_DISCOBSD_RESTRICT);
size_t	 mbrtowc(wchar_t *_DISCOBSD_RESTRICT, const char *_DISCOBSD_RESTRICT,
	    size_t, mbstate_t *_DISCOBSD_RESTRICT);
size_t	 wcrtomb(char *_DISCOBSD_RESTRICT, wchar_t,
	    mbstate_t *_DISCOBSD_RESTRICT);
size_t	 mbsrtowcs(wchar_t *_DISCOBSD_RESTRICT,
	    const char **_DISCOBSD_RESTRICT, size_t,
	    mbstate_t *_DISCOBSD_RESTRICT);
size_t	 wcsrtombs(char *_DISCOBSD_RESTRICT,
	    const wchar_t **_DISCOBSD_RESTRICT, size_t,
	    mbstate_t *_DISCOBSD_RESTRICT);

double	 wcstod(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT);
float	 wcstof(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT);
long	 wcstol(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT, int);
long long wcstoll(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT, int);
unsigned long wcstoul(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT, int);
unsigned long long wcstoull(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT, int);
/* long double has double's format on this ABI; see strtold in <stdlib.h>. */
#if defined(__LDBL_MANT_DIG__) && __LDBL_MANT_DIG__ == __DBL_MANT_DIG__
long double wcstold(const wchar_t *_DISCOBSD_RESTRICT,
	    wchar_t **_DISCOBSD_RESTRICT) __asm__("wcstod");
#endif

#endif /* !_WCHAR_H_ */
