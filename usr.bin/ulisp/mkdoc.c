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
 * Writes the uLisp documentation file from builtins.def, in the format
 * sys_docopen() (unix.c) reads. It runs on the build host.
 */
#include <stdio.h>
#include <string.h>

static const char *const doc[] = {
#define N(e, name, fn, minmax, text) text,
#define B(name, fn, minmax, text) text,
#define K(name, value, context) NULL,
#include "builtins.def"
};

#define COUNT	(sizeof doc / sizeof doc[0])

static void
put16(FILE *f, size_t v)
{
	putc((int)(v & 0xff), f);
	putc((int)(v >> 8 & 0xff), f);
}

int
main(int argc, char **argv)
{
	FILE *f;
	size_t offset = 0;

	if (argc != 2 || (f = fopen(argv[1], "wb")) == NULL) {
		fprintf(stderr, "usage: mkdoc file\n");
		return 1;
	}
	put16(f, COUNT);
	for (size_t i = 0; i < COUNT; i++) {
		put16(f, offset);
		if (doc[i] != NULL)
			offset += strlen(doc[i]);
	}
	put16(f, offset);
	if (offset > 0xffff) {
		fprintf(stderr, "mkdoc: the text exceeds 65535 bytes\n");
		return 1;
	}
	for (size_t i = 0; i < COUNT; i++)
		if (doc[i] != NULL)
			fputs(doc[i], f);
	return fclose(f) == 0 ? 0 : 1;
}
