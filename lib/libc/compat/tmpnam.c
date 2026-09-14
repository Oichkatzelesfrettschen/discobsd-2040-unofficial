/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <sys/param.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef _PATH_USRTMP
#define _PATH_USRTMP _PATH_TMP
#endif
#ifndef L_tmpnam
#define L_tmpnam MAXPATHLEN
#endif

#define TEMPORARY_SUFFIX "XXXXXX"

FILE *
tmpfile(void)
{
	FILE *stream;
	char pathname[] = _PATH_USRTMP TEMPORARY_SUFFIX;
	int descriptor;
	int saved_errno;

	descriptor = mkstemp(pathname);
	if (descriptor < 0)
		return NULL;
	if (unlink(pathname) < 0) {
		saved_errno = errno;
		(void)close(descriptor);
		errno = saved_errno;
		return NULL;
	}
	errno = 0;
	stream = fdopen(descriptor, "w+");
	if (stream == NULL) {
		saved_errno = errno != 0 ? errno : EMFILE;
		(void)close(descriptor);
		errno = saved_errno;
	}
	return stream;
}

char *
tmpnam(char pathname[L_tmpnam])
{
	int allocated;

	allocated = pathname == NULL;
	if (allocated) {
		pathname = (char *)malloc((size_t)MAXPATHLEN);
		if (pathname == NULL)
			return NULL;
	}
	memcpy(pathname, _PATH_USRTMP TEMPORARY_SUFFIX,
	    sizeof(_PATH_USRTMP TEMPORARY_SUFFIX));
	if (mktemp(pathname) != NULL)
		return pathname;
	if (allocated)
		free(pathname);
	return NULL;
}

static char *
try_directory(char *pathname, const char *directory, const char *prefix)
{
	size_t directory_length;
	size_t prefix_length;
	size_t separator_length;
	size_t suffix_length;
	size_t available_length;
	char *cursor;

	if (directory == NULL || directory[0] == '\0')
		return NULL;
	if (prefix == NULL)
		prefix = "";
	directory_length = strlen(directory);
	prefix_length = strlen(prefix);
	separator_length = directory[directory_length - 1] == '/' ? 0 : 1;
	suffix_length = sizeof(TEMPORARY_SUFFIX) - 1;
	if (directory_length >= (size_t)MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	available_length = (size_t)MAXPATHLEN - directory_length;
	if (separator_length + suffix_length + 1 > available_length ||
	    prefix_length >
	    available_length - separator_length - suffix_length - 1) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	cursor = pathname;
	memcpy(cursor, directory, directory_length);
	cursor += directory_length;
	if (separator_length != 0)
		*cursor++ = '/';
	memcpy(cursor, prefix, prefix_length);
	cursor += prefix_length;
	memcpy(cursor, TEMPORARY_SUFFIX, suffix_length + 1);
	return mktemp(pathname);
}

char *
tempnam(char *directory, char *prefix)
{
	char *environment_directory;
	char *pathname;

	pathname = (char *)malloc((size_t)MAXPATHLEN);
	if (pathname == NULL)
		return NULL;
	environment_directory = getenv("TMPDIR");
	if (try_directory(pathname, environment_directory, prefix) != NULL ||
	    try_directory(pathname, directory, prefix) != NULL ||
	    try_directory(pathname, _PATH_USRTMP, prefix) != NULL ||
	    try_directory(pathname, _PATH_TMP, prefix) != NULL)
		return pathname;
	free(pathname);
	return NULL;
}
