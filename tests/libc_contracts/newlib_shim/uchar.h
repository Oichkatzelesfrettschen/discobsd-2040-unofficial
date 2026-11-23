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

#ifndef _NEWLIB_SHIM_UCHAR_H_
#define _NEWLIB_SHIM_UCHAR_H_

/*
 * The newlib that hosts the qemu-user variants lacks <uchar.h>.  This
 * stand-in sits last on the include path of those builds and declares the
 * four functions against newlib's own mbstate_t, so the tree's members
 * and the fixture translate there as they do against a hosting library
 * that has the header.
 */
#include <wchar.h>

typedef __CHAR16_TYPE__	char16_t;
typedef __CHAR32_TYPE__	char32_t;

size_t	mbrtoc16(char16_t *restrict, const char *restrict, size_t,
	    mbstate_t *restrict);
size_t	c16rtomb(char *restrict, char16_t, mbstate_t *restrict);
size_t	mbrtoc32(char32_t *restrict, const char *restrict, size_t,
	    mbstate_t *restrict);
size_t	c32rtomb(char *restrict, char32_t, mbstate_t *restrict);

#endif /* !_NEWLIB_SHIM_UCHAR_H_ */
