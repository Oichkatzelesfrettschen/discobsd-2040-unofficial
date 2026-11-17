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

#ifndef _WCTYPE_H_
#define _WCTYPE_H_

/*
 * C17 7.30.  The classes are those of <ctype.h> in the "C" locale, which
 * cover the basic character set; a wide character outside it belongs to no
 * class and has no case mapping.  wctype_t and wctrans_t are small
 * integers in which 0 is the invalid handle.
 */
#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__	wint_t;
#endif

typedef int	wctrans_t;
typedef int	wctype_t;

#ifndef WEOF
#define WEOF		((wint_t)-1)
#endif

int	 iswalnum(wint_t);
int	 iswalpha(wint_t);
int	 iswblank(wint_t);
int	 iswcntrl(wint_t);
int	 iswdigit(wint_t);
int	 iswgraph(wint_t);
int	 iswlower(wint_t);
int	 iswprint(wint_t);
int	 iswpunct(wint_t);
int	 iswspace(wint_t);
int	 iswupper(wint_t);
int	 iswxdigit(wint_t);
int	 iswctype(wint_t, wctype_t);
wctype_t wctype(const char *);
wint_t	 towlower(wint_t);
wint_t	 towupper(wint_t);
wint_t	 towctrans(wint_t, wctrans_t);
wctrans_t wctrans(const char *);

#endif /* !_WCTYPE_H_ */
