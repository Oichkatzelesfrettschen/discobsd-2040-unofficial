/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Hugh Smith at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifdef CROSS
#   include <time.h>
#   include <fcntl.h>
#   include <stdint.h>
#   include <sys/file.h>
#   include <sys/stat.h>
#   include <stdio.h>
#   include <string.h>
#   include <strings.h>
#   include <stdlib.h>
#   include <unistd.h>
#   include <errno.h>
#else
#   include <sys/param.h>
#   include <sys/stat.h>
#   include <sys/dir.h>
#   include <sys/file.h>
#   include <stdio.h>
#   include <string.h>
#   include <strings.h>
#   include <stdlib.h>
#   include <unistd.h>
#   include <errno.h>
#   include <fcntl.h>
#endif
#include <ar.h>
#include "archive.h"
#include "extern.h"

typedef struct ar_hdr HDR;
/*
 * The header is exactly sizeof(HDR) bytes on the wire; the buffer is wider
 * so a wide field can never truncate the snprintf that formats it.
 */
static char hb[2 * sizeof(HDR)];	/* real header */
static char archive_rewrite_path[MAXPATHLEN];
static char archive_rewrite_directory[MAXPATHLEN];
static int archive_rewrite_cleanup_registered;
static int archive_rewrite_directory_fd = -1;
static dev_t archive_source_device;
static ino_t archive_source_inode;
static int archive_source_identity_valid;

static void
write_all(int fd, const void *buffer, size_t size, const char *name)
{
	const char *next_byte;
	ssize_t bytes_written;

	next_byte = buffer;
	while (size != 0) {
		bytes_written = write(fd, next_byte, size);
		if (bytes_written < 0)
			error((char *)name);
		if (bytes_written == 0) {
			errno = EIO;
			error((char *)name);
		}
		next_byte += bytes_written;
		size -= (size_t)bytes_written;
	}
}

static void
check_archive_source_identity(int source_fd)
{
	struct stat path_stat;
	struct stat source_stat;

	if (fstat(source_fd, &source_stat) < 0)
		error(archive);
	if (!S_ISREG(source_stat.st_mode)) {
		errno = EINVAL;
		error(archive);
	}
	if (source_stat.st_nlink != 1) {
		errno = EMLINK;
		error(archive);
	}
	if (lstat(archive, &path_stat) < 0)
		error(archive);
	if (S_ISLNK(path_stat.st_mode)) {
		errno = ELOOP;
		error(archive);
	}
	if (source_stat.st_dev != path_stat.st_dev ||
	    source_stat.st_ino != path_stat.st_ino) {
		errno = ESTALE;
		error(archive);
	}
	if (archive_source_identity_valid &&
	    (source_stat.st_dev != archive_source_device ||
	    source_stat.st_ino != archive_source_inode)) {
		errno = ESTALE;
		error(archive);
	}
	archive_source_device = source_stat.st_dev;
	archive_source_inode = source_stat.st_ino;
	archive_source_identity_valid = 1;
}

static void
cleanup_archive_rewrite(void)
{
	if (archive_rewrite_path[0] != '\0')
		(void)unlink(archive_rewrite_path);
	if (archive_rewrite_directory_fd >= 0)
		(void)close(archive_rewrite_directory_fd);
}

/*
 * Build archive replacements beside the destination so rename(2) commits
 * one complete byte sequence without exposing a partly rewritten archive.
 */
