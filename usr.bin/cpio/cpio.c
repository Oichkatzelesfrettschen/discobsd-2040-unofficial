/*
 * cpio -- copy file archives in and out, POSIX odc format.
 *
 * The odc header is the portable ASCII format: a "070707" magic and ten
 * octal fields, 76 bytes in all, followed by the path including its
 * terminator and then the file's bytes, with no padding anywhere. Field
 * widths follow HD_CPIO in 4.4BSD-Lite2 bin/pax/cpio.h. The archive ends
 * with a header naming TRAILER!!! at length zero.
 *
 * Names arrive on standard input one per line for -o, which is how cpio
 * pairs with find(1), so this carries no directory walk of its own; -i
 * reads the archive on standard input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAGIC       "070707"
#define HDRSZ       76
#define TRAILER     "TRAILER!!!"
#define BUFSZ       512

struct cpio_hdr {
    char magic[6];
    char dev[6];
    char ino[6];
    char mode[6];
    char uid[6];
    char gid[6];
    char nlink[6];
    char rdev[6];
    char mtime[11];
    char namesize[6];
    char filesize[11];
};

char path[MAXPATHLEN];
char buf[BUFSZ];
int vflag;
int tflag;

void
putfield(field, width, value)
    char *field;
    int width;
    unsigned long value;
{
    register int i;

    for (i = width - 1; i >= 0; i--) {
        field[i] = (char) ('0' + (int) (value & 7));
        value >>= 3;
    }
}

unsigned long
getfield(field, width)
    char *field;
    int width;
{
    unsigned long v = 0;
    register int i;

    for (i = 0; i < width; i++) {
        if (field[i] < '0' || field[i] > '7')
            break;
        v = (v << 3) | (unsigned long) (field[i] - '0');
    }
    return (v);
}

void
fullwrite(fd, p, n)
    int fd;
    char *p;
    int n;
{
    int i;

    while (n > 0) {
        if ((i = write(fd, p, n)) <= 0) {
            perror("cpio: write");
            exit(1);
        }
        p += i;
        n -= i;
    }
}

/*
 * Standard input is a pipe under find(1), so a short read is the rule
 * rather than end of file.
 */
int
fullread(fd, p, n)
    int fd;
    char *p;
    int n;
{
    int i, got = 0;

    while (n > 0) {
        if ((i = read(fd, p, n)) <= 0)
            break;
        p += i;
        n -= i;
        got += i;
    }
    return (got);
}

void
header(sp, name, namelen)
    struct stat *sp;
    char *name;
    int namelen;
{
    struct cpio_hdr h;

    memcpy(h.magic, MAGIC, sizeof(h.magic));
    putfield(h.dev, sizeof(h.dev), sp->st_dev);
    putfield(h.ino, sizeof(h.ino), sp->st_ino);
    putfield(h.mode, sizeof(h.mode), sp->st_mode);
    putfield(h.uid, sizeof(h.uid), sp->st_uid);
    putfield(h.gid, sizeof(h.gid), sp->st_gid);
    putfield(h.nlink, sizeof(h.nlink), sp->st_nlink);
    putfield(h.rdev, sizeof(h.rdev), sp->st_rdev);
    putfield(h.mtime, sizeof(h.mtime), sp->st_mtime);
    putfield(h.namesize, sizeof(h.namesize), namelen);
    putfield(h.filesize, sizeof(h.filesize), sp->st_size);
    fullwrite(1, (char *) &h, HDRSZ);
    fullwrite(1, name, namelen);
}

