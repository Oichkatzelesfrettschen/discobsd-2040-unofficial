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

#include <stddef.h>
#include "device.h"
#include "genmem.h"
extern SCREENDEVICE scrdev;
int app(const char *s, int n)
{
	PSD psd = GdOpenScreen();
	MWCLIPREGION *r, *r2;
	PMWFONT f;
	PSD pm;
	static MWIMAGEBITS bits[16];
	if (!psd) return 1;
	GdSetForegroundColor(psd, 3);
	GdSetBackgroundColor(psd, 0);
	GdSetMode(MWROP_COPY);
	GdFillRect(psd, 10, 10, 100, 60);
	GdLine(psd, 0, 0, 300, 200, 1);
	GdRect(psd, 5, 5, 50, 50);
	GdBitmap(psd, 20, 20, 16, 16, bits);
	f = GdCreateFont(psd, "SystemFixed", 0, 0, NULL);
	GdText(psd, f, 10, 10, s, n, MWTF_UTF8 | MWTF_TOP);
	r = GdAllocRectRegion(0, 0, 320, 320);
	r2 = GdAllocRectRegion(10, 10, 50, 50);
	GdSubtractRegion(r, r, r2);
	GdSetClipRegion(psd, r);
	GdUnionRectWithRegion(&r2->rects[0], r);
	pm = GdCreatePixmap(psd, 100, 60, MWIF_PAL4, NULL, 0);
	GdBlit(psd, 50, 50, 100, 60, pm, 0, 0, MWROP_COPY);
	GdFreePixmap(pm);
	GdCloseScreen(psd);
	return 0;
}