int
begin_archive_rewrite(int source_fd)
{
	static const char rewrite_template[] = ".ar.XXXXXX";
	struct stat source_stat;
	char rewrite_path[MAXPATHLEN];
	const char *path_separator;
	size_t directory_length;
	size_t rewrite_directory_length;
	mode_t creation_mask;
	int replacement_fd;

	if (!archive_rewrite_cleanup_registered) {
		if (atexit(cleanup_archive_rewrite) != 0) {
			errno = ENOMEM;
			error(archive);
		}
		archive_rewrite_cleanup_registered = 1;
	}
	if (archive_rewrite_path[0] != '\0' ||
	    archive_rewrite_directory_fd >= 0) {
		errno = EBUSY;
		error(archive);
	}

	path_separator = strrchr(archive, '/');
	directory_length = path_separator == NULL ?
	    0 : (size_t)(path_separator - archive + 1);
	if (directory_length + sizeof(rewrite_template) >
	    sizeof(rewrite_path)) {
		errno = ENAMETOOLONG;
		error(archive);
	}
	if (directory_length != 0)
		memcpy(rewrite_path, archive, directory_length);
	memcpy(rewrite_path + directory_length, rewrite_template,
	    sizeof(rewrite_template));
	if (path_separator == NULL) {
		(void)strlcpy(archive_rewrite_directory, ".",
		    sizeof(archive_rewrite_directory));
	} else {
		rewrite_directory_length = path_separator == archive ?
		    1 : (size_t)(path_separator - archive);
		if (rewrite_directory_length >=
		    sizeof(archive_rewrite_directory)) {
			errno = ENAMETOOLONG;
			error(archive);
		}
		if (snprintf(archive_rewrite_directory,
		    sizeof(archive_rewrite_directory), "%.*s",
		    (int)rewrite_directory_length, archive) !=
		    (int)rewrite_directory_length) {
			errno = ENAMETOOLONG;
			error(archive);
		}
	}

	archive_rewrite_directory_fd = open(archive_rewrite_directory, O_RDONLY);
	if (archive_rewrite_directory_fd < 0)
		error(archive_rewrite_directory);
	if (fsync(archive_rewrite_directory_fd) < 0)
		error(archive_rewrite_directory);
	if (source_fd >= 0)
		check_archive_source_identity(source_fd);

	replacement_fd = mkstemp(rewrite_path);
	if (replacement_fd < 0)
		error(rewrite_path);
	(void)strlcpy(archive_rewrite_path, rewrite_path,
	    sizeof(archive_rewrite_path));
	if (flock(replacement_fd, LOCK_EX|LOCK_NB) && errno != EOPNOTSUPP)
		error(archive_rewrite_path);
	if (source_fd >= 0) {
		if (fstat(source_fd, &source_stat) < 0 ||
		    fchown(replacement_fd, source_stat.st_uid,
		    source_stat.st_gid) < 0 ||
		    fchmod(replacement_fd, source_stat.st_mode & 07777) < 0)
			error(archive_rewrite_path);
	} else {
		creation_mask = umask(0);
		(void)umask(creation_mask);
		if (fchmod(replacement_fd, 0666 & ~creation_mask) < 0)
			error(archive_rewrite_path);
	}
	write_all(replacement_fd, ARMAG, SARMAG, archive_rewrite_path);
	return(replacement_fd);
}

void
abort_archive_rewrite(int source_fd, int replacement_fd)
{
	int saved_errno;

	saved_errno = 0;
	if (close(replacement_fd) < 0)
		saved_errno = errno;
	if (archive_rewrite_path[0] != '\0') {
		if (unlink(archive_rewrite_path) < 0 && saved_errno == 0)
			saved_errno = errno;
		archive_rewrite_path[0] = '\0';
	}
	if (fsync(archive_rewrite_directory_fd) < 0 && saved_errno == 0)
		saved_errno = errno;
	if (close(archive_rewrite_directory_fd) < 0 && saved_errno == 0)
		saved_errno = errno;
	archive_rewrite_directory_fd = -1;
	archive_source_identity_valid = 0;
	if (source_fd >= 0 && close(source_fd) < 0 && saved_errno == 0)
		saved_errno = errno;
	if (saved_errno != 0) {
		errno = saved_errno;
		error(archive);
	}
}

void
commit_archive_rewrite(int source_fd, int replacement_fd)
{
	int created;

	created = source_fd < 0;
	if (fsync(replacement_fd) < 0)
		error(archive_rewrite_path);
	if (source_fd >= 0) {
		check_archive_source_identity(source_fd);
		if (rename(archive_rewrite_path, archive) < 0)
			error(archive_rewrite_path);
		archive_rewrite_path[0] = '\0';
	} else {
		if (link(archive_rewrite_path, archive) < 0)
			error(archive_rewrite_path);
		if (unlink(archive_rewrite_path) < 0)
			error(archive_rewrite_path);
		archive_rewrite_path[0] = '\0';
	}
	if (fsync(archive_rewrite_directory_fd) < 0)
		error(archive_rewrite_directory);
	if (close(archive_rewrite_directory_fd) < 0)
		error(archive_rewrite_directory);
	archive_rewrite_directory_fd = -1;
	archive_source_identity_valid = 0;
	if (source_fd >= 0 && close(source_fd) < 0)
		error(archive);
	if (close(replacement_fd) < 0)
		error(archive);
	if (created && !(options & AR_C))
		(void)fprintf(stderr, "ar: creating archive %s.\n", archive);
}

