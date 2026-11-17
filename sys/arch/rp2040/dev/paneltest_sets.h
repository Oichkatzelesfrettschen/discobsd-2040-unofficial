/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PANELTEST_SETS_H
#define _PANELTEST_SETS_H

/*
 * The ST7365P register sets of the panel test (doc/PANELTEST.md). Each set
 * is a hardware reset followed by PRE, then BASE, then POST, then SLPOUT,
 * the 320-line scroll definition and DISPON, with COLMOD 66h sent last so
 * /dev/lcdgfx keeps its pixel format. BASE is DRIVER (st7365p.c init_seq)
 * or VENDOR (the PicoMite ST7796SP sequence of the vendor tree). The
 * sequence names (UNLOCK, GAMMA_DEFAULT, ...) are byte tables in
 * st7365p.c; NONE is empty. Values come from ST7365P_SPEC_V1.0: CSCON F0h
 * C3h/96h (page 245), PGC/NGC defaults (pages 240-241), PWR3 C2h default
 * A5h (page 233), VCMPCTL C5h (page 235), DIC B4h (page 223), FRMCTR1 B1h
 * (page 220), BPC B5h (page 222), MADCTL 36h (page 188). Sets are appended,
 * so a set keeps its number across revisions. No set writes an NVM command (D0h,
 * D1h) or a value outside the datasheet's documented range.
 *
 * X(name, pre, base, post); the index in this list is the set number.
 */
#define	PANELTEST_SETS(X) \
	X("shipped",			NONE,	DRIVER,	NONE) \
	X("vendor-st7796sp",		NONE,	VENDOR,	NONE) \
	X("unlock",			UNLOCK,	DRIVER,	NONE) \
	X("unlock gamma-default",	UNLOCK,	DRIVER,	GAMMA_DEFAULT) \
	X("unlock gamma-default v63=0",	UNLOCK,	DRIVER,	GAMMA_V63_ZERO) \
	X("unlock gamma-shift",		UNLOCK,	DRIVER,	GAMMA_SHIFT) \
	X("unlock inversion column",	UNLOCK,	DRIVER,	INV_COLUMN) \
	X("unlock inversion 1-dot",	UNLOCK,	DRIVER,	INV_DOT1) \
	X("unlock source AA",		UNLOCK,	DRIVER,	PWR3_AA) \
	X("unlock source AF",		UNLOCK,	DRIVER,	PWR3_AF) \
	X("unlock vcom 12",		UNLOCK,	DRIVER,	VCOM_12) \
	X("unlock vcom 1c",		UNLOCK,	DRIVER,	VCOM_1C) \
	X("unlock vcom 30",		UNLOCK,	DRIVER,	VCOM_30) \
	X("unlock vcom 3f",		UNLOCK,	DRIVER,	VCOM_3F) \
	X("probe 31hz locked",		NONE,	DRIVER,	FRAME_31HZ) \
	X("probe 31hz unlocked",	UNLOCK,	DRIVER,	FRAME_31HZ) \
	X("probe vcom 3f locked",	NONE,	DRIVER,	VCOM_3F) \
	X("probe gamma zero locked",	NONE,	DRIVER,	GAMMA_ZERO) \
	X("probe gamma zero unlocked",	UNLOCK,	DRIVER,	GAMMA_ZERO) \
	X("madctl 40 rgb bit clear",	NONE,	DRIVER,	MADCTL_40) \
	X("madctl 08 mx clear",		NONE,	DRIVER,	MADCTL_08) \
	X("frame 24hz diva2 unlocked",	UNLOCK,	DRIVER,	FRAME_24HZ) \
	X("frame 40hz unlocked",	UNLOCK,	DRIVER,	FRAME_40HZ) \
	X("frame 60hz unlocked",	UNLOCK,	DRIVER,	FRAME_60HZ) \
	X("frame 75hz unlocked",	UNLOCK,	DRIVER,	FRAME_75HZ) \
	X("frame 90hz unlocked",	UNLOCK,	DRIVER,	FRAME_90HZ) \
	X("frame 120hz unlocked",	UNLOCK,	DRIVER,	FRAME_120HZ) \
	X("ct2 mark unlocked",		UNLOCK,	DRIVER,	CT2_MARK)

#define	PANELTEST_COUNT_ONE(name, pre, base, post)	+ 1
#define	PANELTEST_NSETS	(0 PANELTEST_SETS(PANELTEST_COUNT_ONE))

/*
 * A write of machdep.picocalc.panel_variant carries this token in its high
 * 16 bits and the set number in its low 16 bits; any other value is
 * refused, so a stray sysctl write cannot reprogram the panel.
 */
#define	PANELTEST_ARM		0x5054		/* "PT" */
#define	PANELTEST_VALUE(n)	((int)(PANELTEST_ARM << 16 | (n)))

#endif /* !_PANELTEST_SETS_H */
