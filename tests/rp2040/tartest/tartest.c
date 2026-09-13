/*
 * tartest: on-device check that tar's -Z filter carries an archive through
 * a pipe between two processes.
 *
 * bin/tar/tests/tartest.sh settles the header formats on the host, where
 * both formats are byte-identical to what the board writes. What only the
 * board answers is whether tar's fork, pipe and execl of /usr/bin/compress
 * work when each side owns a separate 96-kbyte USER_DATA_SIZE window and
 * compress alone carries 30 kbytes of bss for its LZW string table: tar
 * writes the archive into the pipe, compress reads it and writes the
 * compressed form to the archive fd, and the whole of it has to arrive.
 *
 * This builds a tree under /tmp, archives it with -Z, extracts it into a
 * second directory with -Z, and compares the bytes. Regression tool, not
 * shipped in the root manifest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#define SRC     "/tmp/tt"
#define DST     "/tmp/tx"
#define ARCHIVE "/tmp/tt.taz"

/*
 * A file whose size is not a multiple of the 512-byte tar record, so the
 * pad tar writes past its end is part of what the pipe has to carry.
 */
#define ODDLEN  777

static int failures;

static void
report(what, ok)
	char *what;
	int ok;
{
	printf("%-40s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok)
		failures++;
}

/*
 * Run tar and wait for it, so the compress child it forks has finished
 * before the archive is read back.
 */
static int
runtar(a1, a2, a3)
	char *a1, *a2, *a3;
{
	int pid, status, w;

	if ((pid = fork()) < 0)
		return (-1);
	if (pid == 0) {
		execl("/bin/tar", "tar", a1, a2, a3, (char *) 0);
		perror("/bin/tar");
		_exit(127);
	}
	while ((w = wait(&status)) != pid && w != -1)
		;
	return (status);
}

static int
writefile(path, len, seed)
	char *path;
	int len, seed;
{
	int fd, i;
	char c;

	if ((fd = creat(path, 0644)) < 0)
		return (-1);
	for (i = 0; i < len; i++) {
		c = (char) ('a' + ((i + seed) % 26));
		if (write(fd, &c, 1) != 1) {
			close(fd);
			return (-1);
		}
	}
	close(fd);
	return (0);
}

static int
samefile(a, b)
	char *a, *b;
{
	int fa, fb, na, nb, same = 1;
	char ba[64], bb[64];

	if ((fa = open(a, O_RDONLY)) < 0)
		return (0);
	if ((fb = open(b, O_RDONLY)) < 0) {
		close(fa);
		return (0);
	}
	for (;;) {
		na = read(fa, ba, sizeof(ba));
		nb = read(fb, bb, sizeof(bb));
		if (na != nb || (na > 0 && memcmp(ba, bb, na) != 0)) {
			same = 0;
			break;
		}
		if (na <= 0)
			break;
	}
	close(fa);
	close(fb);
	return (same);
}

int
main()
{
	struct stat st;
	int ok;

	mkdir(SRC, 0755);
	mkdir(DST, 0755);
	ok = writefile(SRC "/odd", ODDLEN, 0) == 0 &&
	     writefile(SRC "/plain", 40, 7) == 0;
	report("build the source tree", ok);
	if (!ok) {
		printf("tartest: %d failure(s)\n", failures);
		return (1);
	}

	unlink(ARCHIVE);
	if (chdir("/tmp") < 0) {
		perror("/tmp");
		return (1);
	}
	report("tar cZf through the compress pipe",
	    runtar("cZf", ARCHIVE, "tt") == 0);
	report("the compressed archive exists and is not empty",
	    stat(ARCHIVE, &st) == 0 && st.st_size > 0);

	if (chdir(DST) < 0) {
		perror(DST);
		return (1);
	}
	report("tar xZf through the compress pipe",
	    runtar("xZf", ARCHIVE, "tt") == 0);
	report("the odd-sized file survived the pipe",
	    samefile(SRC "/odd", DST "/tt/odd"));
	report("the second file survived the pipe",
	    samefile(SRC "/plain", DST "/tt/plain"));

	printf("tartest: %d failure(s)\n", failures);
	return (failures != 0);
}