int
open_archive(int mode)
{
	int allow_missing, fd, nr;
	char buf[SARMAG];

	allow_missing = mode & O_CREAT;
	mode &= ~O_CREAT;
	fd = open(archive, mode, 0666);
	if (fd < 0 && allow_missing && errno == ENOENT)
		return(-1);
	if (fd < 0)
		error(archive);

	/*
	 * Attempt to place a lock on the opened file - if we get an
	 * error then someone is already working on this library (or
	 * it's going across NFS).
	 */
	if (flock(fd, LOCK_EX|LOCK_NB) && errno != EOPNOTSUPP)
		error(archive);

	/*
	 * If not created, O_RDONLY|O_RDWR indicates that it has to be
	 * in archive format.
	 */
	if ((mode & 3) == O_RDONLY || (mode & 3) == O_RDWR) {
		if ((nr = read(fd, buf, SARMAG)) != SARMAG) {
			if (nr >= 0)
				badfmt();
			error(archive);
		} else if (memcmp(buf, ARMAG, SARMAG) != 0)
			badfmt();
	}
	return(fd);
}

void
close_archive(int fd)
{
	if (close(fd) < 0)			/* Implicit unlock. */
		error(archive);
}

static long
parse_archive_number(const char *field, size_t field_size, int base)
{
	char number[21];
	char *end;
	long value;

	if (field_size >= sizeof(number))
		badfmt();
	if (snprintf(number, sizeof(number), "%.*s", (int)field_size,
	    field) != (int)field_size)
		badfmt();
	errno = 0;
	value = strtol(number, &end, base);
	if (errno == ERANGE || end == number || value < 0)
		badfmt();
	while (*end == ' ')
		end++;
	if (*end != '\0')
		badfmt();
	return(value);
}

/*
 * get_arobj --
 *	read the archive header for this member
 */
int
get_arobj(int fd)
{
	const struct ar_hdr *hdr;
	register int len, nr;
	register char *p;
	long value;

	nr = read(fd, hb, sizeof(HDR));
	if (nr != sizeof(HDR)) {
		if (!nr)
			return(0);
		if (nr < 0)
			error(archive);
		badfmt();
	}

	hdr = (const struct ar_hdr *)hb;
	if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1) != 0)
		badfmt();

	/* Convert the header into the internal format. */
#define	DECIMAL	10
#define	OCTAL	 8

	value = parse_archive_number(hdr->ar_date, sizeof(hdr->ar_date), DECIMAL);
	chdr.date = (time_t)value;
	if ((long)chdr.date != value)
		badfmt();
	value = parse_archive_number(hdr->ar_uid, sizeof(hdr->ar_uid), DECIMAL);
	chdr.uid = (int)value;
	if ((long)chdr.uid != value)
		badfmt();
	value = parse_archive_number(hdr->ar_gid, sizeof(hdr->ar_gid), DECIMAL);
	chdr.gid = (int)value;
	if ((long)chdr.gid != value)
		badfmt();
	value = parse_archive_number(hdr->ar_mode, sizeof(hdr->ar_mode), OCTAL);
	chdr.mode = (unsigned short)value;
	if ((long)chdr.mode != value)
		badfmt();
	value = parse_archive_number(hdr->ar_size, sizeof(hdr->ar_size), DECIMAL);
	chdr.size = (off_t)value;
	if ((long)chdr.size != value)
		badfmt();

	/* Leading spaces should never happen. */
	if (hdr->ar_name[0] == ' ')
		badfmt();

	/*
	 * Long name support.  Set the "real" size of the file, and the
	 * long name flag/size.
	 */
	if (memcmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1) == 0) {
		value = parse_archive_number(
		    hdr->ar_name + sizeof(AR_EFMT1) - 1,
		    sizeof(hdr->ar_name) - (sizeof(AR_EFMT1) - 1), DECIMAL);
		if (value <= 0 || value > MAXNAMLEN)
			badfmt();
		chdr.lname = len = (int)value;
		if (chdr.size < len)
			badfmt();
		nr = read(fd, chdr.name, (size_t)len);
		if (nr != len) {
			if (nr < 0)
				error(archive);
			badfmt();
		}
		chdr.name[len] = 0;
		chdr.size -= len;
	} else {
		chdr.lname = 0;
		memcpy(chdr.name, hdr->ar_name, sizeof(hdr->ar_name));

		/* Strip trailing spaces, null terminate. */
		for (p = chdr.name + sizeof(hdr->ar_name) - 1; *p == ' '; --p);
		*++p = '\0';
	}
	return(1);
}

