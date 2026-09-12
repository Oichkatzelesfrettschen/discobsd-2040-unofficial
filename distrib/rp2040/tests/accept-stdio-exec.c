#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int
cmp(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

int
main(void)
{
	FILE *f;
	char line[64];
	int v[4] = { 4, 1, 3, 2 };
	char *p = malloc(32);
	char *args[] = { "/bin/nonesuch", NULL };

	qsort(v, 4, sizeof(v[0]), cmp);
	if (p) {
		p[0] = 'a';
		free(p);
	}
	if (execve(args[0], args, NULL) < 0)
		printf("exec errno %d\n", errno);
	f = fopen("/etc/motd", "r");
	if (f == NULL) {
		puts("no file");
	} else {
		if (fgets(line, sizeof(line), f) == NULL)
			puts("empty file");
		else
			printf("line: %s", line);
		fclose(f);
	}
	printf("sorted: %d %d %d %d\n", v[0], v[1], v[2], v[3]);
	printf("strtol: %ld\n", strtol("123", NULL, 10));
	return 0;
}
