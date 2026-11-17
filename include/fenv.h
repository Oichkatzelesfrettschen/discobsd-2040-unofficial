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

#ifndef _FENV_H_
#define _FENV_H_

/*
 * C17 7.6.  Floating-point arithmetic on this system is software: it
 * rounds to nearest and keeps no status flags.  7.6p6 defines an exception
 * macro only where the implementation supports the exception, so none is
 * defined and FE_ALL_EXCEPT is 0; 7.6p8 likewise leaves FE_TONEAREST as
 * the only rounding direction.  The environment has no state, and the
 * functions report success exactly for the requests that state can
 * represent.
 */
typedef int	fenv_t;
typedef int	fexcept_t;

#define FE_ALL_EXCEPT	0
#define FE_TONEAREST	0

extern const fenv_t	__fe_dfl_env;
#define FE_DFL_ENV	(&__fe_dfl_env)

int	feclearexcept(int);
int	fegetexceptflag(fexcept_t *, int);
int	feraiseexcept(int);
int	fesetexceptflag(const fexcept_t *, int);
int	fetestexcept(int);
int	fegetround(void);
int	fesetround(int);
int	fegetenv(fenv_t *);
int	feholdexcept(fenv_t *);
int	fesetenv(const fenv_t *);
int	feupdateenv(const fenv_t *);

#endif /* !_FENV_H_ */
