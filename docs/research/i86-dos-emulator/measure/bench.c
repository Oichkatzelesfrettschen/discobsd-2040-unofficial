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
void host_exit(int code){exit(code);}
int _unlink(const char *p){(void)p;return 0;}
int _gettimeofday(struct timeval *tv, void *tz){(void)tz; tv->tv_sec=631195200; tv->tv_usec=0; return 0;}
int main(int argc, char **argv){
	static struct i86 cpu; struct pc_config cf = {0};
	long n = atol(argv[2]), i;
	cf.disk[0]=argv[1]; cf.ram_kb=640; cf.cols=80; cf.model=atoi(argv[3]); cf.fixed_clock=1; cf.quiet=1; cf.readonly=1; cf.keys = argc > 4 ? argv[4] : "";
	if (mem_init(640u<<10, 640, "/dev/null")) return 7;
	pc_init(&cpu,&cf);
	for (i=0;i<n;i++){ if ((i&2047)==0) pc_poll(&cpu); i86_step(&cpu);} 
	return 0;
}
