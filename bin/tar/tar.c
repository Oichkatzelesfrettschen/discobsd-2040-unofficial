/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Tape Archival Program
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <sys/ioctl.h>
#ifndef __APPLE__		/* no tape ioctls on macOS; backtape seeks */
#include <sys/mtio.h>
#endif
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <sys/wait.h>

#define TBLOCK  512
#define NBLOCK  20
#define NAMSIZ  100
#define TPFSZ   155                     /* ustar prefix field */
#define UGSZ    32                      /* ustar uname and gname fields */

/*
 * A path an ustar header can carry: a full prefix field, a joining slash, a
 * full name field, and the terminator neither field leaves room for. NAMSIZ
 * alone is the v7 limit.
 */
#define PATHSIZ (TPFSZ + 1 + NAMSIZ + 1)
#ifndef TAR_PATH_LIMIT
#define TAR_PATH_LIMIT MAXPATHLEN
#endif

/*
 * The LZW codec is a separate program reached through a pipe rather than a
 * library linked in: usr.bin/compress carries 30 kbytes of bss for its
 * string table, and tar and the filter are separate processes with a
 * 144-kbyte USER_DATA_SIZE window each, where linking the codec would have
 * to fit both tables and the block buffer in one.
 */
#ifndef COMPRESS
#define COMPRESS        "/usr/bin/compress"
#endif

#define TMAGIC          "ustar"
#define TMAGLEN         6
#define TVERSION        "00"
#define TVERSLEN        2

/*
 * typeflag values. The v7 header calls the field linkflag and writes only
 * '\0', '1' and '2' into it; ustar adds the rest.
 */
#define REGTYPE         '0'
#define AREGTYPE        '\0'
#define LNKTYPE         '1'
#define SYMTYPE         '2'
#define DIRTYPE         '5'

#define writetape(b)    writetbuf(b, 1)

/*
 * The v7 header and the POSIX.1-1990 ustar header agree byte for byte over
 * the first 257 bytes, name through linkname, so one struct describes both:
 * a v7 header leaves the tail from magic onward zero, and readers tell the
 * two apart by the magic field alone. Field widths follow HD_USTAR in
 * 4.4BSD-Lite2 bin/pax/tar.h; the fields sum to 500 bytes and pad to 512.
 */
union hblock {
    char dummy[TBLOCK];
    struct header {
        char name[NAMSIZ];              /*   0 */
        char mode[8];                   /* 100 */
        char uid[8];                    /* 108 */
        char gid[8];                    /* 116 */
        char size[12];                  /* 124 */
        char mtime[12];                 /* 136 */
        char chksum[8];                 /* 148 */
        char linkflag;                  /* 156 */
        char linkname[NAMSIZ];          /* 157 */
        char magic[TMAGLEN];            /* 257 */
        char version[TVERSLEN];         /* 263 */
        char uname[UGSZ];               /* 265 */
        char gname[UGSZ];               /* 297 */
        char devmajor[8];               /* 329 */
        char devminor[8];               /* 337 */
        char prefix[TPFSZ];             /* 345 */
        char pad[12];                   /* 500 */
    } dbuf;
};

/*
 * One node per inode that carries more than one link, held for the whole of
 * a create run so a second link to the same inode becomes an LNKTYPE entry
 * naming the first. The path is a separate allocation of exactly its own
 * length, so the list costs what the paths present measure rather than the
 * PATHSIZ width an ustar header can carry: the list shares one 144-kbyte
 * USER_DATA_SIZE window with tar's text, data, bss and stack, and the number
 * of link identities a create run holds is what that window has room for.
 * Past it getmem() fails, and every further link to an unrecorded inode
 * enters the archive as a second full copy of the file.
 */
struct linkbuf {
    ino_t   inum;
    dev_t   devnum;
    int count;
    char    *pathname;
    struct  linkbuf *nextp;
};

struct extracted_identity {
    ino_t inode;
    dev_t device;
    struct extracted_identity *next;
};

/*
 * The node holds a pointer to its path rather than the path, so it stays
 * narrower than the header's own name field. bin/tar/tests/tartest.sh
 * measures the list against many live link identities.
 */
_Static_assert(sizeof(struct linkbuf) < NAMSIZ,
    "a linkbuf must not inline a fixed-width path");

static union hblock dblock;
static union hblock *tbuf;
static struct linkbuf *ihead;
static struct stat stbuf;
static struct extracted_identity *extracted_files;

static void usage(void);
static int openmt(const char *, int);
static char *tarcwd(char *);
static void dorep(char **);
static int endtape(void);
static void getdir(void);
static void passtape(void);
static char *getmem(size_t);
static void putfile(char *, char *, const char *);
static void doxtract(char **);
static void dotable(char **);
static void putempty(void);
static void longt(const struct stat *);
static void pmode(const struct stat *);
static void selectbits(const int *, const struct stat *);
static void tomodes(const struct stat *);
static void putoctal(char *, size_t, unsigned long);
static int putheader(const char *, char);
static void zfilter(int);
static int zreap(void);
static int isustar(void);
static const char *uidname(uid_t);
static const char *gidname(gid_t);
static unsigned long getoctal(const char *, size_t);
static int checksum(void);
static int checkw(int, const char *);
static int response(void);
static int checkf(const char *, mode_t, int);
static int checkupdate(const char *);
static _Noreturn void done(int);
static int wantit(char **);
static int prefix(const char *, const char *);
static int readtape(char *);
static size_t readtbuf(char **, size_t);
static int writetbuf(const char *, int);
static void backtape(void);
static void flushtape(void);
static void mterr(const char *, ssize_t, int);
static ssize_t bread(int, char *, size_t);
static void getbuf(void);
static void dodirtimes(char *);
static void setimes(char *, time_t);
static void validate_archive_path(const char *);
static void validate_symlink_target(const char *, const char *);
static void open_verified_directory(const char *, mode_t, int, int, int);
static char *enter_parent_directories(int, char *, int, int);
static int open_verified_regular(const char *, int);
static int create_output_file(const char *, mode_t);
static int write_all(int, const void *, size_t);
static void record_extracted_file(int);
static int was_extracted_file(const struct stat *);
static void free_extracted_files(void);
static _Noreturn void archive_error(const char *);
static _Noreturn void archive_path_error(const char *, const char *);

static int rflag;
static int xflag;
static int vflag;
static int tflag;
static int cflag;
static int mflag;
static int fflag;
static int iflag;
static int oflag;
static int pflag;
static int wflag;
static int hflag;
static int Bflag;
static int Fflag;
static int Oflag;              /* write the v7 header instead of ustar */
static int zflag;              /* pipe the archive through COMPRESS */

static pid_t zpid = -1;        /* the filter, while it runs */
static int zreading;

/*
 * The full path of the header last read: the prefix field, a slash and the
 * name field for an ustar header, the name field alone for a v7 one. The
 * extract and table paths work from this rather than from dbuf.name, which
 * holds at most the trailing NAMSIZ-1 bytes of an ustar path.
 */
static char curname[PATHSIZ];

/*
 * The link target of the header last read. The linkname field carries no
 * terminator when a target fills its 100 bytes, so reading it in place runs
 * into the magic field and hands symlink() and link() a target with "ustar"
 * appended.
 */
static char curlink[NAMSIZ + 1];

static int mt;
/* The target signal.h predates sig_atomic_t; int is one native word. */
static volatile int term;
static int chksum;
static int recno;
static int first;
static int prtlinkerr;
static int freemem = 1;
static int nblock;
static int extraction_root_descriptor = -1;
static int operation_failed;

static FILE *vfile;
static FILE *tfile;
static char tname[] = "/tmp/tarXXXXXX";
static const char *usefile;
static char magtape[] = "/dev/rmt8";

static void
onintr(int sig)
{
    (void) signal(sig, SIG_IGN);
    term++;
}

static void
onquit(int sig)
{
    (void) signal(sig, SIG_IGN);
    term++;
}

static void
onhup(int sig)
{
    (void) signal(sig, SIG_IGN);
    term++;
}

#ifdef notdef
static void
onterm(int sig)
{
    (void) signal(SIGTERM, SIG_IGN);
    term++;
}
#endif

