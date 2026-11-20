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

#include <stdlib.h>
#include "device.h"
#include "genmem.h"
#include "genfont.h"
#include "fb.h"
extern void panel_flush(unsigned char *p, int pitch, int x, int y, int w, int h);
static PSD stub_open(PSD psd)
{
	if (!gen_initpsd(psd, MWPF_PALETTE, 320, 320, PSF_SCREEN | PSF_ADDRMALLOC))
		return NULL;
	psd->palsize = 16;
	return psd;
}
static void stub_close(PSD psd) { free(psd->addr); }
static void stub_setpalette(PSD psd, int first, int count, MWPALENTRY *pal) { }
static void stub_update(PSD psd, MWCOORD x, MWCOORD y, MWCOORD w, MWCOORD h)
{
	panel_flush(psd->addr, psd->pitch, x, y, w, h);
}
SCREENDEVICE scrdev = {
	0, 0, 0, 0, 0, 0, 0, NULL, 0, NULL, 0, 0, 0, 0, 0, 0,
	gen_fonts, stub_open, stub_close, stub_setpalette, gen_getscreeninfo,
	gen_allocatememgc, gen_mapmemgc, gen_freememgc, NULL, stub_update, NULL
};
