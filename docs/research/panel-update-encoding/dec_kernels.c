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

#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
/* 8 bpp palette index -> RGB565 (the INDEX window format) */
void idx8(const u8 *s, u16 *d, u32 n, const u16 *pal) { while (n--) *d++ = pal[*s++ & 15]; }
/* 1 bpp, 8 pixels a byte, fg/bg (the BITS window format) */
void bits1(const u8 *s, u16 *d, u32 nb, u16 fg, u16 bg) {
    while (nb--) { u32 b = *s++; u32 m = 0x80;
        do { *d++ = (b & m) ? fg : bg; m >>= 1; } while (m); }
}
/* 2 bpp packed indexes, 4 pixels a byte, 4-entry palette */
void idx2(const u8 *s, u16 *d, u32 nb, const u16 *pal) {
    while (nb--) { u32 b = *s++;
        d[0] = pal[b >> 6]; d[1] = pal[(b >> 4) & 3]; d[2] = pal[(b >> 2) & 3]; d[3] = pal[b & 3]; d += 4; }
}
/* 4 bpp packed indexes, 2 pixels a byte, 16-entry palette */
void idx4(const u8 *s, u16 *d, u32 nb, const u16 *pal) {
    while (nb--) { u32 b = *s++; d[0] = pal[b >> 4]; d[1] = pal[b & 15]; d += 2; }
}
/* CGA mode 6: 1 bpp, 8 guest pixels a byte -> 4 panel pixels (pair sums), 3-entry palette, 256x4 table of levels */
void cga6(const u8 *s, u16 *d, u32 nb, const u16 *pal, const u8 *lut) {
    while (nb--) { const u8 *t = lut + 4 * (u32)*s++;
        d[0] = pal[t[0]]; d[1] = pal[t[1]]; d[2] = pal[t[2]]; d[3] = pal[t[3]]; d += 4; }
}
/* 80-column text cell: font4 two rows a byte, 4 px wide, 8 rows, fg/bg -> 8 rows x 4 px into a 4-px-stride window row buffer */
void cell80(const u8 *glyph /* 4 bytes */, u16 *d, u32 stride, u16 fg, u16 bg) {
    u32 y;
    for (y = 0; y < 4; y++) { u32 b = glyph[y]; u32 hi = b >> 4, lo = b & 15;
        d[0] = (hi & 8) ? fg : bg; d[1] = (hi & 4) ? fg : bg; d[2] = (hi & 2) ? fg : bg; d[3] = (hi & 1) ? fg : bg; d += stride;
        d[0] = (lo & 8) ? fg : bg; d[1] = (lo & 4) ? fg : bg; d[2] = (lo & 2) ? fg : bg; d[3] = (lo & 1) ? fg : bg; d += stride; }
}
/* skip/literal token decode: varint skip, varint lit, lit bytes copied into the frame store */
u32 tokdec(const u8 *s, u32 n, u8 *frame) {
    const u8 *e = s + n; u8 *f = frame;
    while (s < e) {
        u32 v = *s++; u32 sk = v & 0x7f; if (v & 0x80) { v = *s++; sk |= v << 7; }
        v = *s++; u32 ln = v & 0x7f; if (v & 0x80) { v = *s++; ln |= v << 7; }
        f += sk; while (ln--) *f++ = *s++;
    }
    return (u32)(f - frame);
}
/* XOR variant of the literal copy (delta against the frame store) */
void xorlit(const u8 *s, u8 *f, u32 n) { while (n--) *f++ ^= *s++; }
/* word copy for row moves (memmove of aligned rows) */
void rowmove(const u32 *s, u32 *d, u32 nw) { while (nw--) *d++ = *s++; }
/* per-line predictor reconstruction: up, temporal, left */
void pred_up(const u8 *res, const u8 *above, u8 *out, u32 n) { u32 i; for (i = 0; i < n; i++) out[i] = res[i] ^ above[i]; }
void pred_left(const u8 *res, u8 *out, u32 n) { u32 i; u32 p = 0; for (i = 0; i < n; i++) { p ^= res[i]; out[i] = (u8)p; } }