int
main(int argc, char **argv)
{
    char *cp;

    if (argc < 2)
        usage();

    vfile = stdout;
    tfile = NULL;
    usefile =  magtape;
    argv[argc] = 0;
    argv++;
    for (cp = *argv++; *cp; cp++)
        switch(*cp) {

        case 'f':
            if (*argv == 0) {
                fprintf(stderr,
            "tar: tapefile must be specified with 'f' option\n");
                usage();
            }
            usefile = *argv++;
            fflag++;
            break;

        case 'c':
            cflag++;
            rflag++;
            break;

        case 'o':
            oflag++;
            break;

        case 'p':
            pflag++;
            break;

        case 'u':
        {
            int temporary_descriptor = mkstemp(tname);

            if (temporary_descriptor < 0 ||
                (tfile = fdopen(temporary_descriptor, "w+")) == NULL) {
                int saved_errno = errno;

                if (temporary_descriptor >= 0)
                    (void)close(temporary_descriptor);
                errno = saved_errno;
                fprintf(stderr,
                 "tar: cannot create temporary file (%s)\n",
                 tname);
                done(1);
            }
            fprintf(tfile, "!!!!!/!/!/!/!/!/!/! 000\n");
        }
            /*FALL THRU*/

        case 'r':
            rflag++;
            break;

        case 'v':
            vflag++;
            break;

        case 'w':
            wflag++;
            break;

        case 'x':
            xflag++;
            break;

        case 't':
            tflag++;
            break;

        case 'm':
            mflag++;
            break;

        case '-':
            break;

        case '0':
        case '1':
        case '4':
        case '5':
        case '7':
        case '8':
            magtape[8] = *cp;
            usefile = magtape;
            break;

        case 'b':
        {
            char *end;
            long parsed_block_count;

            if (*argv == 0) {
                fprintf(stderr,
            "tar: blocksize must be specified with 'b' option\n");
                usage();
            }
            errno = 0;
            parsed_block_count = strtol(*argv, &end, 10);
            if (errno != 0 || end == *argv || *end != '\0' ||
                parsed_block_count <= 0 || parsed_block_count > INT_MAX) {
                fprintf(stderr,
                    "tar: invalid blocksize \"%s\"\n", *argv);
                done(1);
            }
            nblock = (int)parsed_block_count;
            argv++;
            break;
        }

        case 'l':
            prtlinkerr++;
            break;

        case 'h':
            hflag++;
            break;

        case 'i':
            iflag++;
            break;

        case 'B':
            Bflag++;
            break;

        case 'F':
            Fflag++;
            break;

        case 'O':
            Oflag++;
            break;

        /*
         * The tree carries no gzip, so -z names the same LZW filter as
         * -Z: usr.bin/compress, the 4.3BSD codec whose output is what
         * uncompress(1) and zcat(1) read.
         */
        case 'z':
        case 'Z':
            zflag++;
            break;

        default:
            fprintf(stderr, "tar: %c: unknown option\n", *cp);
            usage();
        }

    if (!rflag && !xflag && !tflag)
        usage();
    if (rflag) {
        if (cflag && tfile != NULL)
            usage();
        if (signal(SIGINT, SIG_IGN) != SIG_IGN)
            (void) signal(SIGINT, onintr);
        if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
            (void) signal(SIGHUP, onhup);
        if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
            (void) signal(SIGQUIT, onquit);
#ifdef notdef
        if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
            (void) signal(SIGTERM, onterm);
#endif
        mt = openmt(usefile, 1);
        dorep(argv);
        done(operation_failed ? 1 : 0);
    }
    mt = openmt(usefile, 0);
    if (xflag)
        doxtract(argv);
    else
        dotable(argv);
    done(operation_failed ? 1 : 0);
}

static void
usage(void)
{
    fprintf(stderr,
"tar: usage: tar -{txru}[cvfblmhopwBiOzZ] [tapefile] [blocksize] file1 file2...\n");
    done(1);
}

static int
openmt(const char *tape, int writing)
{
    if (strcmp(tape, "-") == 0) {
        /*
         * Read from standard input or write to standard output.
         */
        if (writing) {
            if (cflag == 0) {
                fprintf(stderr,
             "tar: can only create standard output archives\n");
                done(1);
            }
            vfile = stderr;
            setlinebuf(vfile);
            mt = dup(1);
        } else {
            mt = dup(0);
            Bflag++;
        }
    } else {
        /*
         * Use file or tape on local machine.
         */
        if (writing) {
            if (cflag)
                mt = open(tape, O_RDWR|O_CREAT|O_TRUNC, 0666);
            else
                mt = open(tape, O_RDWR);
        } else
            mt = open(tape, O_RDONLY);
        if (mt < 0) {
            fprintf(stderr, "tar: ");
            perror(tape);
            done(1);
        }
    }
    if (zflag)
        zfilter(writing);
    return(mt);
}

/*
 * Replace mt with one end of a pipe to a COMPRESS child holding the other
 * end and the archive itself, so the codec stays a separate process with
 * its own 96-kbyte window. Creating, the child compresses what tar writes;
 * reading, it decompresses what tar reads.
 */
static void
zfilter(int writing)
{
    int fd[2];

    if (mt < 0)
        archive_error("compression filter has no archive descriptor");
    if (pipe(fd) < 0) {
        fprintf(stderr, "tar: ");
        perror("pipe");
        done(1);
    }
    if ((zpid = fork()) < 0) {
        fprintf(stderr, "tar: ");
        perror("fork");
        done(1);
    }
    if (zpid == 0) {
        if (writing) {
            dup2(fd[0], 0);
            dup2(mt, 1);
        } else {
            dup2(mt, 0);
            dup2(fd[1], 1);
        }
        close(fd[0]);
        close(fd[1]);
        close(mt);
        if (writing)
            execl(COMPRESS, "compress", (char *) 0);
        else
            execl(COMPRESS, "compress", "-d", (char *) 0);
        fprintf(stderr, "tar: ");
        perror(COMPRESS);
        _exit(1);
    }
    close(mt);
    zreading = !writing;
    if (writing) {
        close(fd[0]);
        mt = fd[1];
    } else {
        close(fd[1]);
        mt = fd[0];
        /*
         * A pipe returns short reads whatever the block size, so the
         * archive is reblocked on the way in as it is for a -B stream.
         */
        Bflag++;
    }
}

/*
 * Close the pipe so the filter sees end of file, then wait for it. Exiting
 * without the wait loses whatever the codec still holds buffered, which
 * truncates the compressed archive at its last full block.
 */
static int
zreap(void)
{
    int child_status = 0;
    int result = 0;
    pid_t waited;

    if (mt >= 0) {
        if (zpid > 0 && zreading) {
            char discarded[TBLOCK];
            ssize_t received;

            do {
                received = read(mt, discarded, sizeof(discarded));
            } while (received > 0 || (received < 0 && errno == EINTR));
            if (received < 0)
                result = 2;
        }
        if (close(mt) < 0)
            result = 2;
        mt = -1;
    }
    if (zpid > 0) {
        do {
            waited = waitpid(zpid, &child_status, 0);
        } while (waited < 0 && errno == EINTR);
        zpid = -1;
        if (waited < 0 || !WIFEXITED(child_status) ||
            WEXITSTATUS(child_status) != 0)
            return 2;
    }
    return result;
}

static char *
tarcwd(char *buf)
{
    if (getcwd(buf, MAXPATHLEN) == NULL) {
        fprintf(stderr, "tar: cannot determine current directory: ");
        perror("");
        done(1);
    }
    return (buf);
}

