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

/*
 * What NetBSD's libc supplies around its libm members, for an oracle built
 * by netbsd_oracle.py and linked into a host or newlib program.  This file
 * compiles against the hosting library's headers.
 *
 * __errno: NetBSD's errno is (*__errno()); newlib already defines that
 * function, and glibc gets this one over its own errno.
 *
 * __sF: NetBSD's standard streams.  k_standard.c refers to them only on
 * the SVID error path, which the default _LIB_VERSION of _POSIX_ never
 * takes, so the array is storage for the reference and never read.
 *
 * The _fe names: on the soft-float target there are no status flags and
 * the only rounding direction is to nearest, which NetBSD's <arm/fenv.h>
 * spells 0.  Requests for anything else fail, as the tree's <fenv.h> does.
 * The x86 oracles take NetBSD's own fenv.c instead.  The i387 one asks
 * sysctlbyname("machdep.sse") whether SSE registers need its rounding
 * mode too; on the hosts these oracles run on they do.
 *
 * __isthreaded and the __libc_mutex hooks: gdtoa's allocator locks only a
 * threaded program, and the oracle's programs have one thread.
 */
#include <errno.h>
#include <stddef.h>
#include <string.h>

#ifndef __NEWLIB__
int *__errno(void);

int *
__errno(void)
{
	return &errno;
}
#endif

unsigned char __sF[3][256];

#ifdef __arm__
int _feclearexcept(int);
int _fegetenv(void *);
int _fegetround(void);
int _feholdexcept(void *);
int _feraiseexcept(int);
int _fesetenv(const void *);
int _fesetround(int);
int _fetestexcept(int);
int _feupdateenv(const void *);

int _feclearexcept(int excepts) { return excepts != 0; }
int _fegetenv(void *environment) { (void)environment; return 0; }
int _fegetround(void) { return 0; }
int _feholdexcept(void *environment) { (void)environment; return 0; }
int _feraiseexcept(int excepts) { return excepts != 0; }
int _fesetenv(const void *environment) { (void)environment; return 0; }
int _fesetround(int round) { return round != 0; }
int _fetestexcept(int excepts) { (void)excepts; return 0; }
int _feupdateenv(const void *environment) { (void)environment; return 0; }
#endif

int __isthreaded;
int __libc_mutex_lock(void *);
int __libc_mutex_unlock(void *);

int __libc_mutex_lock(void *mutex) { (void)mutex; return 0; }
int __libc_mutex_unlock(void *mutex) { (void)mutex; return 0; }

/*
 * Only the 32-bit x86 oracle build, whose libm asks machdep.sse; the Arm
 * build has 32-bit pointers too. The test names no x86 macro, which the
 * architecture-isolation selector scan would list.
 */
#if __SIZEOF_POINTER__ == 4 && !defined(__arm__)
int sysctlbyname(const char *, void *, size_t *, const void *, size_t);

int
sysctlbyname(const char *name, void *old, size_t *length, const void *new,
    size_t newlength)
{
	(void)new;
	(void)newlength;
	if (strcmp(name, "machdep.sse") != 0 || old == NULL ||
	    *length < sizeof(int))
		return -1;
	*(int *)old = 1;
	*length = sizeof(int);
	return 0;
}
#endif
