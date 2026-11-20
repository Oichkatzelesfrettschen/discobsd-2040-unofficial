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
 * i86: an 8086/8088, 80186 and 80286 real-address-mode interpreter.
 *
 * cpu.c executes instructions against the guest memory of mem.h. The
 * machine around the processor (pc.c) supplies port I/O and the firmware
 * services: the two-byte sequence 0F FF nn, fetched from segment F000,
 * calls pc_trap(nn) instead of decoding as an instruction.
 */
#ifndef I86_H
#define I86_H

#include <stdint.h>

enum { R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI };
enum { S_ES, S_CS, S_SS, S_DS };
enum { I86_8086, I86_186, I86_286 };

#define F_CF	0x0001
#define F_PF	0x0004
#define F_AF	0x0010
#define F_ZF	0x0040
#define F_SF	0x0080
#define F_TF	0x0100
#define F_IF	0x0200
#define F_DF	0x0400
#define F_OF	0x0800

struct i86 {
	uint16_t r[8];		/* AX CX DX BX SP BP SI DI */
	uint16_t s[4];		/* ES CS SS DS */
	uint16_t ip;
	uint16_t fl;
	uint8_t model;		/* I86_8086, I86_186 or I86_286 */
	uint8_t fault;		/* the interrupt last entered was raised by
				 * a failing instruction, not by INT n or
				 * by a request */
	/* The three conditions i86_step() examines before an instruction,
	 * readable as one word that is zero when none holds. */
	union {
		struct {
			uint8_t halted;	 /* HLT executed; an interrupt clears it */
			uint8_t irq;	 /* the interrupt request line */
			uint8_t inhibit; /* interrupts held off for one instruction */
			uint8_t unused;
		};
		uint32_t events;
	};
};

void i86_reset(struct i86 *c, int model);
void i86_setfl(struct i86 *c, unsigned fl);
void i86_int(struct i86 *c, unsigned n);
void i86_step(struct i86 *c);

/* Supplied by the machine. pc_trap returns 0 to return from the interrupt
 * frame on the stack with the arithmetic flags of c->fl, 2 to do the same
 * with interrupts enabled, 1 to continue with the next instruction. */
int pc_trap(struct i86 *c, unsigned n);
/* The acknowledge cycle: the vector of the interrupt the processor takes
 * when the request line is up and interrupts are enabled. The machine
 * lowers c->irq when nothing further is pending. */
unsigned pc_inta(struct i86 *c);
unsigned pc_in(struct i86 *c, unsigned port, int w);
void pc_out(struct i86 *c, unsigned port, unsigned v, int w);

#endif
