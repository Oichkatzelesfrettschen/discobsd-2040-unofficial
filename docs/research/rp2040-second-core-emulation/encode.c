/*
 * Host packer: reads the file named by argv[1], writes the heatshrink stream
 * to argv[2]. Built against the vendored encoder with the window and
 * lookahead the packed-executable decoder uses, so the stream is the one
 * hsx_expand() decodes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heatshrink_encoder.h"

static unsigned char in[1 << 20], out[1 << 20];

int
main(int argc, char **argv)
{
	heatshrink_encoder e;
	size_t n, sunk = 0, o = 0, m;
	FILE *f, *g;

	if (argc != 3)
		return 2;
	f = fopen(argv[1], "rb");
	if (f == NULL)
		return 1;
	n = fread(in, 1, sizeof in, f);
	fclose(f);
	heatshrink_encoder_reset(&e);
	while (sunk < n) {
		size_t k;
		HSE_poll_res r;

		heatshrink_encoder_sink(&e, in + sunk, n - sunk, &k);
		sunk += k;
		do {
			r = heatshrink_encoder_poll(&e, out + o, sizeof out - o, &m);
			o += m;
		} while (r == HSER_POLL_MORE);
	}
	while (heatshrink_encoder_finish(&e) == HSER_FINISH_MORE) {
		HSE_poll_res r;

		do {
			r = heatshrink_encoder_poll(&e, out + o, sizeof out - o, &m);
			o += m;
		} while (r == HSER_POLL_MORE);
	}
	g = fopen(argv[2], "wb");
	if (g == NULL)
		return 1;
	fwrite(out, 1, o, g);
	fclose(g);
	fprintf(stderr, "raw %zu packed %zu ratio %.3f\n", n, o, (double)o / (double)n);
	return 0;
}
