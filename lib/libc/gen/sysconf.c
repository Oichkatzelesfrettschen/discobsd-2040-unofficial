/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/signal.h>

#include <errno.h>
#include <unistd.h>

_Static_assert(NSIG == 1 << 5, "sysconf signal encoding must match NSIG");
_Static_assert(DEV_BSIZE == 1 << 10,
    "sysconf page-size encoding must match DEV_BSIZE");

long
sysconf(int name)
{
	unsigned int selector = (unsigned int)name;

	if (selector <= _SC_OPEN_MAX) {
		if (selector < _SC_NGROUPS_MAX) {
			if (selector == _SC_ARG_MAX)
				return ARG_MAX;
			if (selector == _SC_CLK_TCK)
				return CLK_TCK;
		} else {
			if (selector == _SC_NGROUPS_MAX)
				return NGROUPS_MAX;
			if (selector == _SC_OPEN_MAX)
				return OPEN_MAX;
		}
	} else {
		if (selector <= _SC_NPROCESSORS_ONLN)
			return 1;
		if (selector <= _SC_PAGESIZE)
			return 1L << ((selector - _SC_NPROCESSORS_ONLN) * 5);
		if (selector == _SC_DEVICE_CONTROL)
			return _POSIX_DEVICE_CONTROL;
	}
	errno = EINVAL;
	return -1;
}
