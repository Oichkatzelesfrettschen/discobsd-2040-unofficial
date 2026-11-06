/*
 * Entry points for the cycle model (cycles.py). The packed stream sits at
 * 0x30000000 and the expansion target at 0x30200000 in the emulated address
 * space; run() expands into the target and run_discard() into the preflight
 * discard path that exec uses first. The mem* and b* routines are plain
 * byte loops so the model charges the decoder, not a tuned copy.
 */
#include <machine/hsx_decoder.h>
#include <sys/exec_hsaout.h>

#define PK	((const unsigned char *)0x30000000u)
#define OUT	((unsigned char *)0x30200000u)

static struct hsx_work w;

int
fill(void *ctx, unsigned off, void *buf, unsigned len)
{
	unsigned char *d = buf;
	const unsigned char *s = PK + off;

	(void)ctx;
	while (len--)
		*d++ = *s++;
	return 0;
}

int
run(unsigned clen, unsigned rlen, unsigned *crc)
{
	return hsx_expand(&w, fill, 0, 0, clen, OUT, rlen, crc);
}

int
run_discard(unsigned clen, unsigned rlen, unsigned *crc)
{
	return hsx_expand(&w, fill, 0, 0, clen, 0, rlen, crc);
}

void *
memset(void *p, int c, unsigned n)
{
	unsigned char *q = p;

	while (n--)
		*q++ = (unsigned char)c;
	return p;
}

void *
memcpy(void *d, const void *s, unsigned n)
{
	unsigned char *a = d;
	const unsigned char *b = s;

	while (n--)
		*a++ = *b++;
	return d;
}

void *
memmove(void *d, const void *s, unsigned n)
{
	unsigned char *a = d;
	const unsigned char *b = s;

	if (a < b) {
		while (n--)
			*a++ = *b++;
	} else {
		a += n;
		b += n;
		while (n--)
			*--a = *--b;
	}
	return d;
}

void
bzero(void *p, unsigned n)
{
	unsigned char *q = p;

	while (n--)
		*q++ = 0;
}

void
bcopy(const void *s, void *d, unsigned n)
{
	unsigned char *a = d;
	const unsigned char *b = s;

	while (n--)
		*a++ = *b++;
}
