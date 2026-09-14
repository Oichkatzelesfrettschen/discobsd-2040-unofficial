/*-
 * Copyright (c) 1991, 1993
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
#include <sys/param.h>

#include <errno.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void current(void);
static void pretty(struct passwd *);
static void group(struct passwd *, int);
static void usage(void);
static void user(struct passwd *);
static struct passwd *who(char *);

static const char *
invocation_name(const char *pathname)
{
	const char *cursor;
	const char *name;

	name = pathname;
	for (cursor = pathname; *cursor != '\0'; ++cursor)
		if (*cursor == '/')
			name = cursor + 1;
	return name;
}

int
main(int argc, char *argv[])
{
	struct group *gr;
	struct passwd *pw;
	const char *program_name;
	char *login_name;
	u_int id;
	int Gflag, ch, gflag, nflag, pflag, rflag, uflag;

	Gflag = gflag = nflag = pflag = rflag = uflag = 0;
	program_name = invocation_name(argv[0]);
	if (strcmp(program_name, "whoami") == 0) {
		if (argc != 1)
			errx(1, "usage: whoami");
		nflag = uflag = 1;
		--argc;
		++argv;
	} else if (strcmp(program_name, "groups") == 0) {
		if (argc > 2)
			errx(1, "usage: groups [user]");
		Gflag = nflag = 1;
		--argc;
		++argv;
	} else if (strcmp(program_name, "logname") == 0) {
		if (argc != 1)
			errx(1, "usage: logname");
		login_name = getlogin();
		if (login_name == NULL)
			err(1, "getlogin");
		(void)puts(login_name);
		return 0;
	} else {
		while ((ch = getopt(argc, argv, "Ggnpru")) != EOF)
			switch(ch) {
			case 'G':
				Gflag = 1;
				break;
			case 'g':
				gflag = 1;
				break;
			case 'n':
				nflag = 1;
				break;
			case 'p':
				pflag = 1;
				break;
			case 'r':
				rflag = 1;
				break;
			case 'u':
				uflag = 1;
				break;
			case '?':
			default:
				usage();
			}
		argc -= optind;
		argv += optind;
		if (argc > 1)
			usage();
	}

	switch(Gflag + gflag + pflag + uflag) {
	case 1:
		break;
	case 0:
		if (!nflag && !rflag)
			break;
		/* FALLTHROUGH */
	default:
		usage();
	}

	pw = argc != 0 ? who(*argv) : NULL;

	if (gflag) {
		id = pw ? (u_int)pw->pw_gid : rflag ? getgid() : getegid();
		if (nflag && (gr = getgrgid(id)))
			(void)printf("%s\n", gr->gr_name);
		else
			(void)printf("%u\n", (unsigned)id);
		exit(0);
	}

	if (uflag) {
		id = pw ? (u_int)pw->pw_uid : rflag ? getuid() : geteuid();
		if (nflag && (pw = getpwuid(id)))
			(void)printf("%s\n", pw->pw_name);
		else
			(void)printf("%u\n", (unsigned)id);
		exit(0);
	}

	if (Gflag) {
		group(pw, nflag);
		exit(0);
	}

	if (pflag) {
		pretty(pw);
		exit(0);
	}

	if (pw)
		user(pw);
	else
		current();
	exit(0);
}

void
pretty(struct passwd *pw)
{
	struct group *gr;
	u_int eid, rid;
	char *login;

	if (pw) {
		(void)printf("uid\t%s\n", pw->pw_name);
		(void)printf("groups\t");
		group(pw, 1);
	} else {
		if ((login = getlogin()) == NULL)
			err(1, "getlogin");

		pw = getpwuid(rid = getuid());
		if (pw == NULL || strcmp(login, pw->pw_name))
			(void)printf("login\t%s\n", login);
		if (pw)
			(void)printf("uid\t%s\n", pw->pw_name);
		else
			(void)printf("uid\t%u\n", rid);

		if ((eid = geteuid()) != rid) {
			if ((pw = getpwuid(eid)) != NULL)
				(void)printf("euid\t%s", pw->pw_name);
			else
				(void)printf("euid\t%u", eid);
		}
		if ((rid = getgid()) != (eid = getegid())) {
			if ((gr = getgrgid(rid)) != NULL)
				(void)printf("rgid\t%s\n", gr->gr_name);
			else
				(void)printf("rgid\t%u\n", rid);
		}
		(void)printf("groups\t");
		group(NULL, 1);
	}
}

