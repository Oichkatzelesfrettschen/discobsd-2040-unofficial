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
#   include <stdint.h>
#   include <sys/types.h>
#   include <sys/select.h>
#   include <sys/stat.h>
#   include <sys/time.h>
#   include <time.h>
#   include <sys/fcntl.h>
#   include <sys/signal.h>
#   include <stdio.h>
#   include <string.h>
#   include <stdlib.h>
#   include <unistd.h>
#   include <errno.h>
#   define _PATH_RANTMP "/tmp/ranlib.XXXXXX"
#else
#   include <sys/types.h>
#   include <sys/dir.h>
#   include <sys/file.h>
#   include <sys/stat.h>
#   include <sys/param.h>
#   include <stdio.h>
#   include <errno.h>
#   include <stdlib.h>
#   include <string.h>
#   include <strings.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <signal.h>
#   include <time.h>
#   include <paths.h>
#endif
#include <ar.h>
#include <ranlib.h>
#include <a.out.h>

#ifdef CROSS
#   include "../ar/archive.h"
#   include "../ar/extern.h"
#else
#   include "archive.h"
#   include "extern.h"
#endif

u_int options;				/* UNUSED -- keep open_archive happy */

CHDR chdr;                              /* converted header */
char *archive;                          /* archive name */
char *tname = "temporary file";		/* temporary file "name" */

typedef struct _rlib {
	struct _rlib *next;		/* next structure */
	off_t pos;			/* offset of defining archive file */
	char *sym;			/* symbol */
	int symlen;			/* strlen(sym) */
} RLIB;
static RLIB *rhead, **pnext;

static FILE *fp;
static long symcnt;			/* symbol count */
static long tsymlen;			/* total string length */
static int verbose;

static void
write_bytes(int fd, const void *buffer, size_t size, char *name)
{
	const char *next_byte;
	ssize_t bytes_written;

	next_byte = buffer;
	while (size != 0) {
		bytes_written = write(fd, next_byte, size);
		if (bytes_written < 0)
			error(name);
		if (bytes_written == 0) {
			errno = EIO;
			error(name);
		}
		next_byte += bytes_written;
		size -= (size_t)bytes_written;
	}
}

void
error(char *name)
{
	(void)fprintf(stderr, "ranlib: %s: %s\n", name, strerror(errno));
	exit(1);
}

static void *
emalloc(int len)
{
	char *p;

	p = malloc((u_int)len);
	if (! p)
		error(archive);
	return((void *)p);
}

static unsigned int
fgetword(FILE *f)
{
        register unsigned int h;

        h = getc (f);
        h |= getc (f) << 8;
        h |= getc (f) << 16;
        h |= getc (f) << 24;
        return h;
}

/*
 * Read a symbol table entry.
 * Return a number of bytes read, 0 on end of table or -1 on EOF.
 * Format of symbol record:
 *  1 byte: length of name in bytes
 *  1 byte: type of symbol (N_UNDF, N_ABS, N_TEXT, etc)
 *  4 bytes: value
 *  N bytes: name
 */
static int
fgetsym(FILE *fi, char *name, unsigned int *value, unsigned short *type)
{
        register int len;
        unsigned nbytes;

        len = getc (fi);
        if (len < 0) {
                return -1;
        }
        if (len == 0) {
                return 0;
        }
        *type = getc (fi);
        *value = fgetword (fi);
        nbytes = len + 6;
        if (name) {
                while (len-- > 0)
                        *name++ = getc (fi);
                *name = '\0';
        } else
                fseek (fi, len, SEEK_CUR);
        return nbytes;
}

/*
 * Read the exec structure; ignore any files that don't look exactly right.
 */
