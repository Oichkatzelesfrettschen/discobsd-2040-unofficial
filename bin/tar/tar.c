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

/*
 * The LZW codec is a separate program reached through a pipe rather than a
 * library linked in: usr.bin/compress carries 30 kbytes of bss for its
 * string table, and tar and the filter are separate processes with a
 * 96-kbyte window each, where linking the codec would have to fit both
 * tables and the block buffer in one.
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
#define min(a,b)  ((a) < (b) ? (a) : (b))
#define max(a,b)  ((a) > (b) ? (a) : (b))

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

struct linkbuf {
    ino_t   inum;
    dev_t   devnum;
    int count;
    char    pathname[PATHSIZ];
    struct  linkbuf *nextp;
};

union   hblock dblock;
union   hblock *tbuf;
struct  linkbuf *ihead;
struct  stat stbuf;

void     usage();
int      openmt(char *, int);
char    *tarcwd(char *);
void     dorep(char **);
int      endtape();
void     getdir();
void     passtape();
char    *getmem(int);
void     putfile(char *, char *, char *);
void     doxtract(char **);
void     dotable(char **);
void     putempty();
void     longt(struct stat *);
void     pmode(struct stat *);
void     selectbits(int *, struct stat *);
int      checkdir(char *);
void     tomodes(struct stat *);
void     putoctal(char *, int, unsigned long);
int      putheader(char *, int);
void     zfilter(int);
void     zreap();
int      isustar();
char    *uidname(uid_t);
char    *gidname(gid_t);
long     getoctal(char *, int);
int      checksum();
int      checkw(int, char *);
int      response();
int      checkf(char *, int, int);
int      checkupdate(char *);
void     done(int);
int      wantit(char **);
int      prefix(char *, char *);
daddr_t  lookup(char *);
daddr_t  bsrch(char *, int, daddr_t, daddr_t);
int      cmp(char *, char *, int);
int      readtape(char *);
int      readtbuf(char **, int);
int      writetbuf(char *, int);
void     backtape();
void     flushtape();
void     mterr(char *, int, int);
int      bread(int, char *, int);
void     getbuf();
void     dodirtimes(char *);
void     setimes(char *, time_t);

int rflag;
int xflag;
int vflag;
int tflag;
int cflag;
int mflag;
int fflag;
int iflag;
int oflag;
int pflag;
int wflag;
int hflag;
int Bflag;
int Fflag;
int Oflag;              /* write the v7 header instead of ustar */
int zflag;              /* pipe the archive through COMPRESS */

int zpid = -1;          /* the filter, while it runs */

/*
 * The full path of the header last read: the prefix field, a slash and the
 * name field for an ustar header, the name field alone for a v7 one. The
 * extract and table paths work from this rather than from dbuf.name, which
 * holds at most the trailing NAMSIZ-1 bytes of an ustar path.
 */
char curname[PATHSIZ];

/*
 * The link target of the header last read. The linkname field carries no
 * terminator when a target fills its 100 bytes, so reading it in place runs
 * into the magic field and hands symlink() and link() a target with "ustar"
 * appended.
 */
char curlink[NAMSIZ+1];

int mt;
int term;
int chksum;
int recno;
int first;
int prtlinkerr;
int freemem = 1;
int nblock = 0;

daddr_t low;
daddr_t high;
daddr_t bsrch();

FILE    *vfile;
FILE    *tfile;
char    tname[] = "/tmp/tarXXXXXX";
char    *usefile;
char    magtape[] = "/dev/rmt8";

void
onintr (sig)
    int sig;
{
    (void) signal(sig, SIG_IGN);
    term++;
}

void
onquit (sig)
    int sig;
{
    (void) signal(sig, SIG_IGN);
    term++;
}

void
onhup (sig)
    int sig;
{
    (void) signal(sig, SIG_IGN);
    term++;
}

#ifdef notdef
void
onterm (sig)
    int sig;
{
    (void) signal(SIGTERM, SIG_IGN);
    term++;
}
#endif

