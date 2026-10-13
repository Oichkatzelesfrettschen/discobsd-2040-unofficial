/*
 * A descriptor-owned byte aliases the input and pushback of separate FILEs.
 * Keep every legal descriptor in bounds so the ownership oracle, rather
 * than an unrelated memory fault, rejects the mutation.
 */
#undef _filbuf
#define _filbuf ansi_refill
#include "../../lib/libc/stdio/filbuf.c"
#undef _filbuf
#include <sys/syslimits.h>

int
db_filbuf(FILE *stream)
{
	static char descriptor_bytes[OPEN_MAX];

	if (stream->_base == NULL && (stream->_flag & _IONBF) != 0 &&
	    (stream->_flag & _IOSTRG) == 0)
		stream->_base = &descriptor_bytes[fileno(stream)];
	return (ansi_refill(stream));
}