static void
rexec(int rfd, int wfd)
{
	register RLIB *rp;
	long nsyms;
	register int nr, symlen;
	struct exec ebuf;
	off_t r_off, w_off;

	/* Get current offsets for original and tmp files. */
	r_off = lseek(rfd, (off_t)0, SEEK_CUR);
	w_off = lseek(wfd, (off_t)0, SEEK_CUR);

	/* Read in exec structure. */
	nr = read(rfd, (char *)&ebuf, sizeof(struct exec));
	if (nr != sizeof(struct exec))
		goto bad1;

	/* Check magic number and symbol count. */
	if (N_BADMAG(ebuf) || ebuf.a_syms == 0)
		goto bad1;

	/* Seek to symbol table. */
	if (fseek(fp, (off_t)N_SYMOFF(ebuf) + r_off, SEEK_SET) == (off_t)-1)
		goto bad1;

	/* For each symbol read the nlist entry and save it as necessary. */
	nsyms = ebuf.a_syms;
	while (nsyms > 4) {
                unsigned value = 0;
                unsigned short type = 0;
	        char name [256];

                int c = fgetsym(fp, name, &value, &type);
                if (c < 0) {
			if (feof(fp)) {
				/* Bad file format. */
                                errno = EINVAL;
                        }
			error(archive);
                }
                nsyms -= c;
		symlen = c - 6;

		/* Ignore if no name or local. */
		if (symlen <= 0 || ! (type & N_EXT))
			continue;

		/*
		 * If the symbol is an undefined external and the n_value
		 * field is non-zero, keep it.
		 */
		if ((type & N_TYPE) == N_UNDF && ! value)
			continue;

		rp = (RLIB *)emalloc(sizeof(RLIB));
		rp->next = NULL;
		rp->pos = w_off;
		rp->symlen = symlen;
		rp->sym = (char*) emalloc(symlen + 1);
		memcpy(rp->sym, name, (size_t)symlen + 1);
		tsymlen += symlen;

		/* Build in forward order for "ar -m" command. */
		*pnext = rp;
		pnext = &rp->next;
		++symcnt;
	}
bad1:	(void)lseek(rfd, (off_t)r_off, SEEK_SET);
}

/*
 * symobj --
 *	Write the symbol table into the archive, computing offsets as
 *	writing.
 */
static void
symobj(void)
{
	register RLIB *rp;
	/* Wider than the header so a wide field never truncates the format. */
	char hb[2 * sizeof(struct ar_hdr)];
	long ransize, baseoff;
	mode_t creation_mask;

	/* Rewind the archive, leaving the magic number. */
	if (fseek(fp, (off_t)SARMAG, SEEK_SET) == (off_t)-1)
		error(archive);

        /* Compute the size of rantab. */
        ransize = 0;
	for (rp = rhead; rp; rp = rp->next) {
	        ransize += rp->symlen + 5;
        }
        ransize += 4 - (ransize & 3);

	/* Put out the ranlib archive file header. */
	creation_mask = umask(0);
	(void)umask(creation_mask);
	(void)snprintf(hb, sizeof hb, HDR2, RANLIBMAG, 0L, getuid(), getgid(),
	    0666 & ~creation_mask, ransize, ARFMAG);
	if (! fwrite(hb, sizeof(struct ar_hdr), 1, fp))
		error(tname);

	/* Offset of the first archive file. */
	baseoff = SARMAG + sizeof(struct ar_hdr) + ransize;

	/*
	 * Write out the ranlib structures.  The offset into the archive
         * is the base value plus the offset to the first archive file.
	 */
	for (rp = rhead; rp; rp = rp->next) {
                /* Write struct ranlib to file.
                 * 1 byte - length of name.
                 * 4 bytes - seek in archive.
                 * 'len' bytes - symbol name. */
	        unsigned offset = baseoff + rp->pos;
		if (fputc(rp->symlen, fp) == EOF)
			error(tname);
		if (! fwrite((char *)&offset, 4, 1, fp))
			error(archive);
		if (! fwrite(rp->sym, rp->symlen, 1, fp))
			error(tname);
                if (verbose)
                        fprintf (stderr, "%8u %s\n", offset, rp->sym);
	        ransize -= rp->symlen + 5;
	}

        /* Align to word boundary. */
	while (ransize-- > 0)
		if (fputc(0, fp) == EOF)
			error(tname);

	if (fflush(fp) == EOF)
		error(tname);
}

static void
free_symbol_list(void)
{
	RLIB *entry, *next_entry;

	for (entry = rhead; entry != NULL; entry = next_entry) {
		next_entry = entry->next;
		free(entry->sym);
		free(entry);
	}
	rhead = NULL;
}

static void
settime(int afd)
{
	struct ar_hdr *hdr;
	off_t size;
	char buf[50];

	size = SARMAG + sizeof(hdr->ar_name);
	if (lseek(afd, size, SEEK_SET) == (off_t)-1)
		error(archive);
	(void)snprintf(buf, sizeof(buf), "%-12ld",
	    time((time_t *)NULL) + RANLIBSKEW);
	write_bytes(afd, buf, sizeof(hdr->ar_date), archive);
}