int
main(argc, argv)
int argc;
char    *argv[];
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
            mktemp(tname);
            if ((tfile = fopen(tname, "w")) == NULL) {
                fprintf(stderr,
                 "tar: cannot create temporary file (%s)\n",
                 tname);
                done(1);
            }
            fprintf(tfile, "!!!!!/!/!/!/!/!/!/! 000\n");
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
            if (*argv == 0) {
                fprintf(stderr,
            "tar: blocksize must be specified with 'b' option\n");
                usage();
            }
            nblock = atoi(*argv);
            if (nblock <= 0) {
                fprintf(stderr,
                    "tar: invalid blocksize \"%s\"\n", *argv);
                done(1);
            }
            argv++;
            break;

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
        done(0);
    }
    mt = openmt(usefile, 0);
    if (xflag)
        doxtract(argv);
    else
        dotable(argv);
    done(0);
}

void
usage()
{
    fprintf(stderr,
"tar: usage: tar -{txru}[cvfblmhopwBiOzZ] [tapefile] [blocksize] file1 file2...\n");
    done(1);
}

int
openmt(tape, writing)
    char *tape;
    int writing;
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
void
zfilter(writing)
    int writing;
{
    int fd[2];

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
void
zreap()
{
    int status, w;

    if (mt >= 0) {
        close(mt);
        mt = -1;
    }
    if (zpid > 0) {
        while ((w = wait(&status)) != zpid && w != -1)
            ;
        zpid = -1;
    }
}

char *
tarcwd(buf)
    char *buf;
{
    if (getwd(buf) == NULL) {
        fprintf(stderr, "tar: %s\n", buf);
        exit(1);
    }
    return (buf);
}

void
dorep(argv)
    char *argv[];
{
    register char *cp, *cp2;
    char wdir[MAXPATHLEN], tempdir[MAXPATHLEN], *parent;

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
            char buf[200];

            sprintf(buf,
"sort +0 -1 +1nr %s -o %s; awk '$1 != prev {print; prev=$1}' %s >%sX; mv %sX %s",
                tname, tname, tname, tname, tname, tname);
            fflush(tfile);
            system(buf);
            freopen(tname, "r", tfile);
            fstat(fileno(tfile), &stbuf);
            high = stbuf.st_size;
        }
    }

    (void) tarcwd(wdir);
    while (*argv && ! term) {
        cp2 = *argv;
        if (!strcmp(cp2, "-C") && argv[1]) {
            argv++;
            if (chdir(*argv) < 0) {
                fprintf(stderr, "tar: can't change directories to ");
                perror(*argv);
            } else
                (void) tarcwd(wdir);
            argv++;
            continue;
        }

        if (*argv[0] == '/'){
            parent = "";
        } else {
            parent = wdir;
        }

        for (cp = *argv; *cp; cp++)
            if (*cp == '/')
                cp2 = cp;
        if (cp2 != *argv) {
            *cp2 = '\0';
            if (chdir(*argv) < 0) {
                fprintf(stderr, "tar: can't change directories to ");
                perror(*argv);
                continue;
            }
            parent = tarcwd(tempdir);
            *cp2 = '/';
            cp2++;
        }
        putfile(*argv++, cp2, parent);
        if (chdir(wdir) < 0) {
            fprintf(stderr, "tar: cannot change back?: ");
            perror(wdir);
        }
    }
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

int
endtape()
{
    return (dblock.dbuf.name[0] == '\0');
}

void
getdir()
{
    register struct stat *sp;
    register char *cp;
    int i;
top:
    readtape((char *)&dblock);
    if (dblock.dbuf.name[0] == '\0')
        return;
    sp = &stbuf;
    sp->st_mode = getoctal(dblock.dbuf.mode, sizeof(dblock.dbuf.mode));
    sp->st_uid = getoctal(dblock.dbuf.uid, sizeof(dblock.dbuf.uid));
    sp->st_gid = getoctal(dblock.dbuf.gid, sizeof(dblock.dbuf.gid));
    sp->st_size = getoctal(dblock.dbuf.size, sizeof(dblock.dbuf.size));
    sp->st_mtime = getoctal(dblock.dbuf.mtime, sizeof(dblock.dbuf.mtime));
    chksum = getoctal(dblock.dbuf.chksum, sizeof(dblock.dbuf.chksum));
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

    /*
     * checkdir() and dodirtimes() recognize a directory by the trailing
     * slash the v7 header carries in the name. An ustar producer marks a
     * directory with typeflag DIRTYPE instead and need not add the slash.
     */
    if (dblock.dbuf.linkflag == DIRTYPE && cp > curname && cp[-1] != '/') {
        *cp++ = '/';
        *cp = '\0';
    }

    if (tfile != NULL)
        fprintf(tfile, "%s %.12s\n", curname,
            dblock.dbuf.mtime);
}