/*
 * copy_ar --
 *	Copy size bytes of member payload from one file to another - taking
 *	care to handle the extra byte (for odd size members) when reading
 *	archives and writing an extra byte if necessary when adding files to
 *	an archive.  A member occupies lname + size bytes after its header,
 *	so the caller passes the extended name length that belongs with this
 *	payload: chdr.lname when reading a member out of an archive, the name
 *	just written when writing one, and zero for a bulk copy of bytes that
 *	already carry their own padding.
 *
 *	The padding is really unnecessary, and is almost certainly a remnant
 *	of early archive formats where the header included binary data which
 *	a PDP-11 required to start on an even byte boundary.  (Or, perhaps,
 *	because 16-bit word addressed copies were faster?)  Anyhow, it should
 *	have been ripped out long ago.
 */
void
copy_ar(CF *cfp, off_t size, int lname)
{
	static char pad = '\n';
	off_t sz;
	register int from, nr, nw, off, to;
	size_t bytes_to_read;
	char buf[8*1024];

	if (size < 0)
		badfmt();
	sz = size;

	from = cfp->rfd;
	to = cfp->wfd;
	while (sz) {
		bytes_to_read = sz < (off_t)sizeof(buf) ?
		    (size_t)sz : sizeof(buf);
		nr = read(from, buf, bytes_to_read);
		if (nr <= 0)
		        break;
		sz -= nr;
		for (off = 0; off < nr; off += nw) {
			nw = write(to, buf + off, (size_t)(nr - off));
			if (nw < 0)
				error(cfp->wname);
			if (nw == 0) {
				errno = EIO;
				error(cfp->wname);
			}
		}
	}
	if (sz) {
		if (nr == 0)
			badfmt();
		error(cfp->rname);
	}

	if ((cfp->flags & RPAD) && ((size + lname) & 1) &&
	    (nr = read(from, buf, 1)) != 1) {
		if (nr == 0)
			badfmt();
		error(cfp->rname);
	}
	if ((cfp->flags & WPAD) && ((size + lname) & 1))
		write_all(to, &pad, 1, cfp->wname);
}

/*
 * put_arobj --
 *	Write an archive member to a file.
 */
void
put_arobj(CF *cfp, struct stat *sb)
{
	register int lname;
	register char *name;
	struct ar_hdr *hdr;
	off_t size;

	/*
	 * If passed an sb structure, reading a file from disk.  Get stat(2)
	 * information, build a name and construct a header.  (Files are named
	 * by their last component in the archive.)  If not, then just write
	 * the last header read.
	 */
	if (sb) {
		name = rname(cfp->rname);
		if (fstat(cfp->rfd, sb) < 0)
			error(cfp->rname);

		/*
		 * If not truncating names and the name is too long or contains
		 * a space, use extended format 1.
		 */
		lname = strlen(name);
		if (options & AR_TR) {
			if (lname > OLDARMAXNAME) {
				(void)fflush(stdout);
				(void)fprintf(stderr,
				    "ar: warning: %s truncated to %.*s\n",
				    name, OLDARMAXNAME, name);
				(void)fflush(stderr);
			}
			(void)snprintf(hb, sizeof(hb), HDR3,
			    name, (long)sb->st_mtime, sb->st_uid, sb->st_gid,
			    sb->st_mode, (long)sb->st_size, ARFMAG);
			lname = 0;
		} else if (lname > (int)sizeof(hdr->ar_name) || index(name, ' '))
			(void)snprintf(hb, sizeof(hb), HDR1, AR_EFMT1,
			    lname, (long)sb->st_mtime, sb->st_uid, sb->st_gid,
			    sb->st_mode, (long) sb->st_size + lname, ARFMAG);
		else {
			lname = 0;
			(void)snprintf(hb, sizeof(hb), HDR2,
			    name, (long)sb->st_mtime, sb->st_uid, sb->st_gid,
			    sb->st_mode, (long) sb->st_size, ARFMAG);
		}
		size = sb->st_size;
	} else {
		lname = chdr.lname;
		name = chdr.name;
		size = chdr.size;
	}

	write_all(cfp->wfd, hb, sizeof(HDR), cfp->wname);
	if (lname)
		write_all(cfp->wfd, name, (size_t)lname, cfp->wname);
	copy_ar(cfp, size, lname);
}

/*
 * skip_arobj -
 *	Skip over an object -- taking care to skip the pad bytes.
 */
void
skip_arobj(int fd)
{
	off_t len;

	len = chdr.size + ((chdr.size + chdr.lname) & 1);
	if (lseek(fd, len, SEEK_CUR) == (off_t)-1)
		error(archive);
}