void
copyout()
{
    struct stat st;
    int fd, n, len;
    long left;

    while (fgets(path, sizeof(path), stdin) != NULL) {
        len = strlen(path);
        while (len > 0 && path[len-1] == '\n')
            path[--len] = '\0';
        if (len == 0)
            continue;
        if (stat(path, &st) < 0) {
            fprintf(stderr, "cpio: ");
            perror(path);
            continue;
        }
        if ((st.st_mode & S_IFMT) != S_IFREG)
            st.st_size = 0;
        header(&st, path, len + 1);
        if (vflag)
            fprintf(stderr, "%s\n", path);
        if (st.st_size == 0)
            continue;
        if ((fd = open(path, O_RDONLY)) < 0) {
            fprintf(stderr, "cpio: ");
            perror(path);
            exit(1);
        }
        for (left = st.st_size; left > 0; left -= n) {
            n = (int) (left < BUFSZ ? left : BUFSZ);
            if ((n = read(fd, buf, n)) <= 0) {
                fprintf(stderr, "cpio: %s: short read\n", path);
                exit(1);
            }
            fullwrite(1, buf, n);
        }
        close(fd);
    }
    memset(&st, 0, sizeof(st));
    header(&st, TRAILER, (int) sizeof(TRAILER));
}

/*
 * Make every directory leading to name, so an archive listing files before
 * their parents still extracts.
 */
void
mkpath(name)
    char *name;
{
    register char *cp;

    for (cp = name; *cp; cp++) {
        if (*cp != '/' || cp == name)
            continue;
        *cp = '\0';
        if (access(name, 0) < 0)
            mkdir(name, 0777);
        *cp = '/';
    }
}

void
copyin()
{
    struct cpio_hdr h;
    int fd, n, namelen, mode;
    long left, size;

    for (;;) {
        if (fullread(0, (char *) &h, HDRSZ) != HDRSZ)
            break;
        if (memcmp(h.magic, MAGIC, sizeof(h.magic)) != 0) {
            fprintf(stderr, "cpio: not an odc archive\n");
            exit(1);
        }
        namelen = (int) getfield(h.namesize, sizeof(h.namesize));
        if (namelen <= 0 || namelen > (int) sizeof(path)) {
            fprintf(stderr, "cpio: bad name length\n");
            exit(1);
        }
        if (fullread(0, path, namelen) != namelen) {
            fprintf(stderr, "cpio: truncated name\n");
            exit(1);
        }
        path[namelen - 1] = '\0';
        if (strcmp(path, TRAILER) == 0)
            break;
        size = (long) getfield(h.filesize, sizeof(h.filesize));
        mode = (int) getfield(h.mode, sizeof(h.mode));

        if (tflag) {
            printf("%s\n", path);
            fd = -1;
        } else if ((mode & S_IFMT) == S_IFDIR) {
            mkpath(path);
            if (access(path, 0) < 0)
                mkdir(path, mode & 07777);
            fd = -1;
        } else {
            mkpath(path);
            if ((fd = creat(path, mode & 07777)) < 0) {
                fprintf(stderr, "cpio: ");
                perror(path);
                exit(1);
            }
            if (vflag)
                fprintf(stderr, "%s\n", path);
        }
        for (left = size; left > 0; left -= n) {
            n = (int) (left < BUFSZ ? left : BUFSZ);
            if ((n = fullread(0, buf, n)) <= 0) {
                fprintf(stderr, "cpio: %s: truncated\n", path);
                exit(1);
            }
            if (fd >= 0)
                fullwrite(fd, buf, n);
        }
        if (fd >= 0)
            close(fd);
    }
}

int
main(argc, argv)
    int argc;
    char *argv[];
{
    int out = 0, in = 0;
    register char *cp;

    while (--argc > 0) {
        cp = *++argv;
        if (*cp == '-')
            cp++;
        for (; *cp; cp++)
            switch (*cp) {
            case 'o':
                out++;
                break;
            case 'i':
                in++;
                break;
            case 't':
                tflag++;
                break;
            case 'd':
                break;          /* directories are always made */
            case 'v':
                vflag++;
                break;
            default:
                fprintf(stderr, "cpio: %c: unknown option\n", *cp);
                exit(1);
            }
    }
    if (out == in) {
        fprintf(stderr, "cpio: usage: cpio -o[v] | cpio -i[tvd]\n");
        exit(1);
    }
    if (out)
        copyout();
    else
        copyin();
    exit(0);
}