/*
 * An ustar header carries "ustar\0" in the magic field at offset 257, where
 * a v7 header carries the zero bytes that follow a name shorter than
 * NAMSIZ. The magic alone decides which layout the tail holds.
 */
int
isustar()
{
    return (strncmp(dblock.dbuf.magic, TMAGIC, TMAGLEN) == 0);
}

void
passtape()
{
    long blocks;
    char *bufp;

    if (dblock.dbuf.linkflag == LNKTYPE)
        return;
    blocks = stbuf.st_size;
    blocks += TBLOCK-1;
    blocks /= TBLOCK;

    while (blocks-- > 0)
        (void) readtbuf(&bufp, TBLOCK);
}

char *
getmem(size)
    int size;
{
    char *p = malloc((unsigned) size);

    if (p == NULL && freemem) {
        fprintf(stderr,
            "tar: out of memory, link and directory modtime info lost\n");
        freemem = 0;
    }
    return (p);
}

void
putfile(longname, shortname, parent)
    char *longname;
    char *shortname;
    char *parent;
{
    int infile = 0;
    long blocks;
    char buf[TBLOCK];
    char *bigbuf;
    register char *cp;
    struct direct *dp;
    DIR *dirp;
    register int i;
    long l;
    char newparent[PATHSIZ];
    int maxread;
    int hint;       /* amount to write to get "in sync" */

    if (!hflag)
        i = lstat(shortname, &stbuf);
    else
        i = stat(shortname, &stbuf);
    if (i < 0) {
        fprintf(stderr, "tar: ");
        perror(longname);
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
        for (i = 0, cp = buf; (*cp++ = longname[i++]) != '\0';)
            ;
        *--cp = '/';
        *++cp = 0  ;
        if (!oflag) {
            stbuf.st_size = 0;
            tomodes(&stbuf);
            if (putheader(buf, DIRTYPE) == 0)
                return;
        }
        snprintf(newparent, sizeof(newparent), "%s/%s", parent, shortname);
        if (chdir(shortname) < 0) {
            perror(shortname);
            return;
        }
        if ((dirp = opendir(".")) == NULL) {
            fprintf(stderr, "tar: %s: directory read error\n",
                longname);
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
            strcpy(cp, dp->d_name);
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
            seekdir(dirp, l);
#endif
        }
        closedir(dirp);
        if (chdir(parent) < 0) {
            fprintf(stderr, "tar: cannot change back?: ");
            perror(parent);
        }
        break;

    case S_IFLNK:
        /*
         * The linkname field is NAMSIZ bytes and carries no terminator
         * when a target fills it, so a target of exactly NAMSIZ bytes is
         * storable; getdir() reads the field bounded by its width.
         */
        if (stbuf.st_size > NAMSIZ) {
            fprintf(stderr, "tar: %s: symbolic link too long\n",
                longname);
            return;
        }
        stbuf.st_size = 0;
        tomodes(&stbuf);
        i = readlink(shortname, dblock.dbuf.linkname, NAMSIZ);
        if (i < 0) {
            fprintf(stderr, "tar: can't read symbolic link ");
            perror(longname);
            return;
        }
        if (i < NAMSIZ)
            dblock.dbuf.linkname[i] = '\0';
        dblock.dbuf.linkflag = SYMTYPE;
        if (vflag)
            fprintf(vfile, "a %s symbolic link to %.*s\n",
                longname, NAMSIZ, dblock.dbuf.linkname);
        (void) putheader(longname, SYMTYPE);
        break;

    case S_IFREG:
        if ((infile = open(shortname, 0)) < 0) {
            fprintf(stderr, "tar: ");
            perror(longname);
            return;
        }
        tomodes(&stbuf);
        if (stbuf.st_nlink > 1) {
            struct linkbuf *lp;
            int found = 0;

            for (lp = ihead; lp != NULL; lp = lp->nextp)
                if (lp->inum == stbuf.st_ino &&
                    lp->devnum == stbuf.st_dev) {
                    found++;
                    break;
                }
            if (found) {
                strncpy(dblock.dbuf.linkname, lp->pathname,
                    sizeof(dblock.dbuf.linkname));
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
            lp = (struct linkbuf *) getmem(sizeof(*lp));
            if (lp != NULL) {
                lp->nextp = ihead;
                ihead = lp;
                lp->inum = stbuf.st_ino;
                lp->devnum = stbuf.st_dev;
                lp->count = stbuf.st_nlink - 1;
                strlcpy(lp->pathname, longname, sizeof(lp->pathname));
            }
        }
        blocks = (stbuf.st_size + (TBLOCK-1)) / TBLOCK;
        if (vflag)
            fprintf(vfile, "a %s %ld blocks\n", longname, blocks);
        if ((hint = putheader(longname, Oflag ? AREGTYPE : REGTYPE)) == 0) {
            close(infile);
            return;
        }
        maxread = max(stbuf.st_blksize, (nblock * TBLOCK));
        if (maxread > NBLOCK * TBLOCK)
            maxread = NBLOCK * TBLOCK;
        maxread -= maxread % TBLOCK;
        if (maxread < TBLOCK)
            maxread = TBLOCK;
        if ((bigbuf = malloc((unsigned)maxread)) == 0) {
            maxread = TBLOCK;
            bigbuf = buf;
        }

        while ((i = read(infile, bigbuf, min((hint*TBLOCK), maxread))) > 0
          && blocks > 0) {
            register int nblks;

            nblks = ((i-1)/TBLOCK)+1;
            if (nblks > blocks)
                nblks = blocks;
            /*
             * The last block of a file that is not a multiple of TBLOCK
             * is written whole, so the bytes past the file's end have to
             * be cleared: bigbuf comes from malloc, and writing it as it
             * stands puts heap contents into the archive and makes two
             * runs over the same tree produce different bytes.
             */
            if (i % TBLOCK)
                bzero(bigbuf + i, TBLOCK - (i % TBLOCK));
            hint = writetbuf(bigbuf, nblks);
            blocks -= nblks;
        }
        close(infile);
        if (bigbuf != buf)
            free(bigbuf);
        if (i < 0) {
            fprintf(stderr, "tar: Read error on ");
            perror(longname);
        } else if (blocks != 0 || i != 0)
            fprintf(stderr, "tar: %s: file changed size\n",
                longname);
        while (--blocks >=  0)
            putempty();
        break;

    default:
        fprintf(stderr, "tar: %s is not a file. Not dumped\n",
            longname);
        break;
    }
}

void
doxtract(argv)
    char *argv[];
{
    long blocks, bytes;
    int ofile, i;

    for (;;) {
        if ((i = wantit(argv)) == 0)
            continue;
        if (i == -1)
            break;      /* end of tape */
        if (checkw('x', curname) == 0) {
            passtape();
            continue;
        }
        if (Fflag) {
            char *s;

            if ((s = rindex(curname, '/')) == 0)
                s = curname;
            else
                s++;
            if (checkf(s, stbuf.st_mode, Fflag) == 0) {
                passtape();
                continue;
            }
        }
        if (checkdir(curname)) {   /* have a directory */
            if (mflag == 0)
                dodirtimes(curname);
            continue;
        }
        if (dblock.dbuf.linkflag == SYMTYPE) {  /* symlink */
            /*
             * only unlink non directories or empty
             * directories
             */
            if (rmdir(curname) < 0) {
                if (errno == ENOTDIR)
                    unlink(curname);
            }
            if (symlink(curlink, curname)<0) {
                fprintf(stderr, "tar: %s: symbolic link failed: ",
                    curname);
                perror("");
                continue;
            }
            if (vflag)
                fprintf(vfile, "x %s symbolic link to %s\n",
                    curname, curlink);
#ifdef notdef
            /* ignore alien orders */
            chown(curname, stbuf.st_uid, stbuf.st_gid);
            if (mflag == 0)
                setimes(curname, stbuf.st_mtime);
            if (pflag)
                chmod(curname, stbuf.st_mode & 07777);
#endif
            continue;
        }
        if (dblock.dbuf.linkflag == LNKTYPE) {  /* regular link */
            /*
             * only unlink non directories or empty
             * directories
             */
            if (rmdir(curname) < 0) {
                if (errno == ENOTDIR)
                    unlink(curname);
            }
            if (link(curlink, curname) < 0) {
                fprintf(stderr, "tar: can't link %s to %s: ",
                    curname, curlink);
                perror("");
                continue;
            }
            if (vflag)
                fprintf(vfile, "%s linked to %s\n",
                    curname, curlink);
            continue;
        }
        if ((ofile = creat(curname,stbuf.st_mode&0xfff)) < 0) {
            fprintf(stderr, "tar: can't create %s: ",
                curname);
            perror("");
            passtape();
            continue;
        }
        chown(curname, stbuf.st_uid, stbuf.st_gid);
        blocks = ((bytes = stbuf.st_size) + TBLOCK-1)/TBLOCK;
        if (vflag)
            fprintf(vfile, "x %s, %ld bytes, %ld tape blocks\n",
                curname, bytes, blocks);
        for (; blocks > 0;) {
            register int nread;
            char    *bufp;
            register int nwant;

            nwant = NBLOCK*TBLOCK;
            if (nwant > (blocks*TBLOCK))
                nwant = (blocks*TBLOCK);
            nread = readtbuf(&bufp, nwant);
            if (write(ofile, bufp, (int)min(nread, bytes)) < 0) {
                fprintf(stderr,
                    "tar: %s: HELP - extract write error",
                    curname);
                perror("");
                done(2);
            }
            bytes -= nread;
            blocks -= (((nread-1)/TBLOCK)+1);
        }
        close(ofile);
        if (mflag == 0)
            setimes(curname, stbuf.st_mtime);
        if (pflag)
            chmod(curname, stbuf.st_mode & 07777);
    }
    if (mflag == 0) {
        curname[0] = '\0'; /* process the whole stack */
        dodirtimes(curname);
    }
}

void
dotable(argv)
    char *argv[];
{
    register int i;

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

void
putempty()
{
    char buf[TBLOCK];

    bzero(buf, sizeof (buf));
    (void) writetape(buf);
}

void
longt(st)
    register struct stat *st;
{
    register char *cp;
    char *ctime();

    pmode(st);
    printf("%3d/%1d", st->st_uid, st->st_gid);
    printf("%7ld", st->st_size);
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
int m1[] = { 1, ROWN, 'r', '-' };
int m2[] = { 1, WOWN, 'w', '-' };
int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
int m4[] = { 1, RGRP, 'r', '-' };
int m5[] = { 1, WGRP, 'w', '-' };
int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
int m7[] = { 1, ROTH, 'r', '-' };
int m8[] = { 1, WOTH, 'w', '-' };
int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

int *m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9};

void
pmode(st)
    register struct stat *st;
{
    register int **mp;

    for (mp = &m[0]; mp < &m[9];)
        selectbits(*mp++, st);
}

void
selectbits(pairp, st)
    int *pairp;
    struct stat *st;
{
    register int n, *ap;

    ap = pairp;
    n = *ap++;
    while (--n>=0 && (st->st_mode&*ap++)==0)
        ap++;
    putchar(*ap);
}

/*
 * Make all directories needed by `name'.  If `name' is itself
 * a directory on the tar tape (indicated by a trailing '/'),
 * return 1; else 0.
 */
int
checkdir(name)
    register char *name;
{
    register char *cp;

    /*
     * Quick check for existence of directory.
     */
    if ((cp = rindex(name, '/')) == 0)
        return (0);
    *cp = '\0';
    if (access(name, 0) == 0) { /* already exists */
        *cp = '/';
        return (cp[1] == '\0'); /* return (lastchar == '/') */
    }
    *cp = '/';

    /*
     * No luck, try to make all directories in path.
     */
    for (cp = name; *cp; cp++) {
        if (*cp != '/')
            continue;
        *cp = '\0';
        if (access(name, 0) < 0) {
            if (mkdir(name, 0777) < 0) {
                perror(name);
                *cp = '/';
                return (0);
            }
            chown(name, stbuf.st_uid, stbuf.st_gid);
            if (pflag && cp[1] == '\0') /* dir on the tape */
                chmod(name, stbuf.st_mode & 07777);
        }
        *cp = '/';
    }
    return (cp[-1]=='/');
}

void
tomodes(sp)
register struct stat *sp;
{
    register char *cp;

    for (cp = dblock.dummy; cp < &dblock.dummy[TBLOCK]; cp++)
        *cp = '\0';
    putoctal(dblock.dbuf.mode, sizeof(dblock.dbuf.mode), sp->st_mode & 07777);
    putoctal(dblock.dbuf.uid, sizeof(dblock.dbuf.uid), sp->st_uid);
    putoctal(dblock.dbuf.gid, sizeof(dblock.dbuf.gid), sp->st_gid);
    putoctal(dblock.dbuf.size, sizeof(dblock.dbuf.size), sp->st_size);
    putoctal(dblock.dbuf.mtime, sizeof(dblock.dbuf.mtime), sp->st_mtime);
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
int
putheader(name, typeflag)
    char *name;
    int typeflag;
{
    int len = strlen(name);
    int split = 0;
    register int i;

    if (len >= NAMSIZ) {
        if (Oflag || len > TPFSZ + NAMSIZ) {
            fprintf(stderr, "tar: %s: file name too long\n", name);
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
        i = len - NAMSIZ;
        if (i < 1)
            i = 1;
        for (; i < len - 1; i++)
            if (name[i] == '/' && i <= TPFSZ) {
                split = i;
                break;
            }
        if (split == 0) {
            fprintf(stderr, "tar: %s: file name too long\n", name);
            return (0);
        }
        memcpy(dblock.dbuf.prefix, name, split);
        memcpy(dblock.dbuf.name, name + split + 1, len - split - 1);
    } else
        memcpy(dblock.dbuf.name, name, len);

    if (!Oflag) {
        memcpy(dblock.dbuf.magic, TMAGIC, TMAGLEN);
        memcpy(dblock.dbuf.version, TVERSION, TVERSLEN);
        dblock.dbuf.linkflag = typeflag;
        strncpy(dblock.dbuf.uname, uidname(stbuf.st_uid), UGSZ);
        strncpy(dblock.dbuf.gname, gidname(stbuf.st_gid), UGSZ);
        putoctal(dblock.dbuf.devmajor, sizeof(dblock.dbuf.devmajor), 0);
        putoctal(dblock.dbuf.devminor, sizeof(dblock.dbuf.devminor), 0);
    }
    sprintf(dblock.dbuf.chksum, "%6o", checksum());
    return (writetape((char *) &dblock));
}

/*
 * Name the owner and group for the ustar uname and gname fields, each
 * cached for the one id a run of putfile() repeats. An unknown id yields
 * the empty string, which POSIX reads as "use the numeric field".
 */
char *
uidname(uid)
    uid_t uid;
{
    static uid_t last = (uid_t) -1;
    static char name[UGSZ];
    struct passwd *pw;

    if (uid != last) {
        last = uid;
        name[0] = '\0';
        if ((pw = getpwuid(uid)) != NULL)
            strncpy(name, pw->pw_name, sizeof(name) - 1);
    }
    return (name);
}

char *
gidname(gid)
    gid_t gid;
{
    static gid_t last = (gid_t) -1;
    static char name[UGSZ];
    struct group *gr;

    if (gid != last) {
        last = gid;
        name[0] = '\0';
        if ((gr = getgrgid(gid)) != NULL)
            strncpy(name, gr->gr_name, sizeof(name) - 1);
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
void
putoctal(field, width, value)
    char *field;
    int width;
    unsigned long value;
{
    register int i;

    field[--width] = '\0';
    for (i = width - 1; i >= 0; i--) {
        field[i] = (char) ('0' + (int) (value & 7));
        value >>= 3;
    }
}

/*
 * Read a header field of width bytes as octal. A field filled to its width
 * carries no terminator, so sscanf on the field in place runs into the
 * field that follows; copy it out with an explicit terminator first.
 */
long
getoctal(field, width)
    char *field;
    int width;
{
    char buf[24];
    register int i;

    if (width > (int) sizeof(buf) - 1)
        width = sizeof(buf) - 1;
    for (i = 0; i < width; i++)
        buf[i] = field[i];
    buf[width] = '\0';
    return (strtol(buf, (char **) 0, 8));
}

int
checksum()
{
    register int i;
    register char *cp;
    register unsigned char *up;

    for (cp = dblock.dbuf.chksum;
         cp < &dblock.dbuf.chksum[sizeof(dblock.dbuf.chksum)]; cp++)
        *cp = ' ';
    i = 0;
    for (up = (unsigned char *) dblock.dummy;
         up < (unsigned char *) &dblock.dummy[TBLOCK]; up++)
        i += *up;
    return (i);
}

int
checkw(c, name)
    int c;
    char *name;
{
    if (!wflag)
        return (1);
    printf("%c ", c);
    if (vflag)
        longt(&stbuf);
    printf("%s: ", name);
    return (response() == 'y');
}

int
response()
{
    char c;

    c = getchar();
    if (c != '\n')
        while (getchar() != '\n')
            ;
    else
        c = 'n';
    return (c);
}

int
checkf(name, mode, howmuch)
    char *name;
    int mode, howmuch;
{
    int l;

    if ((mode & S_IFMT) == S_IFDIR){
        if ((strcmp(name, "SCCS")==0) || (strcmp(name, "RCS")==0))
            return(0);
        return(1);
    }
    if ((l = strlen(name)) < 3)
        return (1);
    if (howmuch > 1 && name[l-2] == '.' && name[l-1] == 'o')
        return (0);
    if (strcmp(name, "core") == 0 ||
        strcmp(name, "errs") == 0 ||
        (howmuch > 1 && strcmp(name, "a.out") == 0))
        return (0);
    /* SHOULD CHECK IF IT IS EXECUTABLE */
    return (1);
}

/* Is the current file a new file, or the newest one of the same name? */
int
checkupdate(arg)
    char *arg;
{
    char name[100];
    long mtime;
    daddr_t seekp;
    daddr_t lookup();

    rewind(tfile);
    for (;;) {
        if ((seekp = lookup(arg)) < 0)
            return (1);
        fseek(tfile, seekp, 0);
        fscanf(tfile, "%s %lo", name, &mtime);
        return (stbuf.st_mtime > mtime);
    }
}

void
done(n)
    int n;
{
    zreap();
    unlink(tname);
    exit(n);
}

/*
 * Do we want the next entry on the tape, i.e. is it selected?  If
 * not, skip over the entire entry.  Return -1 if reached end of tape.
 */
int
wantit(argv)
    char *argv[];
{
    register char **cp;

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
int
prefix(s1, s2)
    register char *s1, *s2;
{
    while (*s1)
        if (*s1++ != *s2++)
            return (0);
    if (*s2)
        return (*s2 == '/');
    return (1);
}

#define N   200
int njab;

daddr_t
lookup(s)
    char *s;
{
    register int i;
    daddr_t a;

    for(i=0; s[i]; i++)
        if (s[i] == ' ')
            break;
    a = bsrch(s, i, low, high);
    return (a);
}

daddr_t
bsrch(s, n, l, h)
    daddr_t l, h;
    char *s;
    int n;
{
    register int i, j;
    char b[N];
    daddr_t m, m1;

    njab = 0;

loop:
    if (l >= h)
        return ((daddr_t) -1);
    m = l + (h-l)/2 - N/2;
    if (m < l)
        m = l;
    fseek(tfile, m, 0);
    fread(b, 1, N, tfile);
    njab++;
    for(i=0; i<N; i++) {
        if (b[i] == '\n')
            break;
        m++;
    }
    if (m >= h)
        return ((daddr_t) -1);
    m1 = m;
    j = i;
    for(i++; i<N; i++) {
        m1++;
        if (b[i] == '\n')
            break;
    }
    i = cmp(b+j, s, n);
    if (i < 0) {
        h = m;
        goto loop;
    }
    if (i > 0) {
        l = m1;
        goto loop;
    }
    return (m);
}

int
cmp(b, s, n)
    char *b, *s;
    int n;
{
    register int i;

    if (b[0] != '\n')
        exit(2);
    for(i=0; i<n; i++) {
        if (b[i+1] > s[i])
            return (-1);
        if (b[i+1] < s[i])
            return (1);
    }
    return (b[i+1] == ' '? 0 : -1);
}

int
readtape(buffer)
    char *buffer;
{
    char *bufp;

    if (first == 0)
        getbuf();
    (void) readtbuf(&bufp, TBLOCK);
    bcopy(bufp, buffer, TBLOCK);
    return(TBLOCK);
}

int
readtbuf(bufpp, size)
    char **bufpp;
    int size;
{
    register int i;

    if (recno >= nblock || first == 0) {
        if ((i = bread(mt, (char *)tbuf, TBLOCK*nblock)) < 0)
            mterr("read", i, 3);
        if (first == 0) {
            if ((i % TBLOCK) != 0) {
                fprintf(stderr, "tar: tape blocksize error\n");
                done(3);
            }
            i /= TBLOCK;
            if (i != nblock) {
                fprintf(stderr, "tar: blocksize = %d\n", i);
                nblock = i;
            }
            first = 1;
        }
        recno = 0;
    }
    if (size > ((nblock-recno)*TBLOCK))
        size = (nblock-recno)*TBLOCK;
    *bufpp = (char *)&tbuf[recno];
    recno += (size/TBLOCK);
    return (size);
}

int
writetbuf(buffer, n)
    register char *buffer;
    register int n;
{
    int i;

    if (first == 0) {
        getbuf();
        first = 1;
    }
    if (recno >= nblock) {
        i = write(mt, (char *)tbuf, TBLOCK*nblock);
        if (i != TBLOCK*nblock)
            mterr("write", i, 2);
        recno = 0;
    }

    /*
     *  Special case:  We have an empty tape buffer, and the
     *  users data size is >= the tape block size:  Avoid
     *  the bcopy and dma direct to tape.  BIG WIN.  Add the
     *  residual to the tape buffer.
     */
    while (recno == 0 && n >= nblock) {
        i = write(mt, buffer, TBLOCK*nblock);
        if (i != TBLOCK*nblock)
            mterr("write", i, 2);
        n -= nblock;
        buffer += (nblock * TBLOCK);
    }

    while (n-- > 0) {
        bcopy(buffer, (char *)&tbuf[recno++], TBLOCK);
        buffer += TBLOCK;
        if (recno >= nblock) {
            i = write(mt, (char *)tbuf, TBLOCK*nblock);
            if (i != TBLOCK*nblock)
                mterr("write", i, 2);
            recno = 0;
        }
    }

    /* Tell the user how much to write to get in sync */
    return (nblock - recno);
}

void
backtape()
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

void
flushtape()
{
    int i;

    i = write(mt, (char *)tbuf, TBLOCK*nblock);
    if (i != TBLOCK*nblock)
        mterr("write", i, 2);
}

void
mterr(operation, i, exitcode)
    char *operation;
    int i, exitcode;
{
    fprintf(stderr, "tar: tape %s error: ", operation);
    if (i < 0)
        perror("");
    else
        fprintf(stderr, "unexpected EOF\n");
    done(exitcode);
}

int
bread(fd, buf, size)
    int fd;
    char *buf;
    int size;
{
    int count;
    static int lastread = 0;

    if (!Bflag)
        return (read(fd, buf, size));

    for (count = 0; count < size; count += lastread) {
        lastread = read(fd, buf, size - count);
        if (lastread <= 0) {
            if (count > 0)
                return (count);
            return (lastread);
        }
        buf += lastread;
    }
    return (count);
}

void
getbuf()
{
    if (nblock == 0) {
        fstat(mt, &stbuf);
        if ((stbuf.st_mode & S_IFMT) == S_IFCHR)
            nblock = NBLOCK;
        else {
            nblock = stbuf.st_blksize / TBLOCK;
            if (nblock == 0)
                nblock = NBLOCK;
        }
    }
    /*
     * One rp2040 process owns a single 96-kbyte window for text, data,
     * bss and stack together, so the block buffer is capped at the
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
char dirstack[PATHSIZ];
#define NTIM (PATHSIZ/2+1)      /* a/b/c/d/... */
time_t mtime[NTIM];

void
dodirtimes(name)
    char *name;
{
    register char *p = dirstack;
    register char *q = name;
    register int ndir = 0;
    char *savp;
    int savndir;

    /* Find common prefix */
    while (*p == *q) {
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
    while ((*p = *q++) != '\0')  /* append the rest of the new dir */
        if (*p++ == '/')
            mtime[++ndir] = -1;
    mtime[ndir] = stbuf.st_mtime;   /* overwrite the last one */
}

void
setimes(path, mt)
    char *path;
    time_t mt;
{
    struct timeval tv[2];

    tv[0].tv_sec = time((time_t *) 0);
    tv[1].tv_sec = mt;
    tv[0].tv_usec = tv[1].tv_usec = 0;
    if (utimes(path, tv) < 0) {
        fprintf(stderr, "tar: can't set time on %s: ", path);
        perror("");
    }
}
