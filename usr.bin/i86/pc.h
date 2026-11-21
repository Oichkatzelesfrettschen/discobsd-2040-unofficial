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
 * The machine around the processor: firmware services, text display,
 * keyboard, disks and timer (pc.c), and the host services they use
 * (main.c).
 */
#ifndef I86_PC_H
#define I86_PC_H

#include "i86.h"

struct pc_config {
	const char *disk[4];	/* A:, B:, first and second hard disk */
	const char *keys;	/* keys typed when the guest waits, or null */
	unsigned ram_kb;
	unsigned ems_kb;	/* expanded memory, 0 for none (ems.h) */
	unsigned cols;		/* 40 or 80 */
	int model;
	int fixed_clock;	/* virtual time: a poll is PC_POLL_MS long */
	unsigned khz;		/* -S: the clock to keep to, 0 for none */
	unsigned cpi10;		/* -S: clocks an instruction takes, tenths */
	int quiet;		/* draw nothing; pc_dump() shows the screen */
	int panel;		/* the PicoCalc panel is the display (gfx.h) */
	int readonly;
};

#define PC_POLL_MS	5
#define PC_STEPS	2048	/* instructions between polls of the host */

extern int pc_idle;		/* the guest waits; leave the step loop */
extern unsigned pc_step;	/* instructions run since the last poll */
extern unsigned pc_slice;	/* instructions to run before the next */
extern unsigned long pc_ran;	/* instructions run in all */

void pc_init(struct i86 *c, const struct pc_config *cf);
void pc_poll(struct i86 *c);
void pc_dump(void);
void pc_restore(int draw);
unsigned long pc_target(void);	/* -S's instructions a second, or 0 */

/* main.c */
int host_ready(int ms);		/* 1 input waits, 0 none within ms, -1 end */
int host_getc(void);
void host_write(const char *p, unsigned n);
unsigned long host_ms(void);
unsigned long host_us(void);
void host_sleep(unsigned ms);
void host_exit(int code);

#endif
