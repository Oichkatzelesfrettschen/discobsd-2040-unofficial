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
 * The PicoCalc panel as the machine's display (gfx.c): the text modes in
 * cells of 8 or 4 by 8 pixels, and the graphics modes 4, 5 and 6 of the
 * color adapter. pc.c calls these in place of its terminal output while
 * gfx_on is set.
 */
#ifndef I86_GFX_H
#define I86_GFX_H

#include "i86.h"

#define GFX_VRAM	0xb8000u
#define GFX_NOCURSOR	0xffffu

extern int gfx_on;		/* the panel device is open */
extern int gfx_graphic;		/* display memory holds pixels, not cells */

/* vmem is 8 KB: the cells of the text modes in its first half, pc.c's
 * copy of the cells last drawn in its second, and in a graphics mode the
 * even scan lines in the whole of it. */
void gfx_init(uint8_t *vmem);
/* Take the panel and give it back. gfx_open() returns -1 when the device
 * is absent or held; display memory is then that of a text mode. */
int gfx_open(void);
void gfx_close(void);
/* The mode of interrupt 10h function 0. Returns 1 when it is a graphics
 * mode and display memory holds it. */
int gfx_mode(unsigned mode);
/* Draw what changed: the cells of a text mode with cols columns and the
 * cursor at cell index cursor, or the scan lines of a graphics mode. */
void gfx_refresh(unsigned cols, unsigned cursor);
/* Show or remove the mark of mouse keys in the panel's top margin. */
void gfx_pointer(int on);
/* The firmware's text and pixel services in a graphics mode. */
void gfx_putc(unsigned row, unsigned col, unsigned ch, unsigned color);
void gfx_scroll(unsigned top, unsigned left, unsigned bot, unsigned right,
    unsigned n, unsigned fill, int down);
int gfx_video(struct i86 *c);
/* The adapter's mode (3D8h) and color (3D9h) registers. */
void gfx_out(unsigned port, unsigned v);
/* The 8x8 cells of the 256 characters, eight bytes each. */
const uint8_t *gfx_font(void);

#endif