void
current(void)
{
	struct group *gr;
	struct passwd *pw;
	int cnt, id, eid, lastid, ngroups;
	gid_t groups[NGROUPS];
	char *fmt;

	id = getuid();
	(void)printf("uid=%u", (unsigned)id);
	if ((pw = getpwuid(id)) != NULL)
		(void)printf("(%s)", pw->pw_name);
	if ((eid = geteuid()) != id) {
		(void)printf(" euid=%u", (unsigned)eid);
		if ((pw = getpwuid(eid)) != NULL)
			(void)printf("(%s)", pw->pw_name);
	}
	id = getgid();
	(void)printf(" gid=%u", (unsigned)id);
	if ((gr = getgrgid(id)) != NULL)
		(void)printf("(%s)", gr->gr_name);
	if ((eid = getegid()) != id) {
		(void)printf(" egid=%u", (unsigned)eid);
		if ((gr = getgrgid(eid)) != NULL)
			(void)printf("(%s)", gr->gr_name);
	}
	ngroups = getgroups(NGROUPS, groups);
	if (ngroups > 0)
		for (fmt = " groups=%u", lastid = -1, cnt = 0; cnt < ngroups;
		    fmt = ", %u", lastid = id) {
			id = groups[cnt++];
			if (lastid == id)
				continue;
			(void)printf(fmt, (unsigned)id);
			if ((gr = getgrgid(id)) != NULL)
				(void)printf("(%s)", gr->gr_name);
		}
	(void)printf("\n");
}

void
user(struct passwd *pw)
{
	struct group *gr;
	gid_t groups[NGROUPS + 1];
	int cnt, id, lastid, ngroups;
	char *fmt;

	id = pw->pw_uid;
	(void)printf("uid=%u(%s)", (unsigned)id, pw->pw_name);
	(void)printf(" gid=%u", (unsigned)pw->pw_gid);
	if ((gr = getgrgid(pw->pw_gid)) != NULL)
		(void)printf("(%s)", gr->gr_name);
	ngroups = NGROUPS + 1;
	(void) getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups);
	fmt = " groups=%u";
	for (lastid = -1, cnt = 0; cnt < ngroups; ++cnt) {
		if (lastid == (id = groups[cnt]))
			continue;
		(void)printf(fmt, (unsigned)id);
		fmt = " %u";
		if ((gr = getgrgid(id)) != NULL)
			(void)printf("(%s)", gr->gr_name);
		lastid = id;
	}
	(void)printf("\n");
}

void
group(struct passwd *pw, int nflag)
{
	struct group *gr;
	int cnt, id, lastid, ngroups;
	gid_t groups[NGROUPS + 1];
	char *fmt;

	if (pw) {
		ngroups = NGROUPS + 1;
		(void) getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups);
	} else {
		groups[0] = getgid();
		ngroups = getgroups(NGROUPS, groups + 1) + 1;
	}
	fmt = nflag ? "%s" : "%u";
	for (lastid = -1, cnt = 0; cnt < ngroups; ++cnt) {
		if (lastid == (id = groups[cnt]))
			continue;
		if (nflag) {
			if ((gr = getgrgid(id)) != NULL)
				(void)printf(fmt, gr->gr_name);
			else
				(void)printf(*fmt == ' ' ? " %u" : "%u",
				    (unsigned)id);
			fmt = " %s";
		} else {
			(void)printf(fmt, (unsigned)id);
			fmt = " %u";
		}
		lastid = id;
	}
	(void)printf("\n");
}

struct passwd *
who(char *u)
{
	struct passwd *pw;
	long id;
	char *ep;

	/*
	 * Translate user argument into a pw pointer.  First, try to
	 * get it as specified.  If that fails, try it as a number.
	 */
	if ((pw = getpwnam(u)) != NULL)
		return(pw);
	id = strtol(u, &ep, 10);
	if (*u && !*ep && (pw = getpwuid(id)))
		return(pw);
	errx(1, "%s: No such user", u);
	return NULL;
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: id [user]\n");
	(void)fprintf(stderr, "       id -G [-n] [user]\n");
	(void)fprintf(stderr, "       id -g [-nr] [user]\n");
	(void)fprintf(stderr, "       id -u [-nr] [user]\n");
	exit(1);
}
