/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <strings.h>

/* The fixed POSIX byte locale folds ASCII letters and preserves other bytes. */
static unsigned char
fold_ascii(unsigned char byte)
{
	if (byte >= 'A' && byte <= 'Z')
		byte += 'a' - 'A';
	return byte;
}

int
strcasecmp(const char *left, const char *right)
{
	unsigned char left_byte;
	unsigned char right_byte;

	for (;;) {
		left_byte = fold_ascii((unsigned char)*left++);
		right_byte = fold_ascii((unsigned char)*right++);
		if (left_byte != right_byte || left_byte == '\0')
			return (int)left_byte - (int)right_byte;
	}
}

int
strncasecmp(const char *left, const char *right, size_t length)
{
	unsigned char left_byte;
	unsigned char right_byte;

	while (length != 0) {
		--length;
		left_byte = fold_ascii((unsigned char)*left++);
		right_byte = fold_ascii((unsigned char)*right++);
		if (left_byte != right_byte || left_byte == '\0')
			return (int)left_byte - (int)right_byte;
	}
	return 0;
}