static void
dorep(char **argv)
{
    char creation_path[PATHSIZ];
    char *leaf_name;
    char wdir[MAXPATHLEN];
    char parent[MAXPATHLEN];
    int archive_root_descriptor;

    if (!cflag) {
        getdir();
        do {
            passtape();
            if (term)
                done(0);
            getdir();
        } while (!endtape());
        backtape();
        if (tfile != NULL) {
            if (fflush(tfile) == EOF)
                done(2);
            if (fseek(tfile, 0, SEEK_SET) != 0)
                done(2);
        }
    }

    (void) tarcwd(wdir);
    archive_root_descriptor = open(".", O_RDONLY | O_NONBLOCK);
    if (archive_root_descriptor < 0)
        archive_path_error("open archive root", ".");
    while (*argv && ! term) {
        size_t operand_length;

        if (!strcmp(*argv, "-C") && argv[1]) {
            argv++;
            if (chdir(*argv) < 0) {
                fprintf(stderr, "tar: can't change directories to ");
                perror(*argv);
                operation_failed = 1;
            } else {
                if (close(archive_root_descriptor) < 0)
                    archive_path_error("close archive root", ".");
                (void) tarcwd(wdir);
                archive_root_descriptor = open(".", O_RDONLY | O_NONBLOCK);
                if (archive_root_descriptor < 0)
                    archive_path_error("open archive root", ".");
            }
            argv++;
            continue;
        }
        operand_length = strlen(*argv);
        if (operand_length >= sizeof(creation_path))
            archive_error("archive input path exceeds the format limit");
        memcpy(creation_path, *argv, operand_length + 1);
        while (operand_length > 1 &&
            creation_path[operand_length - 1] == '/')
            creation_path[--operand_length] = '\0';
        validate_archive_path(creation_path);
        leaf_name = enter_parent_directories(archive_root_descriptor,
            creation_path, 0, hflag);
        (void)tarcwd(parent);
        putfile(creation_path, leaf_name, parent);
        argv++;
        if (fchdir(archive_root_descriptor) < 0)
            archive_path_error("restore archive root", wdir);
    }
    if (close(archive_root_descriptor) < 0)
        archive_path_error("close archive root", ".");
    putempty();
    putempty();
    flushtape();
    if (prtlinkerr == 0)
        return;
    for (; ihead != NULL; ihead = ihead->nextp) {
        if (ihead->count == 0)
            continue;
        fprintf(stderr, "tar: missing links to %s\n", ihead->pathname);
    }
}

static int
endtape(void)
{
    return (dblock.dbuf.name[0] == '\0');
}

static void
getdir(void)
{
    struct stat *sp;
    char *cp;
    unsigned long mode_value;
    unsigned long uid_value;
    unsigned long gid_value;
    unsigned long size_value;
    unsigned long time_value;
    unsigned long checksum_value;
    int i;
top:
    readtape((char *)&dblock);
    if (dblock.dbuf.name[0] == '\0')
        return;
    sp = &stbuf;
    mode_value = getoctal(dblock.dbuf.mode, sizeof(dblock.dbuf.mode));
    uid_value = getoctal(dblock.dbuf.uid, sizeof(dblock.dbuf.uid));
    gid_value = getoctal(dblock.dbuf.gid, sizeof(dblock.dbuf.gid));
    size_value = getoctal(dblock.dbuf.size, sizeof(dblock.dbuf.size));
    time_value = getoctal(dblock.dbuf.mtime, sizeof(dblock.dbuf.mtime));
    checksum_value = getoctal(dblock.dbuf.chksum,
        sizeof(dblock.dbuf.chksum));
    if ((unsigned long)(mode_t)mode_value != mode_value ||
        (unsigned long)(uid_t)uid_value != uid_value ||
        (unsigned long)(gid_t)gid_value != gid_value ||
        size_value > LONG_MAX || time_value > LONG_MAX ||
        checksum_value > INT_MAX)
        archive_error("archive numeric field exceeds the target type");
    sp->st_mode = (mode_t)mode_value;
    sp->st_uid = (uid_t)uid_value;
    sp->st_gid = (gid_t)gid_value;
    sp->st_size = (off_t)size_value;
    sp->st_mtime = (time_t)time_value;
    chksum = (int)checksum_value;
    if (chksum != (i = checksum())) {
        fprintf(stderr, "tar: directory checksum error (%d != %d)\n",
            chksum, i);
        if (iflag)
            goto top;
        done(2);
    }
    /*
     * Assemble the full path. ustar splits a path longer than NAMSIZ-1 at
     * a slash into prefix and name; neither field is terminated when it is
     * filled to its width, so both copies are bounded by the field size.
     */
    cp = curname;
    if (isustar() && dblock.dbuf.prefix[0] != '\0') {
        for (i = 0; i < (int) sizeof(dblock.dbuf.prefix); i++) {
            if (dblock.dbuf.prefix[i] == '\0')
                break;
            *cp++ = dblock.dbuf.prefix[i];
        }
        *cp++ = '/';
    }
    for (i = 0; i < (int) sizeof(dblock.dbuf.name); i++) {
        if (dblock.dbuf.name[i] == '\0')
            break;
        *cp++ = dblock.dbuf.name[i];
    }
    *cp = '\0';

    for (i = 0; i < (int) sizeof(dblock.dbuf.linkname); i++) {
        if (dblock.dbuf.linkname[i] == '\0')
            break;
        curlink[i] = dblock.dbuf.linkname[i];
    }
    curlink[i] = '\0';

    if (cp > curname && cp[-1] == '/') {
        cp[-1] = '\0';
        if (dblock.dbuf.linkflag == AREGTYPE ||
            dblock.dbuf.linkflag == REGTYPE)
            dblock.dbuf.linkflag = DIRTYPE;
    }
    validate_archive_path(curname);
    if (dblock.dbuf.linkflag != AREGTYPE &&
        dblock.dbuf.linkflag != REGTYPE &&
        dblock.dbuf.linkflag != LNKTYPE &&
        dblock.dbuf.linkflag != SYMTYPE &&
        dblock.dbuf.linkflag != DIRTYPE)
        archive_error("archive contains an unsupported file type");
    if (dblock.dbuf.linkflag != AREGTYPE &&
        dblock.dbuf.linkflag != REGTYPE && stbuf.st_size != 0)
        archive_error("non-regular archive member carries file data");

    if (tfile != NULL)
        fprintf(tfile, "%s %.12s\n", curname,
            dblock.dbuf.mtime);
}

/*
 * An ustar header carries "ustar\0" in the magic field at offset 257, where
 * a v7 header carries the zero bytes that follow a name shorter than
 * NAMSIZ. The magic alone decides which layout the tail holds.
 */
static int
isustar(void)
{
    return (strncmp(dblock.dbuf.magic, TMAGIC, TMAGLEN) == 0);
}

static void
passtape(void)
{
    off_t blocks;
    char *bufp;

    if (dblock.dbuf.linkflag != AREGTYPE &&
        dblock.dbuf.linkflag != REGTYPE)
        return;
    blocks = stbuf.st_size / TBLOCK;
    if (stbuf.st_size % TBLOCK != 0)
        blocks++;

    while (blocks-- > 0)
        (void) readtbuf(&bufp, TBLOCK);
}

static char *
getmem(size_t size)
{
    char *p = malloc(size);

    if (p == NULL && freemem) {
        fprintf(stderr,
            "tar: out of memory, link and directory modtime info lost\n");
        freemem = 0;
    }
    return (p);
}

