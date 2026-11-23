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
 * bench: run the i86 machine for a fixed number of guest instructions with
 * no terminal, for instruction counting under qemu-arm (README.md).
 *
 *	bench IMAGE INSTRUCTIONS MODEL [KEYS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "mem.h"
#include "pc.h"
int host_ready(int ms){(void)ms;return 0;}
int host_getc(void){return -1;}
void host_write(const char *p, unsigned n){(void)p;(void)n;}
unsigned long host_ms(void){return 0;}
unsigned long host_us(void){return 0;}
void host_sleep(unsigned ms){(void)ms;}
void host_exit(int code){exit(code);}
int _unlink(const char *p){(void)p;return 0;}
int _gettimeofday(struct timeval *tv, void *tz){(void)tz; tv->tv_sec=631195200; tv->tv_usec=0; return 0;}
#ifdef BENCH_H
/* The newlib build (newlib/bench.h): the panel is absent and the page
 * file is never named, so both calls fail as they would without a device. */
int unlinkat_beneath(int dirfd, const char *name, int fd){(void)dirfd;(void)name;(void)fd;return -1;}
int ioctl(int fd, int request, ...){(void)fd;(void)request;return -1;}
#endif
#ifdef COMPOSITION
/* What the next step is: a step is one instruction, except that a repeated
 * string instruction runs up to 256 iterations in it and a halted
 * processor runs none. */
static long reps, iterations, halts;
static void composition(const struct i86 *c){
	uint32_t a = ((uint32_t)c->s[S_CS] << 4) + c->ip;
	unsigned op, rep = 0, k;
	if (c->halted) { halts++; return; }
	for (k = 0; k < 4; k++, a++) {
		op = mem_rb(a & (MEM_SIZE - 1));
		if (op == 0xf2 || op == 0xf3) rep = 1;
		else if (op != 0x26 && op != 0x2e && op != 0x36 && op != 0x3e && op != 0xf0) break;
	}
	if (rep && ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf) || (op >= 0x6c && op <= 0x6f))) {
		reps++;
		iterations += c->r[R_CX] > 256 ? 256 : c->r[R_CX];
	}
}
#endif
int main(int argc, char **argv){
	static struct i86 cpu; struct pc_config cf = {0};
	long n = atol(argv[2]), i, polls;
	cf.disk[0]=argv[1]; cf.ram_kb=640; cf.cols=80; cf.model=atoi(argv[3]); cf.fixed_clock=1; cf.quiet=1; cf.readonly=1; cf.keys = argc > 4 ? argv[4] : "";
	if (mem_init(640u<<10, 640, "/dev/null")) return 7;
	pc_init(&cpu,&cf);
	/* main.c's loop: runs of pc_slice steps counted in pc_step, a poll
	 * after each. The run ends after INSTRUCTIONS steps, at a halt with
	 * interrupts off, or after INSTRUCTIONS polls. */
	for (i=0, polls=0; i<n && polls<n && (!cpu.halted || (cpu.fl & F_IF)); polls++){
		/* The last run stops at the budget; pc_poll sets pc_slice
		 * again, so the loop below stays main.c's. */
		if ((long)pc_slice > n-i)
			pc_slice = (unsigned)(n-i);
		for (pc_step=0; pc_step<pc_slice && !pc_idle && (!cpu.halted ||
		    (cpu.irq && (cpu.fl & F_IF))); pc_step++){
#ifdef COMPOSITION
			composition(&cpu);
#endif
			i86_step(&cpu);
		}
		i += pc_step;
		pc_poll(&cpu);
	}
#ifdef COMPOSITION
	printf("%ld steps: %ld repeated string instructions with %ld iterations, "
	    "%ld halted\n", n, reps, iterations, halts);
#endif
	return 0;
}
