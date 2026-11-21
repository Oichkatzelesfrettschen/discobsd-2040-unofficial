/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	DEVPREFIX	"/dev/"
#define	DEVPREFIXLEN	(sizeof(DEVPREFIX) - 1)

/*
 * The path buffer holds "/dev/" and the longest directory entry name, and
 * the returned name is its tail. readdir() returns an entry that lives in
 * the DIR, which closedir() frees, so the name survives the close only as
 * this copy; the next call overwrites it.
 */
static char devpath[DEVPREFIXLEN + MAXNAMLEN + 1];

/*
 * Return the name of the special file in /dev whose device number is dev
 * and whose file type (S_IFCHR or S_IFBLK) is type, or "??" when /dev
 * holds none. A character and a block device may share one number, so the
 * type decides between them.
 */
char *
devname(int dev, int type)
{
	DIR *dir;
	struct direct *entry;
	struct stat st;
	char *name = devpath + DEVPREFIXLEN;

	dir = opendir("/dev");
	if (dir == NULL) {
		perror("/dev");
		return ("??");
	}
	memcpy(devpath, DEVPREFIX, DEVPREFIXLEN);
	while ((entry = readdir(dir)) != NULL) {
		strcpy(name, entry->d_name);
		if (stat(devpath, &st) < 0)
			continue;
		if ((st.st_mode & S_IFMT) != (mode_t)type)
			continue;
		if (st.st_rdev == dev) {
			closedir(dir);
			return (name);
		}
	}
	closedir(dir);
	return ("??");
}