static void
putfile(char *longname, char *shortname, const char *parent)
{
    int infile = 0;
    off_t blocks;
    char buf[TBLOCK];
    char *bigbuf;
    char *cp;
    struct direct *dp;
    DIR *dirp;
    int i;
#ifndef __APPLE__
    long l;
#endif
    char newparent[PATHSIZ];
    size_t maxread;
    int hint;       /* amount to write to get "in sync" */

    validate_archive_path(longname);
    if (!hflag)
        i = lstat(shortname, &stbuf);
    else
        i = stat(shortname, &stbuf);
    if (i < 0) {
        fprintf(stderr, "tar: ");
        perror(longname);
        operation_failed = 1;
        return;
    }
    if (tfile != NULL && checkupdate(longname) == 0)
        return;
    if (checkw('r', longname) == 0)
        return;
    if (Fflag && checkf(shortname, stbuf.st_mode, Fflag) == 0)
        return;

    switch (stbuf.st_mode & S_IFMT) {
    case S_IFDIR:
    {
        size_t directory_length = strlen(longname);

        if (directory_length + 2 > sizeof(buf)) {
            fprintf(stderr, "tar: %s: file name too long\n", longname);
            operation_failed = 1;
            return;
        }
        memcpy(buf, longname, directory_length);
        buf[directory_length] = '/';
        buf[directory_length + 1] = '\0';
        cp = buf + directory_length + 1;
        if (!oflag) {
            stbuf.st_size = 0;
            tomodes(&stbuf);
            if (putheader(buf, DIRTYPE) == 0)
                return;
        }
        if (snprintf(newparent, sizeof(newparent), "%s/%s", parent,
            shortname) >= (int)sizeof(newparent)) {
            fprintf(stderr, "tar: %s: parent path too long\n", longname);
            operation_failed = 1;
            return;
        }
        open_verified_directory(shortname, 0, 1, 0, hflag);
        if ((dirp = opendir(".")) == NULL) {
            fprintf(stderr, "tar: %s: directory read error\n",
                longname);
            operation_failed = 1;
            if (chdir(parent) < 0) {
                fprintf(stderr, "tar: cannot change back?: ");
                perror(parent);
            }
            return;
        }
        while ((dp = readdir(dirp)) != NULL && !term) {
            if (dp->d_ino == 0)
                continue;
            if (!strcmp(".", dp->d_name) ||
                !strcmp("..", dp->d_name))
                continue;
            if ((size_t)(cp - buf) + strlen(dp->d_name) + 1 > sizeof(buf)) {
                fprintf(stderr, "tar: %s/%s: file name too long\n",
                    longname, dp->d_name);
                operation_failed = 1;
                continue;
            }
            memcpy(cp, dp->d_name, strlen(dp->d_name) + 1);
#ifdef __APPLE__
            /*
             * The stream stays open across the recursion: a telldir
             * cookie is only good on the stream that issued it there,
             * and a descriptor per directory level is nothing on a host.
             */
            putfile(buf, cp, newparent);
#else
            l = telldir(dirp);
            closedir(dirp);
            putfile(buf, cp, newparent);
            dirp = opendir(".");
            if (dirp == NULL) {
                fprintf(stderr, "tar: %s: directory read error\n",
                    longname);
                operation_failed = 1;
                break;
            }
            seekdir(dirp, l);
#endif
        }
        if (dirp != NULL)
            closedir(dirp);
        if (chdir(parent) < 0) {
            fprintf(stderr, "tar: cannot change back?: ");
            perror(parent);
            operation_failed = 1;
        }
        break;
    }

    case S_IFLNK:
    {
        ssize_t link_length;

        /*
         * The linkname field is NAMSIZ bytes and carries no terminator
         * when a target fills it, so a target of exactly NAMSIZ bytes is
         * storable; getdir() reads the field bounded by its width.
         */
        if (stbuf.st_size > NAMSIZ) {
            fprintf(stderr, "tar: %s: symbolic link too long\n",
                longname);
            operation_failed = 1;
            return;
        }
        stbuf.st_size = 0;
        tomodes(&stbuf);
        link_length = readlink(shortname, dblock.dbuf.linkname, NAMSIZ);
        if (link_length < 0) {
            fprintf(stderr, "tar: can't read symbolic link ");
            perror(longname);
            operation_failed = 1;
            return;
        }
        if (link_length < NAMSIZ)
            dblock.dbuf.linkname[link_length] = '\0';
        dblock.dbuf.linkflag = SYMTYPE;
        if (vflag)
            fprintf(vfile, "a %s symbolic link to %.*s\n",
                longname, NAMSIZ, dblock.dbuf.linkname);
        (void) putheader(longname, SYMTYPE);
        break;
    }

    case S_IFREG:
        if ((infile = open(shortname, 0)) < 0) {
            fprintf(stderr, "tar: ");
            perror(longname);
            operation_failed = 1;
            return;
        }
        tomodes(&stbuf);
        if (stbuf.st_nlink > 1) {
            struct linkbuf *lp;
            int found = 0;
            int plen;

            for (lp = ihead; lp != NULL; lp = lp->nextp)
                if (lp->inum == stbuf.st_ino &&
                    lp->devnum == stbuf.st_dev) {
                    found++;
                    break;
                }
            if (found && strlen(lp->pathname) <=
                sizeof(dblock.dbuf.linkname)) {
                memcpy(dblock.dbuf.linkname, lp->pathname,
                    strlen(lp->pathname));
                dblock.dbuf.linkflag = LNKTYPE;
                putoctal(dblock.dbuf.size,
                    sizeof(dblock.dbuf.size), 0);
                if (putheader(longname, LNKTYPE) == 0) {
                    close(infile);
                    return;
                }
                if (vflag)
                    fprintf(vfile, "a %s link to %s\n",
                        longname, lp->pathname);
                lp->count--;
                close(infile);
                return;
            }
            /*
             * The node joins the list only once its path is in hand, so a
             * refused path allocation leaves no node whose pathname a
             * later LNKTYPE entry or the missing-links report would read.
             * A refused node costs the identity, which getmem() reports.
             */
            if (!found) {
                plen = (int)strlen(longname) + 1;
                lp = (struct linkbuf *)getmem(sizeof(*lp));
                if (lp != NULL) {
                    lp->pathname = getmem((size_t)plen);
                    if (lp->pathname == NULL) {
                        free(lp);
                    } else {
                        lp->nextp = ihead;
                        ihead = lp;
                        lp->inum = stbuf.st_ino;
                        lp->devnum = stbuf.st_dev;
                        lp->count = (int)stbuf.st_nlink - 1;
                        memcpy(lp->pathname, longname, (size_t)plen);
                    }
                }
            }
        }
        blocks = stbuf.st_size / TBLOCK;
        if (stbuf.st_size % TBLOCK != 0)
            blocks++;
        if (vflag)
            fprintf(vfile, "a %s %ld blocks\n", longname, (long)blocks);
        if ((hint = putheader(longname, Oflag ? AREGTYPE : REGTYPE)) == 0) {
            close(infile);
            return;
        }
        maxread = (size_t)nblock * TBLOCK;
        if (stbuf.st_blksize > (long)maxread) {
            if (stbuf.st_blksize > NBLOCK * TBLOCK)
                maxread = NBLOCK * TBLOCK;
            else
                maxread = (size_t)stbuf.st_blksize;
        }
        maxread -= maxread % TBLOCK;
        if (maxread < TBLOCK)
            maxread = TBLOCK;
        if ((bigbuf = malloc(maxread)) == NULL) {
            maxread = TBLOCK;
            bigbuf = buf;
        }

        for (;;) {
            size_t requested = (size_t)hint * TBLOCK < maxread ?
                (size_t)hint * TBLOCK : maxread;
            ssize_t received = read(infile, bigbuf, requested);
            int nblks;

            if (received <= 0) {
                i = received < 0 ? -1 : 0;
                break;
            }
            if (blocks <= 0) {
                i = (int)received;
                break;
            }
            nblks = (int)(((size_t)received - 1) / TBLOCK) + 1;
            if (blocks < nblks)
                nblks = (int)blocks;
            /*
             * The last block of a file that is not a multiple of TBLOCK
             * is written whole, so the bytes past the file's end have to
             * be cleared: bigbuf comes from malloc, and writing it as it
             * stands puts heap contents into the archive and makes two
             * runs over the same tree produce different bytes.
             */
            if (received % TBLOCK != 0)
                memset(bigbuf + received, 0,
                    TBLOCK - (size_t)(received % TBLOCK));
            hint = writetbuf(bigbuf, nblks);
            blocks -= nblks;
        }
        close(infile);
        if (bigbuf != buf)
            free(bigbuf);
        if (i < 0) {
            fprintf(stderr, "tar: Read error on ");
            perror(longname);
            operation_failed = 1;
        } else if (blocks != 0 || i != 0) {
            fprintf(stderr, "tar: %s: file changed size\n",
                longname);
            operation_failed = 1;
        }
        while (--blocks >=  0)
            putempty();
        break;

    default:
        fprintf(stderr, "tar: %s is not a file. Not dumped\n",
            longname);
        operation_failed = 1;
        break;
    }
}

