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

#include <locale.h>
#include <string.h>

/*
 * C17 7.11.1.1.  A null locale queries the current one; "C", POSIX's
 * "POSIX" and the implementation default "" all name the one locale the
 * system has.  Any other name, or a value that is not one of the seven categories, selects
 * nothing and returns a null pointer.  The returned string is shared and
 * the caller must not modify it (7.11.1.1p8), so it needs no storage of
 * its own per category.
 */
char *
setlocale(int category, const char *locale)
{
	static char name[] = "C";

	switch (category) {
	case LC_ALL:
	case LC_COLLATE:
	case LC_CTYPE:
	case LC_MESSAGES:
	case LC_MONETARY:
	case LC_NUMERIC:
	case LC_TIME:
		break;
	default:
		return NULL;
	}
	if (locale != NULL && locale[0] != '\0' && strcmp(locale, "C") != 0 &&
	    strcmp(locale, "POSIX") != 0)
		return NULL;
	return name;
}
