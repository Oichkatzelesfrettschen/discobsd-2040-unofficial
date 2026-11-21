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

#include <stdio.h>
#include <stdlib.h>
#include "win.h"
static long painted, nrect;
static int rect_cb(void *self, int x, int y, int w, int h, const unsigned char *px) { (void)self; (void)x; (void)y; (void)px; painted += (long)w * h; nrect++; return 0; }
static int pal_cb(void *self, int i, int r, int g, int b) { (void)self; (void)i; (void)r; (void)g; (void)b; return 0; }
int main(void) {
  static struct win_scene s; struct win_backend be; struct win *w; int k; int dd[][2] = {{5,0},{0,5},{5,5},{2,1},{16,16},{50,50}};
  be.rect = rect_cb; be.palette = pal_cb; be.self = 0;
  for (k = 0; k < 6; k++) {
    win_scene_init(&s, &be); win_flush(&s);
    w = win_create(&s, 10, 10, 100, 100, "t", NULL, NULL); win_flush(&s);
    painted = 0; nrect = 0;
    win_move(&s, w, 10 + dd[k][0], 10 + dd[k][1]); win_flush(&s);
    printf("win_move by (%d,%d) of a 100x100 window: painted %ld px in %ld backend rects (old+new = 20000)\n", dd[k][0], dd[k][1], painted, nrect);
  }
  return 0;
}