static void
doxtract(char **argv)
{
    int selection;

    extraction_root_descriptor = open(".", O_RDONLY);
    if (extraction_root_descriptor < 0)
        archive_path_error("open extraction root", ".");

    for (;;) {
        char *leaf_name;
        int output_descriptor = -1;

        selection = wantit(argv);
        if (selection == 0)
            continue;
        if (selection == -1)
            break;      /* end of tape */
        if (checkw('x', curname) == 0) {
            passtape();
            continue;
        }
        if (Fflag) {
            char *s;

            if ((s = strrchr(curname, '/')) == NULL)
                s = curname;
            else
                s++;
            if (checkf(s, stbuf.st_mode, Fflag) == 0) {
                passtape();
                continue;
            }
        }
        leaf_name = enter_parent_directories(extraction_root_descriptor,
            curname, 1, 0);
        if (dblock.dbuf.linkflag == DIRTYPE) {
            size_t path_length = strlen(curname);

            open_verified_directory(leaf_name, stbuf.st_mode & 0777, 0, 1,
                0);
            if (mflag == 0) {
                if (path_length >= sizeof(curname) - 1) {
                    archive_error("directory path exceeds timestamp stack");
                    return;
                }
                curname[path_length] = '/';
                curname[path_length + 1] = '\0';
                dodirtimes(curname);
                curname[path_length] = '\0';
            }
            continue;
        }
        if (dblock.dbuf.linkflag == SYMTYPE) {
            struct stat status;

            validate_symlink_target(curname, curlink);
            if (lstat(leaf_name, &status) == 0 || errno != ENOENT)
                archive_error("refusing to replace an existing output path");
            if (symlink(curlink, leaf_name) < 0)
                archive_path_error("create symbolic link", curname);
            if (vflag)
                fprintf(vfile, "x %s symbolic link to %s\n",
                    curname, curlink);
            continue;
        }
        if (dblock.dbuf.linkflag == LNKTYPE) {
            int source_descriptor;
            struct stat source_status;
            struct stat output_status;

            validate_archive_path(curlink);
            leaf_name = enter_parent_directories(extraction_root_descriptor,
                curlink, 0, 0);
            source_descriptor = open_verified_regular(leaf_name, 1);
            if (fstat(source_descriptor, &source_status) < 0)
                archive_path_error("fstat hard-link source", curlink);
            if (!was_extracted_file(&source_status))
                archive_error("hard-link source was not created by this extraction");
            if ((source_status.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) != 0)
                archive_error("hard-link source carries special mode bits");
            if (close(source_descriptor) < 0)
                archive_path_error("close hard-link source", curlink);
            leaf_name = enter_parent_directories(extraction_root_descriptor,
                curname, 1, 0);
            if (lstat(leaf_name, &output_status) == 0 || errno != ENOENT)
                archive_error("refusing to replace an existing output path");
            if (fchdir(extraction_root_descriptor) < 0)
                archive_path_error("restore extraction root", ".");
            if (link(curlink, curname) < 0) {
                archive_path_error("create hard link", curname);
            }
            if (vflag)
                fprintf(vfile, "%s linked to %s\n",
                    curname, curlink);
            continue;
        }
        output_descriptor = create_output_file(leaf_name, stbuf.st_mode);
        if (vflag)
            fprintf(vfile, "x %s, %ld bytes, %ld tape blocks\n",
                curname, (long)stbuf.st_size,
                (long)((stbuf.st_size / TBLOCK) +
                (stbuf.st_size % TBLOCK != 0)));
        {
            off_t remaining = stbuf.st_size;
            off_t blocks = remaining / TBLOCK;

            if (remaining % TBLOCK != 0)
                blocks++;

            while (blocks > 0) {
                char *buffer;
                size_t requested = blocks < NBLOCK ?
                    (size_t)blocks * TBLOCK : (size_t)NBLOCK * TBLOCK;
                size_t received = readtbuf(&buffer, requested);
                size_t payload = remaining < (off_t)received ?
                    (size_t)remaining : received;

                if (received == 0 || received % TBLOCK != 0)
                    archive_error("truncated file data");
                if (write_all(output_descriptor, buffer, payload) < 0)
                    archive_path_error("write output", curname);
                remaining -= (off_t)payload;
                blocks -= (off_t)(received / TBLOCK);
            }
        }
        if (pflag && fchmod(output_descriptor, stbuf.st_mode & 0777) < 0)
            archive_path_error("set output mode", curname);
        if (mflag == 0)
            setimes(curname, stbuf.st_mtime);
        record_extracted_file(output_descriptor);
        if (close(output_descriptor) < 0)
            archive_path_error("close output", curname);
    }
    if (mflag == 0) {
        curname[0] = '\0'; /* process the whole stack */
        dodirtimes(curname);
    }
    if (close(extraction_root_descriptor) < 0)
        archive_path_error("close extraction root", ".");
    extraction_root_descriptor = -1;
    free_extracted_files();
}

static void
dotable(char **argv)
{
    int i;

    for (;;) {
        if ((i = wantit(argv)) == 0)
            continue;
        if (i == -1)
            break;      /* end of tape */
        if (vflag)
            longt(&stbuf);
        printf("%s", curname);
        if (dblock.dbuf.linkflag == LNKTYPE)
            printf(" linked to %s", curlink);
        if (dblock.dbuf.linkflag == SYMTYPE)
            printf(" symbolic link to %s", curlink);
        printf("\n");
        passtape();
    }
}

static void
putempty(void)
{
    char buf[TBLOCK];

    memset(buf, 0, sizeof(buf));
    (void) writetape(buf);
}

static void
longt(const struct stat *st)
{
    char *cp;
    pmode(st);
    printf("%3d/%1d", st->st_uid, st->st_gid);
    printf("%7ld", (long)st->st_size);
    cp = ctime(&st->st_mtime);
    printf(" %-12.12s %-4.4s ", cp+4, cp+20);
}

#define SUID    04000
#define SGID    02000
#define ROWN    0400
#define WOWN    0200
#define XOWN    0100
#define RGRP    040
#define WGRP    020
#define XGRP    010
#define ROTH    04
#define WOTH    02
#define XOTH    01
#define STXT    01000
static const int m1[] = { 1, ROWN, 'r', '-' };
static const int m2[] = { 1, WOWN, 'w', '-' };
static const int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
static const int m4[] = { 1, RGRP, 'r', '-' };
static const int m5[] = { 1, WGRP, 'w', '-' };
static const int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
static const int m7[] = { 1, ROTH, 'r', '-' };
static const int m8[] = { 1, WOTH, 'w', '-' };
static const int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

static const int * const m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

static void
pmode(const struct stat *st)
{
    const int * const *mp;

    for (mp = &m[0]; mp < &m[9];)
        selectbits(*mp++, st);
}

static void
selectbits(const int *pairp, const struct stat *st)
{
    int n;
    const int *ap;

    ap = pairp;
    n = *ap++;
    while (--n >= 0 && (st->st_mode & (mode_t)*ap++) == 0)
        ap++;
    putchar(*ap);
}

static _Noreturn void
archive_error(const char *message)
{
    fprintf(stderr, "tar: %s\n", message);
    done(2);
}

static _Noreturn void
archive_path_error(const char *operation, const char *path)
{
    int saved_errno = errno;

    fprintf(stderr, "tar: %s %s: ", operation, path);
    errno = saved_errno;
    perror("");
    done(2);
}

static void
validate_archive_path(const char *name)
{
    const char *component = name;
    const char *cursor;

    if (name[0] == '\0' || name[0] == '/' ||
        strlen(name) >= TAR_PATH_LIMIT)
        archive_error("archive contains an unsafe path");
    for (cursor = name;; cursor++) {
        size_t component_length;
        unsigned char byte = (unsigned char)*cursor;

        if (byte < 0x20U || (byte >= 0x7fU && byte <= 0x9fU)) {
            if (byte == '\0')
                break;
            archive_error("archive path contains a control byte");
        }
        if (*cursor != '/')
            continue;
        component_length = (size_t)(cursor - component);
        if (component_length == 0 ||
            (component_length == 1 && component[0] == '.') ||
            (component_length == 2 && component[0] == '.' &&
            component[1] == '.'))
            archive_error("archive contains an unsafe path component");
        component = cursor + 1;
    }
    if (cursor == component ||
        (cursor - component == 1 && component[0] == '.') ||
        (cursor - component == 2 && component[0] == '.' &&
        component[1] == '.'))
        archive_error("archive contains an unsafe path component");
}

