#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <grp.h>

#define	MAXGRP	200

static char GROUP[] = "/etc/group";
static FILE *grf = NULL;
static struct group group;

/*
 * The strings and member vector of the entry getgrent() returns. The
 * record is allocated by the first getgrent() call, so a program that
 * links the group routines without calling them, as most applets of a
 * multicall box do, carries none of it in its bss, its process window
 * or its swap image. It is never freed: getgrgid() and getgrnam() return
 * an entry after endgrent(), and every entry stays valid until the next
 * getgrent() call.
 */
static struct grbuf {
	char	*mem[MAXGRP];
	char	line[256+1];
} *grbuf;

void
setgrent()
{
	if (!grf)
		grf = fopen(GROUP, "r");
	else
		rewind(grf);
}

void
endgrent()
{
	if (grf) {
		fclose(grf);
		grf = NULL;
	}
}

static char *
grskip(p,c)
        register char *p;
        register int c;
{
	while(*p && *p != c) ++p;
	if (*p) *p++ = 0;
	return(p);
}

struct group *
getgrent()
{
	register char *p, **q, **gr_mem;

	/* A failed allocation returns NULL with malloc()'s errno, ENOMEM. */
	if (!grbuf && !(grbuf = malloc(sizeof(*grbuf))))
		return(NULL);
	if (!grf && !(grf = fopen(GROUP, "r")))
		return(NULL);
	if (!(p = fgets(grbuf->line, sizeof(grbuf->line)-1, grf)))
		return(NULL);
	gr_mem = grbuf->mem;
	group.gr_name = p;
	group.gr_passwd = p = grskip(p,':');
	group.gr_gid = atoi(p = grskip(p,':'));
	group.gr_mem = gr_mem;
	p = grskip(p,':');
	grskip(p,'\n');
	q = gr_mem;
	while (*p) {
		if (q < &gr_mem[MAXGRP-1])
			*q++ = p;
		p = grskip(p,',');
	}
	*q = NULL;
	return(&group);
}
