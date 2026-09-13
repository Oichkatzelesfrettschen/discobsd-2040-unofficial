/*
 * streamtest: exercise stdio past the NSTATIC static FILE count.
 *
 * lib/libc/stdio/findiop.c keeps NSTATIC static FILE slots (8: stdin,
 * stdout, stderr and five more); the ninth concurrent stream falls to the
 * dynamic _f_morefiles path. This opens twelve streams at once, so slots
 * nine through twelve come from _f_morefiles, writes and reads each back to
 * confirm the dynamically allocated FILEs work, and reports. It is the
 * on-device gate for the NSTATIC 20 -> 8 reduction.
 */
#include <stdio.h>
#include <string.h>

#define N 12

int
main(void)
{
	FILE *f[N];
	char name[24], line[32];
	int i, opened = 0, readback = 0;

	for (i = 0; i < N; i++) {
		sprintf(name, "/tmp/st%d", i);
		f[i] = fopen(name, "w+");
		if (f[i] == NULL) {
			printf("open %d FAILED\n", i);
			continue;
		}
		opened++;
		fprintf(f[i], "stream-%d\n", i);
	}
	printf("opened %d of %d concurrent streams\n", opened, N);

	for (i = 0; i < N; i++) {
		if (f[i] == NULL)
			continue;
		rewind(f[i]);
		if (fgets(line, sizeof line, f[i]) != NULL) {
			sprintf(name, "stream-%d\n", i);
			if (strcmp(line, name) == 0)
				readback++;
		}
		fclose(f[i]);
	}
	printf("read back %d of %d\n", readback, opened);

	if (opened == N && readback == N) {
		printf("STREAMTEST OK (slots 9-12 via _f_morefiles)\n");
		return 0;
	}
	printf("STREAMTEST FAILED\n");
	return 1;
}