static void
validate_symlink_target(const char *member, const char *target)
{
    const char *cursor;
    const char *component = target;
    size_t depth = 0;
    int saw_named_component = 0;

    if (target[0] == '\0' || target[0] == '/')
        archive_error("archive contains an unsafe symbolic-link target");
    for (cursor = member; *cursor != '\0'; cursor++)
        if (*cursor == '/')
            depth++;
    for (cursor = target;; cursor++) {
        size_t length;
        unsigned char byte = (unsigned char)*cursor;

        if (byte < 0x20U || (byte >= 0x7fU && byte <= 0x9fU)) {
            if (byte == '\0')
                break;
            archive_error("symbolic-link target contains a control byte");
        }
        if (*cursor != '/' && *cursor != '\0')
            continue;
        length = (size_t)(cursor - component);
        if (length == 0)
            archive_error("symbolic-link target contains an empty component");
        if (length == 2 && component[0] == '.' && component[1] == '.') {
            if (saw_named_component)
                archive_error("symbolic-link target backtracks after a named component");
            if (depth == 0)
                archive_error("symbolic-link target escapes the extraction root");
            depth--;
        } else if (!(length == 1 && component[0] == '.')) {
            depth++;
            saw_named_component = 1;
        }
        if (*cursor == '\0')
            break;
        component = cursor + 1;
    }
}

static void
open_verified_directory(const char *name, mode_t creation_mode,
    int enter_directory, int create_missing, int follow_symlinks)
{
    struct stat status;
    dev_t path_device;
    ino_t path_inode;
    mode_t creation_mask = 0;
    int created = 0;
    int descriptor;

    if ((follow_symlinks ? stat(name, &status) : lstat(name, &status)) < 0) {
        if (errno != ENOENT || !create_missing)
            archive_path_error("lstat directory", name);
        creation_mask = umask(0);
        if (mkdir(name, S_IRWXU) < 0) {
            int saved_errno = errno;

            (void)umask(creation_mask);
            errno = saved_errno;
            archive_path_error("mkdir", name);
        }
        (void)umask(creation_mask);
        created = 1;
        if (lstat(name, &status) < 0)
            archive_path_error("lstat created directory", name);
    }
    if (!S_ISDIR(status.st_mode))
        archive_error("path contains a non-directory component");
    path_device = status.st_dev;
    path_inode = status.st_ino;
    descriptor = open(name, O_RDONLY | O_NONBLOCK);
    if (descriptor < 0)
        archive_path_error("open directory", name);
    if (fstat(descriptor, &status) < 0)
        archive_path_error("fstat directory", name);
    if (!S_ISDIR(status.st_mode) || status.st_dev != path_device ||
        status.st_ino != path_inode)
        archive_error("directory changed during traversal");
    if (created && fchmod(descriptor,
        ((creation_mode & 0777) & ~creation_mask) | S_IRWXU) < 0)
        archive_path_error("set created directory mode", name);
    if (enter_directory && fchdir(descriptor) < 0)
        archive_path_error("enter directory", name);
    if (close(descriptor) < 0)
        archive_path_error("close directory", name);
}

static char *
enter_parent_directories(int root_descriptor, char *path, int create_missing,
    int follow_symlinks)
{
    char *component = path;
    char *cursor;

    if (fchdir(root_descriptor) < 0)
        archive_path_error("restore extraction root", ".");
    for (cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        open_verified_directory(component, 0777, 1, create_missing,
            follow_symlinks);
        *cursor = '/';
        component = cursor + 1;
    }
    return component;
}

static int
open_verified_regular(const char *name, int nonblocking)
{
    struct stat path_status;
    struct stat descriptor_status;
    int flags = O_RDONLY;
    int descriptor;

    if (lstat(name, &path_status) < 0)
        archive_path_error("lstat regular file", name);
    if (!S_ISREG(path_status.st_mode))
        archive_error("hard-link source is not a regular file");
    if (nonblocking)
        flags |= O_NONBLOCK;
    descriptor = open(name, flags);
    if (descriptor < 0)
        archive_path_error("open regular file", name);
    if (fstat(descriptor, &descriptor_status) < 0)
        archive_path_error("fstat regular file", name);
    if (!S_ISREG(descriptor_status.st_mode) ||
        descriptor_status.st_dev != path_status.st_dev ||
        descriptor_status.st_ino != path_status.st_ino)
        archive_error("regular file changed during verification");
    return descriptor;
}

static int
create_output_file(const char *name, mode_t mode)
{
    int descriptor = open(name, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);

    if (descriptor < 0)
        archive_path_error("create new output", curname);
    return descriptor;
}

static int
write_all(int descriptor, const void *buffer, size_t byte_count)
{
    const char *position = buffer;

    while (byte_count > 0) {
        ssize_t written = write(descriptor, position, byte_count);

        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        position += written;
        byte_count -= (size_t)written;
    }
    return 0;
}

static void
record_extracted_file(int descriptor)
{
    struct extracted_identity *identity = malloc(sizeof(*identity));
    struct stat status;

    if (identity == NULL) {
        archive_error("out of memory recording an extracted file");
        return;
    }
    if (fstat(descriptor, &status) < 0)
        archive_path_error("fstat extracted file", curname);
    identity->inode = status.st_ino;
    identity->device = status.st_dev;
    identity->next = extracted_files;
    extracted_files = identity;
}

static int
was_extracted_file(const struct stat *status)
{
    const struct extracted_identity *identity;

    for (identity = extracted_files; identity != NULL;
        identity = identity->next)
        if (identity->inode == status->st_ino &&
            identity->device == status->st_dev)
            return 1;
    return 0;
}

static void
free_extracted_files(void)
{
    while (extracted_files != NULL) {
        struct extracted_identity *next = extracted_files->next;

        free(extracted_files);
        extracted_files = next;
    }
}

static void
tomodes(const struct stat *sp)
{
    char *cp;

    for (cp = dblock.dummy; cp < &dblock.dummy[TBLOCK]; cp++)
        *cp = '\0';
    putoctal(dblock.dbuf.mode, sizeof(dblock.dbuf.mode), sp->st_mode & 07777);
    putoctal(dblock.dbuf.uid, sizeof(dblock.dbuf.uid), sp->st_uid);
    putoctal(dblock.dbuf.gid, sizeof(dblock.dbuf.gid), sp->st_gid);
    if (sp->st_size < 0 || sp->st_mtime < 0)
        archive_error("file metadata cannot be represented in octal");
    putoctal(dblock.dbuf.size, sizeof(dblock.dbuf.size),
        (unsigned long)sp->st_size);
    putoctal(dblock.dbuf.mtime, sizeof(dblock.dbuf.mtime),
        (unsigned long)sp->st_mtime);
}

/*
 * Split name across the ustar prefix and name fields, fill the ustar tail,
 * checksum the header and write it. ustar stores a path longer than
 * NAMSIZ-1 by splitting it at a slash so that at most NAMSIZ-1 bytes land
 * in name and at most TPFSZ in prefix; a single path component longer than
 * NAMSIZ-1 has no such split point and is refused, as is any path at all
 * over NAMSIZ-1 bytes under -O, which writes the v7 header. Returns the
 * writetape hint, or 0 when the name does not fit.
 */
