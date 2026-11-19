/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The system interface of the uLisp interpreter (ulisp.c): console, files,
 * clock, pins, sound and the pixel display. unix.c implements the POSIX
 * part, gfx.c draws shapes and text through dev_fill(), and one of
 * dev_picocalc.c (the PicoCalc devices of DiscoBSD) or dev_host.c (a
 * frame buffer in memory, for a development host) supplies the dev_ part.
 */
#ifndef ULISP_PLATFORM_H
#define ULISP_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The three files open at once: with-sd-card input, its output, and one
 * for an image, a bitmap, the library or the documentation. */
enum { SYS_FIN, SYS_FOUT, SYS_FTMP, SYS_NFILES };
enum { SYS_READ, SYS_APPEND, SYS_WRITE };
enum { SYS_INPUT, SYS_OUTPUT, SYS_INPUT_PULLUP, SYS_INPUT_PULLDOWN };

/*
 * sys_init() allocates the workspace: *cells objects of cellsize bytes, or
 * as many as fit when *cells is zero, and stores the count. *stacklimit
 * receives the lowest address the C stack may reach.
 */
void *sys_init(unsigned int *cells, size_t cellsize, uintptr_t *stacklimit);
void sys_exit(int status) __attribute__((noreturn));

int sys_getc(void);		/* -1 at the end of input */
int sys_getkey(void);		/* one key, unbuffered and unechoed */
void sys_putc(char c);
void sys_flush(void);
bool sys_interactive(void);
bool sys_break(void);		/* true once for each interrupt received */

unsigned long sys_millis(void);
unsigned long sys_micros(void);
void sys_delay(unsigned long ms);
uint32_t sys_random(void);

bool sys_fopen(int file, const char *path, int mode);
bool sys_fclose(int file);	/* false when buffered output was lost */
int sys_fgetc(int file);
void sys_fputc(int file, char c);
bool sys_fread(int file, void *data, size_t bytes);
bool sys_fwrite(int file, const void *data, size_t bytes);
bool sys_diropen(const char *path);
const char *sys_dirnext(void);
const char *sys_library(void);
/* Positions SYS_FTMP at the documentation of a builtin; returns its length. */
int sys_docopen(unsigned int index);

/* ulisp.c: raises a Lisp error from the system side; does not return. */
void ulisp_fail(const char *message) __attribute__((noreturn));

/* Devices. */
void dev_pinmode(int pin, int mode);
int dev_digitalread(int pin);
void dev_digitalwrite(int pin, int level);
/* The raw 12-bit count of the analogue input on pin 26-29; fails by ulisp_fail(). */
int dev_analogread(int pin);
/* Holds pin at duty, 0-255, of a fixed-frequency square wave; fails by ulisp_fail(). */
void dev_analogwrite(int pin, int duty);
/* Plays ms of a square wave of hz; false when there is no sound device. */
bool dev_tone(unsigned int hz, unsigned long ms);
bool dev_callcode(const uint8_t *entry, const int param[4], int *result);
/* Fills the rectangle x0..x1, y0..y1, which lies on the panel. */
void dev_fill(int x0, int y0, int x1, int y1, uint16_t color);
int dev_readpixel(int x, int y);
void dev_release(void);		/* returns the panel to the text console */

#define sys_pinmode		dev_pinmode
#define sys_digitalread		dev_digitalread
#define sys_digitalwrite	dev_digitalwrite
#define sys_analogread		dev_analogread
#define sys_analogwrite		dev_analogwrite
#define sys_callcode		dev_callcode
void sys_tone(unsigned int hz);	/* sounds during sys_delay(); 0 is silence */

/* Graphics (gfx.c). Colors are RGB565. */
#define GFX_WIDTH	320
#define GFX_HEIGHT	320
#define GFX_TRANSPARENT	(-1)	/* text background left as it is */

void gfx_pixel(int x, int y, int color);
void gfx_line(int x0, int y0, int x1, int y1, int color);
void gfx_rect(int x, int y, int w, int h, int color);
void gfx_fillrect(int x, int y, int w, int h, int color);
void gfx_circle(int x, int y, int r, int color);
void gfx_fillcircle(int x, int y, int r, int color);
void gfx_roundrect(int x, int y, int w, int h, int r, int color);
void gfx_fillroundrect(int x, int y, int w, int h, int r, int color);
void gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color);
void gfx_filltriangle(int x0, int y0, int x1, int y1, int x2, int y2,
    int color);
void gfx_char(int x, int y, int c, int color, int bg, int size);
void gfx_write(char c);
void gfx_setcursor(int x, int y);
void gfx_settextcolor(int color, int bg);
void gfx_settextsize(int size);
void gfx_settextwrap(bool wrap);
void gfx_fillscreen(int color);
int gfx_readpixel(int x, int y);

#endif /* !ULISP_PLATFORM_H */
