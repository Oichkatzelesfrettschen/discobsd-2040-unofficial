/*
 * streamtest: exercise stdio past the NSTATIC static FILE count.
 *
 * lib/libc/stdio/findiop.c keeps NSTATIC static FILE slots (8: stdin,
 * stdout, stderr and five more); a stream beyond them comes from the
 * dynamic _f_morefiles path. This program holds N concurrent FILE objects
 * over ONE open file, so it costs one in-core inode and one file-table
 * entry however large N is (the first run opened N distinct files and the
 * 12th fopen failed on a PICOCALC kernel whose inode table was nearly
 * full). Stream 0 is fdopen() of the descriptor from open(2); streams 1 to
 * N-1 are fdopen() of a dup(2) of it. A dup shares the file offset, so each
 * stream positions itself with fseek before every transfer and fflush
 * pushes its record out before the next stream runs. Stream i owns bytes
 * [i*REC, (i+1)*REC) of the file: it writes its own record, and after all
 * streams have written, each reads its own record back, in reverse order,
 * and compares it. A final lseek/read over the raw descriptor checks the
 * file holds all N records, so a stream that read back from its own buffer
 * alone still fails there.
 *
 * Descriptors: 3 inherited, 1 from open and N-1 from dup, so N + 3; the
 * program names the failing call and errno on any error. The file is
 * unlinked right after it is created, so a failed run leaves nothing in
 * /tmp; the name is kept to report errors.
 *
 * Verdict lines: STREAMTEST OK (exit 0) and STREAMTEST FAILED (exit 1).
 * It is the on-device gate for the NSTATIC 20 -> 8 reduction.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define N	12		/* concurrent FILE objects */
#define REC	16		/* bytes each stream owns */

static FILE *stream[N];
static int failures;

static void
fail(const char *what, int i)
{
	printf("FAIL %s (stream %d): errno %d (%s)\n", what, i, errno,
	    strerror(errno));
	failures++;
}

static void
record(char *buf, int i)
{
	memset(buf, '.', REC);
	snprintf(buf, REC, "stream-%02d", i);
}

int
main(void)
{
	char name[32], want[REC], got[REC];
	int fd, i, d, held, written = 0, readback = 0, rawgood = 0;

	snprintf(name, sizeof name, "/tmp/streamtest.%d", (int)getpid());
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		fail("open", -1);
		printf("STREAMTEST FAILED\n");
		return 1;
	}
	if (unlink(name) < 0) {
		fail("unlink", -1);
		printf("STREAMTEST FAILED (%s left behind)\n", name);
		return 1;
	}

	for (i = 0; i < N; i++) {
		d = i == 0 ? fd : dup(fd);
		if (d < 0) {
			fail("dup", i);
			break;
		}
		stream[i] = fdopen(d, "r+");
		if (stream[i] == NULL) {
			fail("fdopen", i);
			if (i != 0)
				(void)close(d);
			break;
		}
	}
	for (held = 0; held < N && stream[held] != NULL; held++)
		;
	printf("holding %d of %d concurrent streams over one file\n", held,
	    N);

	for (i = 0; i < N && stream[i] != NULL; i++) {
		record(want, i);
		if (fseek(stream[i], (long)i * REC, SEEK_SET) != 0)
			fail("fseek (write)", i);
		else if (fwrite(want, 1, REC, stream[i]) != REC)
			fail("fwrite", i);
		else if (fflush(stream[i]) != 0)
			fail("fflush", i);
		else
			written++;
	}
	for (i = N - 1; i >= 0; i--) {
		if (stream[i] == NULL)
			continue;
		record(want, i);
		if (fseek(stream[i], (long)i * REC, SEEK_SET) != 0)
			fail("fseek (read)", i);
		else if (fread(got, 1, REC, stream[i]) != REC)
			fail("fread", i);
		else if (memcmp(got, want, REC) != 0) {
			printf("FAIL stream %d read back \"%.*s\", not its own "
			    "record\n", i, REC, got);
			failures++;
		} else
			readback++;
	}
	printf("wrote %d, read back %d of %d\n", written, readback, N);

	for (i = 0; i < N; i++) {
		record(want, i);
		if (lseek(fd, (long)i * REC, SEEK_SET) < 0)
			fail("lseek (raw)", i);
		else if (read(fd, got, REC) != REC)
			fail("read (raw)", i);
		else if (memcmp(got, want, REC) != 0) {
			printf("FAIL record %d in the file is \"%.*s\"\n", i,
			    REC, got);
			failures++;
		} else
			rawgood++;
	}
	printf("file holds %d of %d records\n", rawgood, N);

	for (i = 0; i < N; i++)
		if (stream[i] != NULL && fclose(stream[i]) != 0)
			fail("fclose", i);

	if (failures == 0 && held == N && written == N && readback == N && rawgood == N) {
		printf("STREAMTEST OK (%d streams, 1 inode)\n", N);
		return 0;
	}
	printf("STREAMTEST FAILED\n");
	return 1;
}
