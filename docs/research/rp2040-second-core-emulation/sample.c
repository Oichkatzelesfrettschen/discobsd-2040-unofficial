/*
 * Stand-in workload for the cycle model: a small static newlib program whose
 * flat image (text, rodata and data, 46,540 bytes) the decoder expands. It
 * is not a DiscoBSD a.out; it shares only the instruction mix of Thumb-1
 * text.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	char b[64];

	sprintf(b, "%d %s", argc, argv[0]);
	puts(b);
	return strlen(b) + atoi(b);
}
