/*
 * Contract gate for the C17 stdio surface this tree implements outside the
 * read and write path: the mode strings fopen accepts, the descriptor flags
 * they produce, fflush over every stream, rewind as a seek, the position
 * pair, and setvbuf's argument checking.
 *
 * The tree's sources are compiled against the tree's headers, so the FILE
 * layout and the getc and putc macros under test are the target's. open(2)
 * is answered here because the tree's O_APPEND is 0x0008 and O_TRUNC is
 * 0x0400, which are not the host's values; the shim both translates them
 * for the host kernel and records what _sflags asked for.
 *
 * ANSI_REAL_FINDIOP links the tree's findiop.c, whose static assertion pins
 * sizeof(FILE) at the target's 24 bytes and so compiles only where pointers
 * are four bytes. Without it the stream table and the walk are the test's
 * own, and every other check is the same at both widths.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ANSI_REAL_FINDIOP
/*
 * The three standard streams the tree's sources name, and the two table
 * functions fopen and fflush reach. Eight slots match NSTATIC in findiop.c.
 */
struct _iobuf _iob[8];

FILE *
_findiop(void)
{
	FILE *iop;

	for (iop = _iob; iop < &_iob[8]; iop++)
		if ((iop->_flag & (_IOREAD|_IOWRT|_IORW)) == 0)
			return (iop);
	return (NULL);
}

int
_fwalk(int (*function)(FILE *))
{
	FILE *iop;
	int status;

	status = 0;
	for (iop = _iob; iop < &_iob[8]; iop++)
		if ((iop->_flag & (_IOREAD|_IOWRT|_IORW)) != 0 &&
		    (*function)(iop) == EOF)
			status = EOF;
	return (status);
}

char *_smallbuf;
#endif

int ansi_test_errno;

/*
 * The tree's struct stat is not the host's, so the core's fstat, which wants
 * st_blksize alone, is answered in the tree's layout rather than letting the
 * host write its own layout over a smaller object.
 */
int
test_fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_blksize = 512;
	return (0);
}

static int open_flags;		/* the tree flags of the last test_open */

/*
 * The tree sources reach this through -Dopen=test_open, which also rewrites
 * the declaration in <fcntl.h>, so the parameter list is that declaration's.
 * This translation unit is compiled without the rename and calls the host's
 * open(2) below.
 */
int
test_open(const char *path, int flags, ...)
{
	va_list ap;
	int host, mode;

	va_start(ap, flags);
	mode = va_arg(ap, int);
	va_end(ap);

	open_flags = flags;
	host = flags & 3;		/* O_RDONLY, O_WRONLY and O_RDWR agree */
	if (flags & O_CREAT)
		host |= 0100;		/* the host's O_CREAT */
	if (flags & O_TRUNC)
		host |= 01000;		/* the host's O_TRUNC */
	if (flags & O_APPEND)
		host |= 02000;		/* the host's O_APPEND */
	if (flags & O_EXCL)
		host |= 0200;		/* the host's O_EXCL */
	return (open(path, host, mode));
}

int db_fclose(FILE *);
int db_fflush(FILE *);
int db_fseek(FILE *, long, int);
long db_ftell(FILE *);
void db_rewind(FILE *);
int db_setvbuf(FILE *, char *, int, size_t);
int db_fgetpos(FILE *, fpos_t *);
int db_fsetpos(FILE *, const fpos_t *);
FILE *db_fopen(const char *, const char *);
FILE *db_fdopen(int, const char *);

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

/*
 * C17 7.21.5.3p3 lists the mode strings and where '+', 'b' and 'x' may
 * stand in them. Each row is the whole answer _sflags owes: the stream flag
 * that decides the direction, and the descriptor flags open(2) receives.
 */
static const struct {
	const char *mode;
	int sflag;
	int oflags;
} modes[] = {
	{ "r",     _IOREAD, O_RDONLY },
	{ "rb",    _IOREAD, O_RDONLY },
	{ "r+",    _IORW,   O_RDWR },
	{ "rb+",   _IORW,   O_RDWR },
	{ "r+b",   _IORW,   O_RDWR },
	{ "w",     _IOWRT,  O_TRUNC|O_CREAT|O_WRONLY },
	{ "wb",    _IOWRT,  O_TRUNC|O_CREAT|O_WRONLY },
	{ "w+",    _IORW,   O_TRUNC|O_CREAT|O_RDWR },
	{ "wb+",   _IORW,   O_TRUNC|O_CREAT|O_RDWR },
	{ "w+b",   _IORW,   O_TRUNC|O_CREAT|O_RDWR },
	{ "wx",    _IOWRT,  O_EXCL|O_CREAT|O_WRONLY },
	{ "w+x",   _IORW,   O_EXCL|O_CREAT|O_RDWR },
	{ "wb+x",  _IORW,   O_EXCL|O_CREAT|O_RDWR },
	{ "a",     _IOWRT,  O_APPEND|O_CREAT|O_WRONLY },
	{ "ab",    _IOWRT,  O_APPEND|O_CREAT|O_WRONLY },
	{ "a+",    _IORW,   O_APPEND|O_CREAT|O_RDWR },
	{ "ab+",   _IORW,   O_APPEND|O_CREAT|O_RDWR },
	{ "a+b",   _IORW,   O_APPEND|O_CREAT|O_RDWR },
};

