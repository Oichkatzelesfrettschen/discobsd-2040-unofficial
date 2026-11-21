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

typedef unsigned short u16; typedef unsigned char u8; typedef unsigned u32;
/* a: index -> RGB565 into a bounce buffer, palette stored byte-swapped so the buffer is wire order */
void expand_a(const u8 *p, unsigned n, const u16 *pal, u16 *out) { while (n--) *out++ = pal[*p++ & 15]; }
/* b: pair table: one byte holds two 4-bit indexes; 256 entries of two pixels */
void expand_b(const u8 *p, unsigned n, const u32 *pair, u32 *out) { while (n--) *out++ = pair[*p++]; }
/* c: current style: palette lookup then two polled FIFO writes per pixel */
void expand_c(const u8 *p, unsigned n, const u16 *pal, volatile u32 *sr, volatile u32 *dr) {
    while (n--) { u32 c = pal[*p++ & 15];
        while (!(*sr & 2))
            continue;
        *dr = c >> 8;
        while (!(*sr & 2))
            continue;
        *dr = c & 255;
    }
}