int
tmp(void)
{
#ifndef CROSS
	long set, oset;
#endif
	int fd;
	char path[MAXPATHLEN];

	memcpy(path, _PATH_RANTMP, sizeof(_PATH_RANTMP));

#ifndef CROSS
	set = sigmask(SIGHUP) | sigmask(SIGINT) |
             sigmask(SIGQUIT) | sigmask(SIGTERM);
	oset = sigsetmask(set);
#endif
	fd = mkstemp(path);
	if (fd < 0)
		error(tname);
	if (unlink(path) < 0)
		error(path);
#ifndef CROSS
	(void)sigsetmask(oset);
#endif
	return(fd);
}

static int
build(void)
{
	CF cf;
	int afd, replacement_fd, replacement_stream_fd, tfd;
	off_t size;

	afd = open_archive(O_RDWR);
	/*
	 * The symbol reader's stream gets an open file description of its
	 * own. On a dup of afd the two would share one file offset, which
	 * get_arobj and rexec move underneath the stream; a stdio that keeps
	 * a seek inside its buffer without a fresh lseek (macOS) then refills
	 * from the wrong place and the table comes out of other members.
	 */
	fp = fopen(archive, "r");
	if (fp == NULL)
		error(archive);
	tfd = tmp();

	SETCF(afd, archive, tfd, tname, RPAD|WPAD);

	/* Read through the archive, creating list of symbols. */
	pnext = &rhead;
	symcnt = tsymlen = 0;
	while(get_arobj(afd)) {
		if (strcmp(chdr.name, RANLIBMAG) == 0) {
			skip_arobj(afd);
			continue;
		}
		rexec(afd, tfd);
		put_arobj(&cf, (struct stat *)NULL);
	}
	*pnext = NULL;
	if (fclose(fp) < 0)
		error(archive);

	replacement_fd = begin_archive_rewrite(afd);
	replacement_stream_fd = dup(replacement_fd);
	if (replacement_stream_fd < 0)
		error(archive);
	fp = fdopen(replacement_stream_fd, "r+");
	if (fp == NULL)
		error(archive);

	/* Create the symbol table in the complete replacement archive. */
	symobj();
	free_symbol_list();

	/* Copy the saved objects into the replacement archive. */
	size = lseek(tfd, (off_t)0, SEEK_CUR);
	if (size == (off_t)-1 ||
	    lseek(tfd, (off_t)0, SEEK_SET) == (off_t)-1)
		error(tname);
	SETCF(tfd, tname, replacement_fd, archive, NOPAD);
	copy_ar(&cf, size, 0);
	if (close(tfd) < 0)
		error(tname);

	/* Set the time. */
	settime(replacement_fd);
	if (fclose(fp) < 0)
		error(archive);
	commit_archive_rewrite(afd, replacement_fd);
	return(0);
}

static int
touch(void)
{
	CF cf;
	struct stat archive_stat;
	int afd, replacement_fd;
	off_t payload_size;

	afd = open_archive(O_RDWR);

	if (!get_arobj(afd) ||
	    strncmp(RANLIBMAG, chdr.name, sizeof(RANLIBMAG) - 1) != 0) {
		(void)fprintf(stderr,
			    "ranlib: %s: no symbol table.\n", archive);
		close_archive(afd);
		return(1);
	}
	if (fstat(afd, &archive_stat) < 0)
		error(archive);
	payload_size = archive_stat.st_size - SARMAG;
	if (lseek(afd, (off_t)SARMAG, SEEK_SET) == (off_t)-1)
		error(archive);
	replacement_fd = begin_archive_rewrite(afd);
	SETCF(afd, archive, replacement_fd, archive, NOPAD);
	copy_ar(&cf, payload_size, 0);
	settime(replacement_fd);
	commit_archive_rewrite(afd, replacement_fd);
	return(0);
}

static void
usage(void)
{
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  ranlib [-t] file...\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -t      Update the timestamp of the symbol map\n");
        fprintf(stderr, "  -v      Verbose: print the resulting symbol map\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch, eval, tflag;

	tflag = 0;
	while ((ch = getopt(argc, argv, "tv")) != EOF)
		switch(ch) {
		case 't':
			tflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	argv += optind;

	if (! *argv)
		usage();

	for (eval = 0; (archive = *argv++);)
		eval |= tflag ? touch() : build();
        return eval;
}

/*
 * For archive.c.
 */
char *
rname(char *path)
{
	register char *ind;

	ind = strrchr(path, '/');
	if (! ind)
	        return path;
	return ind + 1;
}

void
badfmt(void)
{
	errno = EINVAL;
	error(archive);
}
