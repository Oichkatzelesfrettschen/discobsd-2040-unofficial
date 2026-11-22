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

/* Rectangle cover of a damage mask under the wire cost rule.
 * cost(w,h) = 2*w*h + 11*ceil(h / floor(5120/w)).
 * The mask is covered by stripes: a band of rows, inside it clusters of columns
 * (column occupancy of the band, bridged when cheaper), each cluster one window.
 * transpose=1 swaps the roles (bands of columns, clusters of rows) and the cost arguments.
 * rect_cover returns the cost; rect_emit also returns the rectangles (x, y, w, h in the matrix as given). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define INF 0x3fffffffL
static long wcost(long w, long h) {
    long per = 5120 / w; if (per < 1) per = 1;
    return 2*w*h + 11*((h + per - 1) / per);
}
static int runs_of(const int *cs, int H, int W, int a, int b, const uint8_t *iscutc, int *rs, int *re) {
    int x, k = 0, in = 0;
    (void)H;
    for (x = 0; x < W; x++) {
        int occ = (cs[(size_t)b * W + x] - cs[(size_t)a * W + x]) > 0;
        if (occ && in && iscutc[x]) { re[k - 1] = x - 1; in = 0; }
        if (occ && !in) { rs[k] = x; in = 1; k++; }
        else if (!occ && in) { re[k - 1] = x - 1; in = 0; }
    }
    if (in) re[k - 1] = W - 1;
    return k;
}
static void cluster_dp(const int *rs, const int *re, int k, long h, int transpose, const uint8_t *iscutc,
                       long *g, int *gc, int *gp) {
    int i, j, c;
    g[0] = 0; gc[0] = 0; gp[0] = 0;
    for (j = 1; j <= k; j++) {
        int lim = j - 1 - 80 < 0 ? 0 : j - 1 - 80;
        g[j] = INF; gc[j] = 0; gp[j] = j - 1;
        for (i = j; i >= 1 && i - 1 >= lim; i--) {
            int xs = rs[i - 1], xe = re[j - 1], bad = 0;
            for (c = xs + 1; c <= xe; c++) if (iscutc[c]) { bad = 1; break; }
            if (bad) break;
            long wd = xe - xs + 1;
            long cc = transpose ? wcost(h, wd) : wcost(wd, h);
            if (g[i - 1] + cc < g[j]) { g[j] = g[i - 1] + cc; gc[j] = gc[i - 1] + 1; gp[j] = i - 1; }
        }
    }
}
static long cover_impl(const uint8_t *mask, int H, int W, int transpose,
                const int *rowcut, int nrowcut, const int *colcut, int ncolcut, int *nrects,
                int *out, int maxout)
{
    int y, x, a, b, i;
    int *cs = malloc(sizeof(int) * (size_t)(H + 1) * W);
    long *best = malloc(sizeof(long) * (H + 1));
    int *cnt = malloc(sizeof(int) * (H + 1)), *from = malloc(sizeof(int) * (H + 1));
    uint8_t *rowhas = calloc(H, 1);
    uint8_t *iscut = calloc(H + 1, 1), *iscutc = calloc(W + 1, 1);
    int *rs = malloc(sizeof(int) * (W + 1)), *re = malloc(sizeof(int) * (W + 1));
    long *g = malloc(sizeof(long) * (W + 2)); int *gc = malloc(sizeof(int) * (W + 2)), *gp = malloc(sizeof(int) * (W + 2));
    for (i = 0; i < nrowcut; i++) if (rowcut[i] > 0 && rowcut[i] < H) iscut[rowcut[i]] = 1;
    for (i = 0; i < ncolcut; i++) if (colcut[i] > 0 && colcut[i] < W) iscutc[colcut[i]] = 1;
    for (x = 0; x < W; x++) cs[x] = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint8_t m = mask[(size_t)y * W + x] ? 1 : 0;
            cs[(size_t)(y + 1) * W + x] = cs[(size_t)y * W + x] + m;
            if (m) rowhas[y] = 1;
        }
    best[0] = 0; cnt[0] = 0; from[0] = -1;
    for (b = 1; b <= H; b++) {
        best[b] = INF; cnt[b] = 0; from[b] = -1;
        if (!rowhas[b - 1]) { best[b] = best[b - 1]; cnt[b] = cnt[b - 1]; from[b] = b - 1; continue; }
        int lo = 0;
        for (a = b - 1; a >= 1; a--) if (iscut[a]) { lo = a; break; }
        for (a = b - 1; a >= lo; a--) {
            if (!rowhas[a] || best[a] >= INF) continue;
            long h = b - a;
            int k = runs_of(cs, H, W, a, b, iscutc, rs, re);
            cluster_dp(rs, re, k, h, transpose, iscutc, g, gc, gp);
            long tot = best[a] + g[k];
            if (g[k] < INF && tot < best[b]) { best[b] = tot; cnt[b] = cnt[a] + gc[k]; from[b] = a; }
        }
    }
    long r = best[H]; if (nrects) *nrects = cnt[H];
    if (out) {
        int n = 0;
        for (b = H; b > 0; ) {
            a = from[b];
            if (a == b - 1 && !rowhas[b - 1]) { b = a; continue; }
            if (a < 0) break;
            long h = b - a;
            int k = runs_of(cs, H, W, a, b, iscutc, rs, re), j;
            cluster_dp(rs, re, k, h, transpose, iscutc, g, gc, gp);
            for (j = k; j > 0; j = gp[j]) {
                int s0 = gp[j];
                if (n < maxout) {
                    out[4 * n + 0] = rs[s0]; out[4 * n + 1] = a; out[4 * n + 2] = re[j - 1] - rs[s0] + 1; out[4 * n + 3] = (int)h; n++;
                }
            }
            b = a;
        }
        if (nrects) *nrects = n;
    }
    free(cs); free(best); free(cnt); free(from); free(rowhas); free(iscut); free(iscutc); free(rs); free(re); free(g); free(gc); free(gp);
    return r;
}
long rect_cover(const uint8_t *mask, int H, int W, int transpose, const int *rowcut, int nrowcut,
                const int *colcut, int ncolcut, int *nrects) {
    return cover_impl(mask, H, W, transpose, rowcut, nrowcut, colcut, ncolcut, nrects, NULL, 0);
}
long rect_emit(const uint8_t *mask, int H, int W, int transpose, const int *rowcut, int nrowcut,
               const int *colcut, int ncolcut, int *nrects, int *out, int maxout) {
    return cover_impl(mask, H, W, transpose, rowcut, nrowcut, colcut, ncolcut, nrects, out, maxout);
}
