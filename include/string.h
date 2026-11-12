/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)string.h	5.1.3 (2.11BSD) 1996/3/20
 */

#ifndef _STRING_H_
#define _STRING_H_

#include <_featuretest.h>

#ifndef	NULL
#define	NULL	((void *)0)
#endif

#ifndef	_SIZE_T
#define	_SIZE_T
typedef	unsigned int	size_t;
#endif

char		*strcat(char *, const char *);
char		*strncat(char *, const char *, size_t);
char		*strcpy(char *, const char *);
char		*strncpy(char *, const char *, size_t);

char		*strstr(const char *, const char *);

int		 strcmp(const char *, const char *);
int		 strncmp(const char *, const char *, size_t);
size_t		 strlen(const char *);

int		 memcmp(const void *, const void *, size_t);

void		*memmove(void *, const void *, size_t);
void		*memchr(const void *, int, size_t);
void		*memcpy(void *, const void *, size_t);
void		*memset(void *, int, size_t);
char		*strchr(const char *, int);

char		*strpbrk(const char *, const char *);
char		*strrchr(const char *, int);
char		*strtok(char *, const char *);

size_t		 strcspn(const char *, const char *);
size_t		 strspn(const char *, const char *);

char		*strerror(int);

#if _DISCOBSD_BSD_VISIBLE || _DISCOBSD_POSIX_VISIBLE >= 200809L || \
    _DISCOBSD_XSI_VISIBLE >= 500
char		*strdup(const char *);
#endif

#if _DISCOBSD_BSD_VISIBLE || _DISCOBSD_POSIX_VISIBLE >= 199506L
char		*strtok_r(char *, const char *, char **);
#endif

#if _DISCOBSD_BSD_VISIBLE || _DISCOBSD_XSI_VISIBLE >= 500
void		*memccpy(void *, const void *, int, size_t);
#endif

#if _DISCOBSD_BSD_VISIBLE || _DISCOBSD_POSIX_VISIBLE >= 202405L
void		*memmem(const void *, size_t, const void *, size_t);
size_t		 strlcat(char *, const char *, size_t);
size_t		 strlcpy(char *, const char *, size_t);
#endif

#if _DISCOBSD_BSD_VISIBLE
void		 explicit_bzero(void *, size_t);
char		*strsep(char **, const char *);
int		 timingsafe_bcmp(const void *, const void *, size_t);
const char	*syserrlst(int);
#endif

#endif /* _STRING_H_ */
