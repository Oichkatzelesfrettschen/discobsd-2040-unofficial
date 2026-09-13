/*
 * tail [+-][n][lbc][rf] [file]
 *
 * Copy the end of a file or its beginning past a point. The end of a
 * seekable file is found by scanning backward one block at a time and
 * counting newlines, so a line may be any length and only one block is
 * resident. A pipe is spooled into memory while it fits MEMSIZE bytes
 * and into a temporary file past that, and the same backward scan runs
 * over the spool; when the temporary file cannot be made the last
 * MEMSIZE bytes are kept and the loss is reported.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The port's unistd.h leaves these to stdio.h, which this file avoids. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#endif

#define BLKSIZE 1024        /* one scan or copy block */
#define MEMSIZE 4096        /* a pipe is held here before it spools */

static char blk[BLKSIZE];
static char mem[MEMSIZE];

/*
 * The source being scanned: a file descriptor with a known size, or
 * mem[] holding a short pipe. memoff is the offset of mem[0] within
 * the stream, which is nonzero only for the ring kept after a failed
 * spool.
 */
static int srcfd = -1;
static off_t srcsize;
static off_t memoff;
static int follow, seekable, piped;

/* Messages go out with write: a formatted print would link _doprnt. */
static void
say(const char *s)
{
    (void)write(2, s, strlen(s));
}

static void
saynum(long v)
{
    char buf[12];
    int i = sizeof buf;

    buf[--i] = '\0';
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while (v > 0);
    say(buf + i);
}

/* Report what failed and the errno text, then leave. */
static void
fail(const char *what)
{
    say("tail: ");
    say(what);
    say(": ");
    say(strerror(errno));
    say("\n");
    exit(1);
}

static void
usage(void)
{
    say("usage: tail [+_[n][lbc][rf]] [file]\n");
    exit(2);
}

/* Read n bytes of the source at off into buf; short only at its end. */
static int
srcread(off_t off, char *buf, int n)
{
    if (srcfd < 0) {
        off_t rel = off - memoff;

        if (rel < 0 || rel >= srcsize - memoff)
            return 0;
        if (n > srcsize - memoff - rel)
            n = srcsize - memoff - rel;
        memcpy(buf, mem + rel, n);
        return n;
    }
    if (lseek(srcfd, off, SEEK_SET) == -1)
        return -1;
    return read(srcfd, buf, n);
}

/*
 * Take standard input, which cannot seek, into mem[] and then into an
 * unlinked temporary file. Past MEMSIZE without a temporary file the
 * ring keeps the newest MEMSIZE bytes and memoff moves up.
 */
static void
spool(void)
{
    char name[] = "/tmp/tailXXXXXX";
    int fd = -1, n, failed = 0;
    off_t total = 0;

    for (;;) {
        n = read(0, blk, BLKSIZE);
        if (n < 0)
            fail("read");
        if (n == 0)
            break;
        if (fd < 0 && !failed && total + n > MEMSIZE) {
            fd = mkstemp(name);
            if (fd < 0) {
                failed = errno;
            } else {
                (void)unlink(name);
                if (write(fd, mem, (int)total) != (int)total)
                    fail("spool");
            }
        }
        if (fd >= 0) {
            if (write(fd, blk, n) != n)
                fail("spool");
        } else if (total + n <= MEMSIZE) {
            memcpy(mem + total, blk, n);
        } else {
            /* ring: drop the oldest bytes to make room */
            int keep = MEMSIZE - n;

            if (keep < 0)
                keep = 0;
            memmove(mem, mem + (MEMSIZE - keep), keep);
            memcpy(mem + keep, blk + (n - (MEMSIZE - keep)), MEMSIZE - keep);
            memoff = total + n - MEMSIZE;
        }
        total += n;
    }
    srcsize = total;
    if (fd >= 0) {
        srcfd = fd;
    } else if (failed) {
        say("tail: cannot spool standard input: ");
        say(strerror(failed));
        say("; only the last ");
        saynum(MEMSIZE);
        say(" of ");
        saynum((long)total);
        say(" bytes are kept\n");
    }
}

/*
 * Write [from, to) of the source to standard output. A memory source
 * is written in place; a file source goes through mem[], which is idle
 * then, so the block a caller is still scanning stays intact.
 */
static void
copyout(off_t from, off_t to)
{
    int n, want;

    if (srcfd < 0) {
        if (from < memoff)
            from = memoff;
        if (to > srcsize)
            to = srcsize;
        if (from < to)
            (void)write(1, mem + (from - memoff), (int)(to - from));
        return;
    }
    while (from < to) {
        want = to - from > MEMSIZE ? MEMSIZE : (int)(to - from);
        n = srcread(from, mem, want);
        if (n <= 0)
            break;
        (void)write(1, mem, n);
        from += n;
    }
}

/*
 * Offset of the first byte of the last n lines: the byte after the
 * (n+1)-th newline from the end of the file, or the start when there
 * are fewer. A file whose last byte is not a newline therefore yields
 * n complete lines and the partial one, as it always has.
 */
