/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

volatile unsigned *
shim_reg(unsigned long addr)
{
	int i;

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
