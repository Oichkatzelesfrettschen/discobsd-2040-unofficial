/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>

/*
 * Choose one of the three kinds of buffering for a stream, with a buffer the
 * caller supplies or one allocated here.
 *
 * C17 7.21.5.6p2 allows the call only on a stream that has been opened and
 * not yet used, so the flush and the discard below cover a caller that
 * reached here anyway rather than a supported sequence. C17 7.21.7.10p2
 * discards pushback at a successful positioning call, and replacing the
 * buffer is the stronger act, so the parked byte goes with the buffer it
 * was parked beside.
 *
 * Trimmed from the 4.4BSD version: the flag names are the ones this stream
 * core uses, and BUFSIZ stands in for 4.4BSD's optimum-size selection, which
 * asks fstat for a block size this target's buffers do not follow.
 */
int
setvbuf(FILE *fp, char *buf, int mode, size_t size)
{
	int flags, ret;

	/*
	 * _bufsiz records the size as an int, so a request past INT_MAX
	 * cannot be held; C17 7.21.5.6p3 answers an argument the stream
	 * cannot honor with a nonzero return rather than a short buffer.
	 */
	if (mode != _IONBF &&
	    ((mode != _IOFBF && mode != _IOLBF) || size > (size_t) INT_MAX))
		return (EOF);

	(void) fflush(fp);
	flags = fp->_flag;
	if (flags & _IOMYBUF)
		free(fp->_base);
	flags &= ~(_IOLBF | _IONBF | _IOMYBUF | _IOUNGET);
	fp->_cnt = 0;
	fp->_bufsiz = 0;
	ret = 0;

	if (mode != _IONBF && buf == NULL) {
		if (size == 0)
			size = BUFSIZ;
		buf = malloc(size);
		if (buf == NULL) {
			/*
			 * The size asked for is refused whatever follows, so
			 * the return is already EOF; a second try at BUFSIZ
			 * leaves the stream buffered rather than unbuffered.
			 */
			ret = EOF;
			if (size != BUFSIZ) {
				size = BUFSIZ;
				buf = malloc(size);
			}
		}
		if (buf != NULL)
			flags |= _IOMYBUF;
	}

	/*
	 * An unbuffered stream keeps no base, so _filbuf reads one byte into
	 * its _smallbuf slot and no later call frees a buffer this one owns.
	 * A buffer that could not be allocated arrives here the same way.
	 */
	if (mode == _IONBF || buf == NULL) {
		fp->_flag = (short) (flags | _IONBF);
		fp->_base = fp->_ptr = NULL;
		return (ret);
	}

	/*
	 * An r+ stream returns to the state where neither mode is chosen, so
	 * the first read reaches _filbuf for its initial fill and the first
	 * write reaches _flsbuf with an empty buffer to fill.
	 */
	if (mode == _IOLBF)
		flags |= _IOLBF;
	if (flags & _IORW)
		flags &= ~(_IOREAD | _IOWRT);
	fp->_flag = (short) flags;
	fp->_base = fp->_ptr = buf;
	fp->_bufsiz = (int) size;
	return (ret);
}