static int
putheader(const char *name, char typeflag)
{
    size_t length = strlen(name);
    size_t split = 0;
    size_t index = 0;

    if (length >= NAMSIZ) {
        if (Oflag || length > TPFSZ + NAMSIZ) {
            fprintf(stderr, "tar: %s: file name too long\n", name);
            operation_failed = 1;
            return (0);
        }
        /*
         * Take the first slash that leaves a tail the name field holds,
         * so the prefix stays as short as the split allows. The search
         * starts at len-NAMSIZ, which leaves at most NAMSIZ-1 bytes in
         * name, and stops before the last byte: a directory arrives with
         * a trailing slash, and splitting there would leave name empty,
         * which endtape() reads as the end of the archive and which would
         * silently drop every entry that follows.
         */
        index = length - NAMSIZ;
        if (index < 1)
            index = 1;
        for (; index < length - 1; index++)
            if (index <= TPFSZ && name[index] == '/') {
                split = index;
                break;
            }
        if (split == 0) {
            fprintf(stderr, "tar: %s: file name too long\n", name);
            operation_failed = 1;
            return (0);
        }
        memcpy(dblock.dbuf.prefix, name, split);
        memcpy(dblock.dbuf.name, name + split + 1,
            length - split - 1);
    } else
        memcpy(dblock.dbuf.name, name, length);

    if (!Oflag) {
        memcpy(dblock.dbuf.magic, TMAGIC, TMAGLEN);
        memcpy(dblock.dbuf.version, TVERSION, TVERSLEN);
        dblock.dbuf.linkflag = typeflag;
        memcpy(dblock.dbuf.uname, uidname(stbuf.st_uid),
            strlen(uidname(stbuf.st_uid)));
        memcpy(dblock.dbuf.gname, gidname(stbuf.st_gid),
            strlen(gidname(stbuf.st_gid)));
        putoctal(dblock.dbuf.devmajor, sizeof(dblock.dbuf.devmajor), 0);
        putoctal(dblock.dbuf.devminor, sizeof(dblock.dbuf.devminor), 0);
    }
    if (snprintf(dblock.dbuf.chksum, sizeof(dblock.dbuf.chksum),
        "%6o", checksum()) != 6)
        archive_error("header checksum exceeds its field");
    return (writetape((char *) &dblock));
}

/*
 * Name the owner and group for the ustar uname and gname fields, each
 * cached for the one id a run of putfile() repeats. An unknown id yields
 * the empty string, which POSIX reads as "use the numeric field".
 */
static const char *
uidname(uid_t uid)
{
    static uid_t last = (uid_t) -1;
    static char name[UGSZ];
    struct passwd *pw;

    if (uid != last) {
        last = uid;
        name[0] = '\0';
        if ((pw = getpwuid(uid)) != NULL) {
            size_t length = strlen(pw->pw_name);

            if (length >= sizeof(name))
                length = sizeof(name) - 1;
            memcpy(name, pw->pw_name, length);
            name[length] = '\0';
        }
    }
    return (name);
}

static const char *
gidname(gid_t gid)
{
    static gid_t last = (gid_t) -1;
    static char name[UGSZ];
    struct group *gr;

    if (gid != last) {
        last = gid;
        name[0] = '\0';
        if ((gr = getgrgid(gid)) != NULL) {
            size_t length = strlen(gr->gr_name);

            if (length >= sizeof(name))
                length = sizeof(name) - 1;
            memcpy(name, gr->gr_name, length);
            name[length] = '\0';
        }
    }
    return (name);
}

/*
 * Fill a header field with right-justified zero-padded octal in width-1
 * bytes and terminate it with a NUL in the last, the encoding GNU tar and
 * bsdtar write and every tar reader accepts. sprintf("%11lo ") into the
 * 12-byte size and mtime fields writes its terminator one byte past the
 * field and overwrites the first byte of the field that follows.
 */
static void
putoctal(char *field, size_t width, unsigned long value)
{
    size_t index;

    field[--width] = '\0';
    for (index = width; index > 0;) {
        index--;
        field[index] = (char)('0' + (int)(value & 7UL));
        value >>= 3;
    }
    if (value != 0)
        archive_error("file metadata exceeds an archive octal field");
}

/*
 * Read a header field of width bytes as octal. A field filled to its width
 * carries no terminator, so sscanf on the field in place runs into the
 * field that follows; copy it out with an explicit terminator first.
 */
static unsigned long
getoctal(const char *field, size_t width)
{
    unsigned long value = 0;
    size_t index;
    int saw_digit = 0;

    for (index = 0; index < width && field[index] == ' '; index++)
        ;
    for (; index < width; index++) {
        unsigned long digit;

        if (field[index] == '\0' || field[index] == ' ')
            break;
        if (field[index] < '0' || field[index] > '7')
            archive_error("archive contains a malformed octal field");
        digit = (unsigned long)(field[index] - '0');
        if (value > (ULONG_MAX - digit) / 8UL)
            archive_error("archive octal field overflows");
        value = value * 8UL + digit;
        saw_digit = 1;
    }
    if (!saw_digit)
        archive_error("archive contains an empty octal field");
    for (; index < width; index++)
        if (field[index] != '\0' && field[index] != ' ')
            archive_error("archive contains trailing octal-field data");
    return value;
}

static int
checksum(void)
{
    int i;
    char *cp;
    unsigned char *up;

    for (cp = dblock.dbuf.chksum;
         cp < &dblock.dbuf.chksum[sizeof(dblock.dbuf.chksum)]; cp++)
        *cp = ' ';
    i = 0;
    for (up = (unsigned char *) dblock.dummy;
         up < (unsigned char *) &dblock.dummy[TBLOCK]; up++)
        i += *up;
    return (i);
}

static int
checkw(int c, const char *name)
{
    if (!wflag)
        return (1);
    printf("%c ", c);
    if (vflag)
        longt(&stbuf);
    printf("%s: ", name);
    return (response() == 'y');
}

static int
response(void)
{
    int response_byte = getchar();
    int discarded_byte;

    if (response_byte == EOF || response_byte == '\n')
        return 'n';
    do {
        discarded_byte = getchar();
    } while (discarded_byte != '\n' && discarded_byte != EOF);
    return response_byte;
}

static int
checkf(const char *name, mode_t mode, int howmuch)
{
    size_t length;

    if ((mode & S_IFMT) == S_IFDIR){
        if ((strcmp(name, "SCCS")==0) || (strcmp(name, "RCS")==0))
            return(0);
        return(1);
    }
    length = strlen(name);
    if (length < 3)
        return (1);
    if (howmuch > 1 && name[length - 2] == '.' &&
        name[length - 1] == 'o')
        return (0);
    if (strcmp(name, "core") == 0 ||
        strcmp(name, "errs") == 0 ||
        (howmuch > 1 && strcmp(name, "a.out") == 0))
        return (0);
    /* SHOULD CHECK IF IT IS EXECUTABLE */
    return (1);
}

/* Is the current file a new file, or the newest one of the same name? */
static int
checkupdate(const char *arg)
{
    char line[PATHSIZ + 16];
    unsigned long newest = 0;
    int found = 0;
    size_t argument_length = strlen(arg);

    if (fseek(tfile, 0, SEEK_SET) != 0)
        done(2);
    while (fgets(line, sizeof(line), tfile) != NULL) {
        char *separator = strrchr(line, ' ');
        unsigned long archived_time;

        if (separator == NULL || (size_t)(separator - line) != argument_length ||
            memcmp(line, arg, argument_length) != 0)
            continue;
        archived_time = strtoul(separator + 1, NULL, 8);
        if (!found || archived_time > newest)
            newest = archived_time;
        found = 1;
    }
    if (ferror(tfile))
        done(2);
    return !found || stbuf.st_mtime > (time_t)newest;
}

static _Noreturn void
done(int n)
{
    int filter_result = zreap();

    if (n == 0 && filter_result != 0)
        n = filter_result;
    unlink(tname);
    exit(n);
}

/*
 * Do we want the next entry on the tape, i.e. is it selected?  If
 * not, skip over the entire entry.  Return -1 if reached end of tape.
 */
static int
wantit(char **argv)
{
    char **cp;

    getdir();
    if (endtape())
        return (-1);
    if (*argv == 0)
        return (1);
    for (cp = argv; *cp; cp++)
        if (prefix(*cp, curname))
            return (1);
    passtape();
    return (0);
}

/*
 * Does s2 begin with the string s1, on a directory boundary?
 */
static int
prefix(const char *s1, const char *s2)
{
    while (*s1)
        if (*s1++ != *s2++)
            return (0);
    if (*s2)
        return (*s2 == '/');
    return (1);
}

static int
readtape(char *buffer)
{
    char *bufp;

    if (first == 0)
        getbuf();
    (void) readtbuf(&bufp, TBLOCK);
    memcpy(buffer, bufp, TBLOCK);
    return(TBLOCK);
}

