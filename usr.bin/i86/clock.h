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
 * The steps from one reading of the host's clock to the next, apart from
 * the clock itself so that tests/clocktest.c can drive them with chosen
 * readings: a host clock set back, one that jumps ahead, one that wraps.
 * pc.c calls them with host_us() and host_ms().
 */
#ifndef I86_CLOCK_H
#define I86_CLOCK_H

#include <stdint.h>

#define CLK_PIT_HZ	1193182u	/* the 8254's input clock */
#define CLK_GAP_US	1000000UL	/* the longest gap one reading counts */
#define CLK_CREDIT_US	20000L		/* -S: time a slow guest may make up */
#define CLK_BACK_US	100000L		/* -S: lead past which the host went back */

uint32_t	clk_pit(unsigned long *, unsigned long, uint32_t *);
unsigned long	clk_forward(unsigned long *, unsigned long);
unsigned	clk_pace(unsigned long *, unsigned long);

#endif