static void
check_modes(void)
{
	unsigned i;
	int sflag, oflags;

	for (i = 0; i < sizeof modes / sizeof modes[0]; i++) {
		oflags = -1;
		sflag = _sflags(modes[i].mode, &oflags);
		check(sflag == modes[i].sflag, modes[i].mode);
		check(oflags == modes[i].oflags, modes[i].mode);
	}

	oflags = -1;
	check(_sflags("", &oflags) == 0, "an empty mode string is accepted");
	check(_sflags("z", &oflags) == 0, "mode \"z\" is accepted");
	check(_sflags("+", &oflags) == 0, "mode \"+\" is accepted");
}

static void
check_setvbuf(void)
{
	char buf[64];
	FILE f;

	memset(&f, 0, sizeof f);
	f._flag = _IOWRT;
	f._file = -1;

	/*
	 * _bufsiz is an int, so a size past INT_MAX cannot be recorded.
	 * <stdio.h> makes size_t an unsigned int at every width this tree
	 * builds for, so the bound and a signed reading of the size agree on
	 * every value and this check pins the refusal rather than the form
	 * the refusal takes.
	 */
	check(db_setvbuf(&f, buf, _IOFBF, (size_t)2147483647 + 1) == EOF,
	    "setvbuf accepts a size past INT_MAX");
	check(db_setvbuf(&f, buf, 99, sizeof buf) == EOF,
	    "setvbuf accepts a mode that is none of the three");

	check(db_setvbuf(&f, buf, _IOLBF, sizeof buf) == 0,
	    "setvbuf refuses line buffering with a caller's buffer");
	check((f._flag & _IOLBF) != 0, "setvbuf leaves _IOLBF clear");
	check(f._base == buf, "setvbuf ignores the caller's buffer");
	check(f._bufsiz == (int)sizeof buf, "setvbuf records the wrong size");

	/*
	 * Replacing the buffer discards a parked pushback byte with it, which
	 * C17 7.21.7.10p2 requires of a positioning call and which a stale
	 * _IOUNGET would otherwise hand to the next refill along with the
	 * count _bufsiz was carrying for it.
	 */
	f._flag |= _IOUNGET;
	f._ub[0] = 'z';
	check(db_setvbuf(&f, buf, _IONBF, 0) == 0, "setvbuf refuses _IONBF");
	check((f._flag & _IOUNGET) == 0, "setvbuf keeps a parked pushback byte");
	check((f._flag & _IONBF) != 0, "setvbuf leaves _IONBF clear");
	check(f._base == NULL, "an unbuffered stream keeps a base");
}

static void
check_positions(int fd)
{
	fpos_t pos;
	FILE f;
	int c;

	check(lseek(fd, 0L, SEEK_SET) == 0, "seed rewind failed");
	memset(&f, 0, sizeof f);
	f._flag = _IOREAD;
	f._file = (short)fd;

	check(getc(&f) == '0', "the first byte is not read");
	check(getc(&f) == '1', "the second byte is not read");
	check(db_ftell(&f) == 2L, "ftell does not account for the buffer");
	check(db_fgetpos(&f, &pos) == 0, "fgetpos fails on a readable stream");

	check(db_fseek(&f, 6L, SEEK_SET) == 0, "fseek to an absolute offset fails");
	check(getc(&f) == '6', "fseek leaves the stream at the wrong byte");

	check(db_fsetpos(&f, &pos) == 0, "fsetpos fails");
	check(getc(&f) == '2', "fsetpos does not restore the recorded position");

	/*
	 * C17 7.21.9.5: rewind is a seek to the start with the error
	 * indicator cleared as well, which is the part fseek does not do.
	 */
	f._flag |= _IOERR;
	db_rewind(&f);
	check((f._flag & _IOERR) == 0, "rewind keeps the error indicator");
	check((f._flag & _IOEOF) == 0, "rewind keeps the end-of-file indicator");
	check(getc(&f) == '0', "rewind does not reach the first byte");

	while ((c = getc(&f)) != EOF)
		;
	check((f._flag & _IOEOF) != 0, "reading to the end sets no indicator");
	db_rewind(&f);
	check((f._flag & _IOEOF) == 0, "rewind keeps end of file set");

	if (f._flag & _IOMYBUF)
		free(f._base);
}

/*
 * C17 7.21.5.2p3: a null stream flushes every stream for which a flush is
 * defined. The pending output below is queued through the putc macro, so a
 * fflush that dereferenced its argument would fault before reaching it.
 */