static off_t
lastlines(long n)
{
    off_t pos = srcsize, lo, base = srcfd < 0 ? memoff : 0;
    int k, got;

    n++;
    while (pos > base) {
        lo = pos - BLKSIZE < base ? base : pos - BLKSIZE;
        got = srcread(lo, blk, (int)(pos - lo));
        if (got <= 0)
            break;
        for (k = got; k-- > 0; )
            if (blk[k] == '\n' && --n <= 0)
                return lo + k + 1;
        pos = lo;
    }
    return base;
}

/* Write [from, to) and a newline when the line lacked its own. */
static void
emit(off_t from, off_t to, int addnl)
{
    copyout(from, to);
    if (addnl)
        (void)write(1, "\n", 1);
}

/*
 * Write the last n lines (all when n < 0) last first. A line is the
 * bytes after one newline through the next; the last line gets a
 * newline when the file ends without one.
 */
static void
reverse(long n)
{
    off_t pos, lo, end, base = srcfd < 0 ? memoff : 0;
    int k, got, missing = 0;

    end = srcsize;
    if (end == base) {
        /* an empty input has always produced one newline */
        (void)write(1, "\n", 1);
        return;
    }
    missing = !(srcread(end - 1, blk, 1) == 1 && blk[0] == '\n');
    pos = missing ? end : end - 1;
    while (pos > base && n != 0) {
        lo = pos - BLKSIZE < base ? base : pos - BLKSIZE;
        got = srcread(lo, blk, (int)(pos - lo));
        if (got <= 0)
            break;
        for (k = got; k-- > 0; ) {
            if (blk[k] != '\n')
                continue;
            emit(lo + k + 1, end, missing && end == srcsize);
            end = lo + k + 1;
            if (n > 0 && --n == 0)
                return;
        }
        pos = lo;
    }
    if (n != 0 && end > base)
        emit(base, end, missing && end == srcsize);
}

int
main(int argc, char **argv)
{
    long n;
    int i, bylines, bkwds, fromend;
    char *arg;
    struct stat st;

    arg = argv[1];
    if (argc <= 1 || (*arg != '-' && *arg != '+')) {
        arg = "-10l";
        argc++;
        argv--;
    }
    fromend = *arg == '-';
    arg++;
    if (isdigit((unsigned char)*arg)) {
        n = 0;
        while (isdigit((unsigned char)*arg))
            n = n * 10 + *arg++ - '0';
    } else
        n = -1;
    if (!fromend && n > 0)
        n--;
    if (argc > 2) {
        (void)close(0);
        if (open(argv[2], 0) != 0)
            fail(argv[2]);
    }
    bylines = -1;
    bkwds = 0;
    while (*arg)
        switch (*arg++) {
        case 'b':
            if (n == -1)
                n = 1;
            n <<= 9;
            if (bylines != -1)
                usage();
            bylines = 0;
            break;
        case 'c':
            if (bylines != -1)
                usage();
            bylines = 0;
            break;
        case 'f':
            follow = 1;
            break;
        case 'r':
            bkwds = 1;
            fromend = 1;
            bylines = 1;
            break;
        case 'l':
            if (bylines != -1)
                usage();
            bylines = 1;
            break;
        default:
            usage();
        }
    if (n == -1)
        n = bkwds ? -1 : 10;
    if (bylines == -1)
        bylines = 1;
    if (bkwds)
        follow = 0;

    piped = lseek(0, (off_t)0, SEEK_CUR) == -1 && errno == ESPIPE;
    seekable = !piped && fstat(0, &st) == 0 &&
        (st.st_mode & S_IFMT) == S_IFREG;

    if (!fromend) {
        /* skip n lines or bytes, then copy */
        if (bylines) {
            while (n > 0 && (i = read(0, blk, BLKSIZE)) > 0) {
                char *p = blk;

                while (i > 0 && n > 0) {
                    if (*p++ == '\n')
                        n--;
                    i--;
                }
                if (n == 0 && i > 0)
                    (void)write(1, p, i);
            }
        } else if (n > 0) {
            if (seekable)
                (void)lseek(0, (off_t)n, SEEK_SET);
            else
                while (n > 0 && (i = read(0, blk,
                    n > BLKSIZE ? BLKSIZE : (int)n)) > 0)
                    n -= i;
        }
        while ((i = read(0, blk, BLKSIZE)) > 0)
            (void)write(1, blk, i);
        goto done;
    }

    if (n <= 0 && !(bkwds && n < 0))
        goto done;
    if (seekable) {
        srcfd = 0;
        srcsize = st.st_size;
    } else
        spool();

    if (bkwds)
        reverse(n);
    else if (bylines)
        copyout(lastlines(n), srcsize);
    else
        copyout(srcsize > n ? srcsize - n : 0, srcsize);
done:
    if (follow && !piped && !bkwds)
        for (;;) {
            sleep(1);
            while ((i = read(0, blk, BLKSIZE)) > 0)
                (void)write(1, blk, i);
        }
    exit(0);
}
