/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_DHARA_COMPAT_STRING_H_
#define	_DHARA_COMPAT_STRING_H_

#include <sys/types.h>
#include <sys/systm.h>		/* bcopy, bzero */

/*
 * Dhara calls memcpy and memset; the kernel offers bcopy and bzero, whose
 * arguments run the other way and which cannot fill with a non-zero byte.
 */
static __inline void *
memcpy(void *d, const void *s, size_t n)
{
	bcopy(s, d, n);
	return d;
}

static __inline void *
memset(void *d, int c, size_t n)
{
	u_char *p = (u_char *)d;
	size_t i;

	if (c == 0) {
		bzero(d, n);
		return d;
	}
	for (i = 0; i < n; i++)
		p[i] = (u_char)c;
	return d;
}

/*
 * heatshrink_encoder.c slides its backlog down with memmove. machdep.c's
 * bcopy copies overlapping regions as memmove does, so the call passes
 * straight through.
 */
static __inline void *
memmove(void *d, const void *s, size_t n)
{
	bcopy(s, d, n);
	return d;
}

#endif	/* !_DHARA_COMPAT_STRING_H_ */