static void
check_fflush_all(int fd)
{
	char buf[16];
	char back[16];
	FILE *fp;

	check(ftruncate(fd, 0) == 0, "truncate for the flush check failed");
	check(lseek(fd, 0L, SEEK_SET) == 0, "rewind for the flush check failed");

	/*
	 * The stream comes from the table rather than the stack, because the
	 * walk reaches the table alone.
	 */
	fp = _findiop();
	check(fp != NULL, "_findiop has no free stream");
	if (fp == NULL)
		return;
	memset(fp, 0, sizeof *fp);
	fp->_flag = _IOWRT;
	fp->_file = (short)fd;
	check(db_setvbuf(fp, buf, _IOFBF, sizeof buf) == 0,
	    "setvbuf for the flush check failed");

	check(putc('A', fp) == 'A' && putc('B', fp) == 'B',
	    "putc on a buffered stream fails");
	check(lseek(fd, 0L, SEEK_END) == 0,
	    "the buffered bytes reached the file before the flush");

	check(db_fflush(NULL) == 0, "fflush(NULL) does not report success");

	check(lseek(fd, 0L, SEEK_SET) == 0, "verification rewind failed");
	memset(back, 0, sizeof back);
	check(read(fd, back, sizeof back - 1) == 2 &&
	    memcmp(back, "AB", 2) == 0,
	    "fflush(NULL) does not flush a stream the walk reaches");
	fp->_flag = 0;
}

/*
 * C17 7.21.5.3p6: every write to an append stream goes to the end of the
 * file, which survives a seek away from it. The old core seeked to the end
 * once at open and let a later seek move the writes.
 */
static void
check_append(void)
{
	char path[] = "/tmp/stdioansiXXXXXX";
	char back[32];
	FILE *fp;
	int fd, n;

	fd = mkstemp(path);
	check(fd >= 0, "mkstemp for the append check failed");
	check(write(fd, "0123", 4) == 4, "append seed write failed");
	check(close(fd) == 0, "append seed close failed");

	fp = db_fopen(path, "a");
	check(fp != NULL, "fopen in append mode fails");
	if (fp != NULL) {
		check((open_flags & O_APPEND) != 0,
		    "append mode does not reach open(2) as O_APPEND");
		check((open_flags & O_TRUNC) == 0,
		    "append mode truncates the file");
		check(db_ftell(fp) == 4L,
		    "an append stream does not start at the end of the file");
		check(db_fseek(fp, 0L, SEEK_SET) == 0,
		    "a seek on an append stream fails");
		check(putc('X', fp) == 'X', "putc on an append stream fails");
		check(db_fclose(fp) == 0, "fclose on an append stream fails");
	}

	fd = open(path, 0, 0);
	check(fd >= 0, "reopening the append file failed");
	memset(back, 0, sizeof back);
	n = (int)read(fd, back, sizeof back - 1);
	check(n == 5 && memcmp(back, "0123X", 5) == 0,
	    "a write after a seek on an append stream did not go to the end");
	(void)close(fd);
	(void)unlink(path);
}

/*
 * C17 7.21.5.3p3: the 'x' of a w mode makes the create fail rather than
 * truncate a file that is already there.
 */
static void
check_exclusive(void)
{
	char path[] = "/tmp/stdioexclXXXXXX";
	FILE *fp;
	int fd;

	fd = mkstemp(path);
	check(fd >= 0, "mkstemp for the exclusive check failed");
	check(write(fd, "keep", 4) == 4, "exclusive seed write failed");
	check(close(fd) == 0, "exclusive seed close failed");

	fp = db_fopen(path, "wx");
	check(fp == NULL, "mode \"wx\" opens a file that already exists");
	if (fp != NULL)
		(void)db_fclose(fp);

	fd = open(path, 0, 0);
	check(fd >= 0, "the exclusive check removed the file");
	if (fd >= 0) {
		char back[8];

		memset(back, 0, sizeof back);
		check(read(fd, back, sizeof back - 1) == 4 &&
		    memcmp(back, "keep", 4) == 0,
		    "mode \"wx\" truncated a file it refused to open");
		(void)close(fd);
	}
	(void)unlink(path);
}

int
main(void)
{
	char path[] = "/tmp/stdioansiXXXXXX";
	char report[64];
	int fd;

	fd = mkstemp(path);
	check(fd >= 0, "mkstemp failed");
	(void)unlink(path);
	check(write(fd, "0123456789", 10) == 10, "seed write failed");

	check_modes();
	check_setvbuf();
	check_positions(fd);
	check_fflush_all(fd);
	check_append();
	check_exclusive();

	(void)close(fd);

	(void)snprintf(report, sizeof report, "ansi: %d checks, %d failures\n",
	    checks, failures);
	(void)write(1, report, strlen(report));
	return (failures != 0);
}
