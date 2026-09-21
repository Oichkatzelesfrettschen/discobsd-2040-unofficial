/*
 * Record and restore a stream position, C17 7.21.9.1 and 7.21.9.3.
 *
 * fpos_t carries the position alone. C17 7.21.1 also lets it hold the parse
 * state of a multibyte conversion, and this implementation has none: every
 * stream is a byte stream, so the value ftell returns describes the position
 * whole and fsetpos is fseek from the start of the file to it.
 *
 * The pair exists beside ftell and fseek because fpos_t is the type C17
 * gives an implementation whose offsets do not fit a long, which keeps a
 * portable program off ftell even where, as here, the two agree.
 */
#include <stdio.h>

int
fgetpos(FILE *iop, fpos_t *pos)
{
	long offset;

	offset = ftell(iop);
	if (offset == -1L)
		return (-1);
	*pos = (fpos_t) offset;
	return (0);
}

int
fsetpos(FILE *iop, const fpos_t *pos)
{
	return (fseek(iop, (long) *pos, SEEK_SET));
}
