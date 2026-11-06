/*
 * A DiscoBSD user program for aoutrun: arguments, stdio, the heap, integer
 * division through lib/libc/arm/gen/aeabi_div.S, single and double
 * arithmetic through the boot ROM float glue, and an exit status.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Ten bytes, "0123456789", written by the Makefile before the run. */
#define SEEK_FIXTURE "seek.dat"

static int
seek_probe(void)
{
	char c;
	int fd, ch;
	FILE *f;

	fd = open(SEEK_FIXTURE, O_RDONLY);
	if (fd < 0)
		return -1;
	printf("lseek set %ld\n", (long)lseek(fd, 4, SEEK_SET));
	if (read(fd, &c, 1) == 1)
		printf("read %c\n", c);
	printf("lseek cur %ld\n", (long)lseek(fd, 2, SEEK_CUR));
	if (read(fd, &c, 1) == 1)
		printf("read %c\n", c);
	printf("lseek end %ld\n", (long)lseek(fd, -3, SEEK_END));
	printf("lseek eof %ld\n", (long)lseek(fd, 0, SEEK_END));
	close(fd);

	f = fopen(SEEK_FIXTURE, "r");
	if (f == NULL)
		return -1;
	printf("fseek set %d\n", fseek(f, 3, SEEK_SET));
	ch = fgetc(f);
	printf("fgetc %c ftell %ld\n", ch, ftell(f));
	printf("fseek end %d\n", fseek(f, -2, SEEK_END));
	ch = fgetc(f);
	printf("fgetc %c ftell %ld\n", ch, ftell(f));
	fclose(f);
	return 0;
}

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
	if (seek_probe() != 0)
		return 4;
	heap = malloc(30000);
	if (heap == NULL)
		return 2;
	memset(heap, 7, 30000);
	printf("heap %d\n", heap[29999]);
	return 3;
}
