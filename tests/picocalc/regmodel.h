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

/*
 * Register file for the host tests that compile a driver unchanged. The
 * shim <sys/param.h> routes PC_REG32 to shim_reg(), which finds or creates
 * a word by address; the reset-done register reads as all ones so that
 * pc_unreset() returns. A test reads and writes the model through
 * model_reg().
 */

#ifndef _TESTS_PICOCALC_REGMODEL_H_
#define _TESTS_PICOCALC_REGMODEL_H_

#define	MODEL_MAX	128

static struct {
	unsigned long	addr;
	volatile unsigned	val;
} model[MODEL_MAX];
static int model_n;

#define	MODEL_RESETS_DONE	(0x4000c000UL + 0x8)

/*
 * Interrupt mask. Every blocking spl on the RP2040 is PRIMASK, so the model
 * is one flag: splhigh() and splbio() set it and return the previous value,
 * splx() restores it. shim_spl_pin records the flag at the last read of the
 * SIO input register, where the card-detect pin is sampled.
 */
int shim_spl;
int shim_spl_pin = -1;

int
shim_splraise(void)
{
	int old = shim_spl;

	shim_spl = 1;
	return old;
}

void
shim_splx(int s)
{
	shim_spl = s;
}

volatile unsigned *
shim_reg(unsigned long addr)
{
	int i;

	if (addr == 0xd0000004UL)
		shim_spl_pin = shim_spl;
	for (i = 0; i < model_n; i++)
		if (model[i].addr == addr)
			return &model[i].val;
	if (model_n == MODEL_MAX) {
		fprintf(stderr, "regmodel: register table full\n");
		abort();
	}
	model[model_n].addr = addr;
	model[model_n].val = addr == MODEL_RESETS_DONE ? ~0U : 0;
	return &model[model_n++].val;
}

#define	model_reg(addr)		(*shim_reg(addr))

#endif