static size_t
readtbuf(char **bufpp, size_t size)
{
    static int valid_records;
    ssize_t received;

    if (recno >= valid_records || first == 0) {
        received = bread(mt, (char *)tbuf, (size_t)TBLOCK * (size_t)nblock);
        if (received < 0)
            mterr("read", received, 3);
        if (received == 0)
            archive_error("unexpected end of archive");
        if (received % TBLOCK != 0)
            archive_error("archive ends in a partial record");
        valid_records = (int)(received / TBLOCK);
        if (first == 0 && valid_records != nblock)
            fprintf(stderr, "tar: blocksize = %d\n", valid_records);
        if (first == 0)
            nblock = valid_records;
        first = 1;
        recno = 0;
    }
    if (size > (size_t)(valid_records - recno) * TBLOCK)
        size = (size_t)(valid_records - recno) * TBLOCK;
    *bufpp = (char *)&tbuf[recno];
    recno += (int)(size / TBLOCK);
    return size;
}

static int
writetbuf(const char *buffer, int n)
{
    ssize_t written;
    size_t tape_bytes = (size_t)TBLOCK * (size_t)nblock;

    if (first == 0) {
        getbuf();
        first = 1;
    }
    if (recno >= nblock) {
        written = write(mt, (char *)tbuf, tape_bytes);
        if (written != (ssize_t)tape_bytes)
            mterr("write", written, 2);
        recno = 0;
    }

    /*
     *  Special case:  We have an empty tape buffer, and the
     *  user data size is at least the tape block size. Write complete
     *  blocks directly and retain only the residual in the tape buffer.
     */
    while (recno == 0 && n >= nblock) {
        written = write(mt, buffer, tape_bytes);
        if (written != (ssize_t)tape_bytes)
            mterr("write", written, 2);
        n -= nblock;
        buffer += (nblock * TBLOCK);
    }

    while (n-- > 0) {
        memcpy((char *)&tbuf[recno++], buffer, TBLOCK);
        buffer += TBLOCK;
        if (recno >= nblock) {
            written = write(mt, (char *)tbuf, tape_bytes);
            if (written != (ssize_t)tape_bytes)
                mterr("write", written, 2);
            recno = 0;
        }
    }

    /* Tell the user how much to write to get in sync */
    return (nblock - recno);
}

static void
backtape(void)
{
#ifndef __APPLE__
    static int mtdev = 1;
    static struct mtop mtop = {MTBSR, 1};
    struct mtget mtget;

    if (mtdev == 1)
        mtdev = ioctl(mt, MTIOCGET, (char *)&mtget);
    if (mtdev == 0) {
        if (ioctl(mt, MTIOCTOP, (char *)&mtop) < 0) {
            fprintf(stderr, "tar: tape backspace error: ");
            perror("");
            done(4);
        }
    } else
#endif
        lseek(mt, (daddr_t) -TBLOCK*nblock, 1);
    recno--;
}

static void
flushtape(void)
{
    size_t tape_bytes = (size_t)TBLOCK * (size_t)nblock;
    ssize_t written;

    written = write(mt, (char *)tbuf, tape_bytes);
    if (written != (ssize_t)tape_bytes)
        mterr("write", written, 2);
}

static void
mterr(const char *operation, ssize_t result, int exitcode)
{
    fprintf(stderr, "tar: tape %s error: ", operation);
    if (result < 0)
        perror("");
    else
        fprintf(stderr, "unexpected EOF\n");
    done(exitcode);
}

static ssize_t
bread(int fd, char *buf, size_t size)
{
    size_t count;
    ssize_t lastread;

    if (!Bflag)
        return (read(fd, buf, size));

    for (count = 0; count < size; count += (size_t)lastread) {
        lastread = read(fd, buf, size - count);
        if (lastread <= 0) {
            if (count > 0)
                return (ssize_t)count;
            return (lastread);
        }
        buf += lastread;
    }
    return (ssize_t)count;
}

static void
getbuf(void)
{
    if (nblock == 0) {
        long block_count;

        if (fstat(mt, &stbuf) < 0)
            archive_path_error("fstat archive", usefile);
        if ((stbuf.st_mode & S_IFMT) == S_IFCHR)
            nblock = NBLOCK;
        else {
            block_count = stbuf.st_blksize / TBLOCK;
            if (block_count <= 0)
                nblock = NBLOCK;
            else if (block_count > NBLOCK)
                nblock = NBLOCK;
            else
                nblock = (int)block_count;
        }
    }
    /*
     * One rp2040 process owns a single 144-kbyte USER_DATA_SIZE window for
     * text, data, bss and stack together, so the block buffer is capped at the
     * 20-block 10240-byte default however large the archive file's
     * st_blksize is and however large a -b argument asks for.
     */
    if (nblock > NBLOCK)
        nblock = NBLOCK;
    tbuf = (union hblock *)malloc((unsigned)nblock*TBLOCK);
    if (tbuf == NULL) {
        fprintf(stderr, "tar: blocksize %d too big, can't get memory\n",
            nblock);
        done(1);
    }
}

/*
 * Save this directory and its mtime on the stack, popping and setting
 * the mtimes of any stacked dirs which aren't parents of this one.
 * A null directory causes the entire stack to be unwound and set.
 *
 * Since all the elements of the directory "stack" share a common
 * prefix, we can make do with one string.  We keep only the current
 * directory path, with an associated array of mtime's, one for each
 * '/' in the path.  A negative mtime means no mtime.  The mtime's are
 * offset by one (first index 1, not 0) because calling this with a null
 * directory causes mtime[0] to be set.
 *
 * This stack algorithm is not guaranteed to work for tapes created
 * with the 'r' option, but the vast majority of tapes with
 * directories are not.  This avoids saving every directory record on
 * the tape and setting all the times at the end.
 */
static char dirstack[PATHSIZ];
#define NTIM (PATHSIZ/2+1)      /* a/b/c/d/... */
static time_t mtime[NTIM];

static void
dodirtimes(char *name)
{
    char *p = dirstack;
    char *q = name;
    int ndir = 0;
    char *savp;
    int savndir;

    /* Find common prefix */
    while (*p != '\0' && *p == *q) {
        if (*p++ == '/')
            ++ndir;
        q++;
    }

    savp = p;
    savndir = ndir;
    while (*p) {
        /*
         * Not a child: unwind the stack, setting the times.
         * The order we do this doesn't matter, so we go "forward."
         */
        if (*p++ == '/')
            if (mtime[++ndir] >= 0) {
                *--p = '\0';    /* zap the slash */
                setimes(dirstack, mtime[ndir]);
                *p++ = '/';
            }
    }
    p = savp;
    ndir = savndir;

    /* Push this one on the "stack" */
    while ((*p = *q++) != '\0') { /* append the rest of the new dir */
        if (*p++ == '/') {
            if (ndir >= NTIM - 1) {
                archive_error("directory nesting exceeds timestamp stack");
                return;
            }
            mtime[++ndir] = -1;
        }
    }
    mtime[ndir] = stbuf.st_mtime;   /* overwrite the last one */
}

static void
setimes(char *path, time_t modification_time)
{
    char *leaf_name;
    char *trailing_slash = NULL;
    struct stat status;
    struct timeval tv[2];

    if (path[0] == '\0')
        return;
    if (path[strlen(path) - 1] == '/') {
        trailing_slash = path + strlen(path) - 1;
        *trailing_slash = '\0';
    }
    if (extraction_root_descriptor >= 0) {
        leaf_name = enter_parent_directories(extraction_root_descriptor,
            path, 0, 0);
        if (lstat(leaf_name, &status) < 0)
            archive_path_error("lstat metadata target", path);
        if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
            archive_error("metadata target changed type");
    } else {
        leaf_name = path;
    }
    tv[0].tv_sec = time((time_t *) 0);
    tv[1].tv_sec = modification_time;
    tv[0].tv_usec = tv[1].tv_usec = 0;
    if (utimes(leaf_name, tv) < 0) {
        int saved_errno = errno;

        fprintf(stderr, "tar: cannot set modification time on %s: ", path);
        errno = saved_errno;
        perror("");
        operation_failed = 1;
    }
    if (trailing_slash != NULL)
        *trailing_slash = '/';
}
