/*
 * A DiscoBSD user program for aoutrun: arguments, stdio, the heap, integer
 * division through lib/libc/arm/gen/aeabi_div.S, single and double
 * arithmetic through the boot ROM float glue, and an exit status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	volatile float a = 1.5f, b = 2.25f;
	volatile double d = 3.0;
	volatile long long big = -5000000000LL;
	volatile int num = 1000000, den = 7;
	char *heap;
	int i;

	printf("argc=%d\n", argc);
	for (i = 0; i < argc; i++)
		printf("argv[%d]=%s\n", i, argv[i]);
	printf("div %d %d %d\n", num / den, -num / den, num % den);
	printf("float %d %d\n", (int)(a * b * 100), (int)(d / 7.0 * 1000));
	printf("int64 %d\n", (int)((double)big / 3.0 / 1000000.0));
	heap = malloc(30000);
	if (heap == NULL)
		return 2;
	memset(heap, 7, 30000);
	printf("heap %d\n", heap[29999]);
	return 3;
}
