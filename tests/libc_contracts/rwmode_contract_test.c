/*
 * Contract gate for the read/write mode switch on an r+ stream.
 *
 * C17 7.21.5.3p7: output may be followed by input after an fflush or a
 * positioning call, and input may be followed by output after a
 * positioning call or once input reached end of file. The tree's _filbuf
 * and _flsbuf are compiled against the tree's headers over a host
 * descriptor, so the getc and putc macros and the FILE layout under test
 * are the target's; the file itself is a host temporary.
 *
 * The V7 core failed both directions: after fflush the write buffer's
 * free count let getc() read the output buffer as input, and putc() at
 * end of file wrote the consumed read-ahead back to the file as output.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The tree's stdio references these; the gate touches no standard stream. */
struct _iobuf _iob[3];
char *_smallbuf;

/*
 * The tree's struct stat is not the host's, so the core's fstat, which
 * only wants st_blksize, is answered here in the tree's layout instead of
 * letting the host write its own layout over a smaller object.
 */
int
test_fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_blksize = 512;
	return 0;
}

int db_ungetc(int, FILE *);

static int checks;
static int failures;

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		(void)write(2, message, strlen(message));
		(void)write(2, "\n", 1);
	}
}

int
main(void)
{
	char path[] = "/tmp/rwmodeXXXXXX";
	char back[16];
	FILE f;
	int fd, n;

	fd = mkstemp(path);
	check(fd >= 0, "mkstemp failed");
	(void)unlink(path);
	check(write(fd, "123456", 6) == 6, "seed write failed");
	check(lseek(fd, 0L, SEEK_SET) == 0, "seed rewind failed");

	memset(&f, 0, sizeof f);
	f._flag = _IORW;
	f._file = (short)fd;

	/* Output, then fflush, then input: input must continue at offset 3. */
	check(putc('a', &f) == 'a' && putc('b', &f) == 'b' && putc('c', &f) == 'c',
	    "putc on a fresh r+ stream fails");
	check(fflush(&f) == 0, "fflush after output fails");
	check(getc(&f) == '4', "getc after fflush does not read the file at the output position");
	check(getc(&f) == '5' && getc(&f) == '6',
	    "getc after the mode switch loses bytes");
	check(getc(&f) == EOF, "getc does not reach end of file");

	/* Input reached end of file, then output: appends at end. */
	check(putc('Q', &f) == 'Q', "putc at end of file fails");
	check(fflush(&f) == 0, "fflush after output at end of file fails");

	check(lseek(fd, 0L, SEEK_SET) == 0, "verification rewind failed");
	memset(back, 0, sizeof back);
	n = (int)read(fd, back, sizeof back - 1);
	check(n == 7 && memcmp(back, "abc456Q", 7) == 0,
	    "file content after write, read, write is not abc456Q");

	/*
	 * Output, fflush, then a pushback before any read: the stream is
	 * still in write mode when ungetc arrives, so ungetc must make the
	 * switch itself, or the next refill writes the pushed byte as output.
	 */
	check(lseek(fd, 0L, SEEK_SET) == 0 && ftruncate(fd, 0) == 0, "second seed rewind failed");
	check(write(fd, "a23", 3) == 3 && lseek(fd, 0L, SEEK_SET) == 0, "second seed failed");
	f._flag = _IORW;
	f._cnt = 0;
	f._ptr = f._base;
	check(putc('a', &f) == 'a' && fflush(&f) == 0, "second output fails");
	check(db_ungetc('X', &f) == 'X', "ungetc in write mode is refused");
	check(getc(&f) == 'X', "the pushed-back byte does not come back");
	check(getc(&f) == '2', "the byte after the pushback is not the file's next byte");
	check(fflush(&f) == 0, "fflush after the read fails");
	check(lseek(fd, 0L, SEEK_SET) == 0, "third rewind failed");
	memset(back, 0, sizeof back);
	n = (int)read(fd, back, sizeof back - 1);
	check(n == 3 && memcmp(back, "a23", 3) == 0,
	    "a pushed-back byte reached the file as output");

	/*
	 * A line-buffered r+ stream queues bytes through the putc macro
	 * without calling _flsbuf until the newline; entering write mode
	 * there must keep the queued prefix.
	 */
	check(lseek(fd, 0L, SEEK_SET) == 0 && ftruncate(fd, 0) == 0, "truncate failed");
	{
		static char lbuf[64];

		f._flag = _IORW | _IOLBF;
		f._base = f._ptr = lbuf;
		f._bufsiz = (int)sizeof lbuf;
		f._cnt = 0;
	}
	check(putc('a', &f) == 'a' && putc('b', &f) == 'b' && putc('c', &f) == 'c' &&
	    putc('\n', &f) == '\n', "line-buffered putc fails");
	check(lseek(fd, 0L, SEEK_SET) == 0, "fourth rewind failed");
	memset(back, 0, sizeof back);
	n = (int)read(fd, back, sizeof back - 1);
	check(n == 4 && memcmp(back, "abc\n", 4) == 0,
	    "a line-buffered r+ stream lost the bytes queued before the newline");

	(void)close(fd);
	if (failures == 0) {
		(void)write(1, "rwmode contracts: pass\n", 23);
		return 0;
	}
	(void)write(2, "rwmode contracts: fail\n", 23);
	(void)checks;
	return 1;
}
