/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * sstrun: execute single instructions for tests/sst.py.
 *
 * Each input line holds fourteen register values (ax bx cx dx cs ss ds es
 * sp bp si di ip flags), a count of address-value pairs to store, the
 * pairs, a count of addresses to report and the addresses, all decimal.
 * The reply holds the registers after one i86_step() and the bytes at the
 * reported addresses. The first argument selects the processor model.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../i86.h"
#include "../mem.h"

int
pc_trap(struct i86 *c, unsigned n)
{
	(void)c;
	(void)n;
	return (1);
}

unsigned
pc_inta(struct i86 *c)
{
	c->irq = 0;
	return (0);
}

unsigned
pc_in(struct i86 *c, unsigned port, int w)
{
	(void)c;
	(void)port;
	return (w ? 0xffff : 0xff);
}

void
pc_out(struct i86 *c, unsigned port, unsigned v, int w)
{
	(void)c;
	(void)port;
	(void)v;
	(void)w;
}

#ifdef SSTRUN_NEWLIB
/* newlib's unlink() for the qemu-arm build, whose page store is never
 * opened: tests/libbn/qemu_support.c has no such call. */
int _unlink(const char *path);

int
_unlink(const char *path)
{
	(void)path;
	return (0);
}
#endif

static int
num(unsigned long *v)
{
	return (scanf("%lu", v) == 1);
}

int
main(int argc, char **argv)
{
	static const uint8_t order[12] = {
		R_AX, R_BX, R_CX, R_DX, 8 + S_CS, 8 + S_SS, 8 + S_DS, 8 + S_ES,
		R_SP, R_BP, R_SI, R_DI
	};
	struct i86 c;
	unsigned long v, n, a;
	unsigned i;

	i86_reset(&c, argc > 1 ? atoi(argv[1]) : I86_8086);
	if (mem_init(MEM_SIZE, argc > 2 ? (unsigned)atoi(argv[2]) : MEM_NPAGE,
	    argc > 3 ? argv[3] : "sstrun.store") != 0)
		return (2);
	for (;;) {
		for (i = 0; i < 12; i++) {
			if (!num(&v))
				goto out;
			if (order[i] < 8)
				c.r[order[i]] = (uint16_t)v;
			else
				c.s[order[i] - 8] = (uint16_t)v;
		}
		if (!num(&v))
			return (2);
		c.ip = (uint16_t)v;
		if (!num(&v))
			return (2);
		i86_setfl(&c, (unsigned)v);
		c.events = 0;
		if (!num(&n))
			return (2);
		while (n--) {
			if (!num(&a) || !num(&v))
				return (2);
			mem_wb((uint32_t)a & (MEM_SIZE - 1), (unsigned)v);
		}
		i86_step(&c);
		for (i = 0; i < 12; i++)
			printf("%u ", order[i] < 8 ? c.r[order[i]] :
			    c.s[order[i] - 8]);
		printf("%u %u", c.ip, c.fl);
		if (!num(&n))
			return (2);
		while (n--) {
			if (!num(&a))
				return (2);
			printf(" %u", mem_rb((uint32_t)a & (MEM_SIZE - 1)));
		}
		putchar('\n');
		fflush(stdout);
	}
out:
	mem_fini();
	return (0);
}
