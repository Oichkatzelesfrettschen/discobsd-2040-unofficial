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

#include <fenv.h>

/*
 * C17 7.6 for an implementation without floating-point status flags or
 * dynamic rounding: arithmetic is software, rounds to nearest and records
 * nothing.  <fenv.h> therefore defines no exception macro, FE_ALL_EXCEPT
 * is 0 and FE_TONEAREST is the only rounding direction.  Each function
 * succeeds for the requests that are representable (no exceptions, round
 * to nearest) and reports failure for any other.
 */
const fenv_t __fe_dfl_env = 0;

int
feclearexcept(int excepts)
{
	return excepts != 0;
}

int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = 0;
	return excepts != 0;
}

int
feraiseexcept(int excepts)
{
	return excepts != 0;
}

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	(void)flagp;
	return excepts != 0;
}

int
fetestexcept(int excepts)
{
	(void)excepts;
	return 0;
}

int
fegetround(void)
{
	return FE_TONEAREST;
}

int
fesetround(int round)
{
	return round != FE_TONEAREST;
}

int
fegetenv(fenv_t *envp)
{
	*envp = 0;
	return 0;
}

int
feholdexcept(fenv_t *envp)
{
	*envp = 0;
	return 0;
}

int
fesetenv(const fenv_t *envp)
{
	(void)envp;
	return 0;
}

int
feupdateenv(const fenv_t *envp)
{
	(void)envp;
	return 0;
}
