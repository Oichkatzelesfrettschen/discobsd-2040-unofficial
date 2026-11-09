/*
 * A DiscoBSD user program for aoutrun: arguments, stdio, the heap, integer
 * division through lib/libc/arm/gen/aeabi_div.S, single and double
 * arithmetic through the boot ROM float glue, and an exit status.
 */
#include <errno.h>
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

/*
 * Fixtures written by the Makefile: LOOP_FIXTURE is a symbolic link to itself.
 * The DiscoBSD errno values come from sys/sys/errno.h: ENOENT 2, ENOMEM 12,
 * ELOOP 62, ENAMETOOLONG 63. A Linux number leaking through shows as 36 for
 * the long name and 40 for the loop.
 */
#define LOOP_FIXTURE "loop.lnk"

static int
errno_probe(void)
{
	static char longname[300];
	int fd;

	memset(longname, 'n', sizeof longname - 1u);
	errno = 0;
	fd = open(longname, O_RDONLY);
	printf("open long %d errno %d\n", fd, errno);
	errno = 0;
	fd = open(LOOP_FIXTURE, O_RDONLY);
	printf("open loop %d errno %d\n", fd, errno);
	errno = 0;
	fd = open("no-such-file", O_RDONLY);
	printf("open absent %d errno %d\n", fd, errno);
	return 6;
}

/*
 * brk() requests that lie outside the process window or leave the stack no
 * room must fail with ENOMEM; 0xfffffc00 is the request whose sum with the
 * 1024-byte stack reserve wraps to zero.
 */
static int
brk_probe(void)
{
	static const unsigned long wild[] = {
		0xfffffc00ul, 0xffffffffu, 0x20030000ul, 0x20030001ul
	};
	unsigned i;

	for (i = 0; i < sizeof wild / sizeof wild[0]; i++) {
		errno = 0;
		printf("brk %08lx %s errno %d\n", wild[i],
		    brk((void *)wild[i]) == (void *)-1 ? "refused" : "granted",
		    errno);
	}
	printf("brk sbrk %s\n", sbrk(4096) == (void *)-1 ? "refused" : "granted");
	return 7;
}

/* The Makefile passes the variable AOUT_BIG whose length is checked here. */
static int
env_probe(void)
{
	const char *v = getenv("AOUT_BIG");
	const char *w = getenv("AOUT_BIG2");

	printf("env %lu %lu\n", v ? (unsigned long)strlen(v) : 0ul,
	    w ? (unsigned long)strlen(w) : 0ul);
	return 8;
}

/*
 * Integer division through the SIO divider model of aoutrun: signed and
 * unsigned operands of every sign pairing, the INT_MIN by -1 wrap, a zero
 * divisor (the helpers answer it in software) and operands above INT_MAX.
 * div.expected holds the C99 truncating results.
 */
static int
div_probe(void)
{
	static const int sn[] = { 7, -7, 7, -7, -2147483647 - 1, 0, 2147483647 };
	static const int sd[] = { 2, 2, -2, -2, -1, 5, 1 };
	static const unsigned un[] = { 4294967295u, 2147483648u, 12345u, 0u };
	static const unsigned ud[] = { 2u, 3u, 100000u, 7u };
	unsigned i;

	for (i = 0; i < sizeof sn / sizeof sn[0]; i++) {
		volatile int n = sn[i], d = sd[i];

		printf("sdiv %d %d %d %d\n", n, d, n / d, n % d);
	}
	for (i = 0; i < sizeof un / sizeof un[0]; i++) {
		volatile unsigned n = un[i], d = ud[i];

		printf("udiv %u %u %u %u\n", n, d, n / d, n % d);
	}
	{
		volatile int n = 9, z = 0;
		volatile unsigned un0 = 9u, uz = 0u;

		printf("zero %d %d %u %u\n", n / z, n % z, un0 / uz, un0 % uz);
	}
	return 9;
}

int
main(int argc, char **argv)
{
	volatile float a = 1.5f, b = 2.25f;
	volatile double d = 3.0;
	volatile long long big = -5000000000LL;
	volatile int num = 1000000, den = 7;
	volatile unsigned int un = 4000000000u, ud = 7u;
	volatile int sn = -7, sd = 2, imin = -2147483647 - 1;
	char *heap;
	int i;

	if (argc == 2 && strcmp(argv[1], "errno") == 0)
		return errno_probe();
	if (argc == 2 && strcmp(argv[1], "brk") == 0)
		return brk_probe();
	if (argc == 2 && strcmp(argv[1], "env") == 0)
		return env_probe();
	if (argc == 2 && strcmp(argv[1], "div") == 0)
		return div_probe();
	printf("argc=%d\n", argc);
	for (i = 0; i < argc; i++)
		printf("argv[%d]=%s\n", i, argv[i]);
	printf("div %d %d %d\n", num / den, -num / den, num % den);
	printf("div2 %u %u %d %d %d %d %d %d %d %d\n", un / ud, un % ud, sn / sd,
	    sn % sd, sn / -sd, sn % -sd, -sn / -sd, -sn % -sd, imin / 3,
	    imin % 3);
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
