/*
 * Host fixture for usr.bin/make's ordering of modification times.
 *
 * make_order OUT DEPENDENCY: doname() of a target OUT with one dependency,
 * where the file times are counts of seconds since 1970 as the kernel
 * stores them. exists() returns the target's time the way the target build
 * does, converting a time_t through STORED_TIME (the stat() of a four-byte
 * time_t returns a count with bit 31 set as a negative number). The
 * dependency is already made, so doname() takes its time from the name
 * block. The program prints "rebuilt" when doname() runs the target's
 * command and "current" when it does not.
 *
 * doname.c is linked unchanged; the rest of make is replaced here by the
 * minimum doname() calls.
 */
#include "defs.h"

#include <stdlib.h>
#include <string.h>

#ifndef STORED_TIME
#define STORED_TIME(t) ((TIMETYPE)(t))
#endif

int dbgflag;
int silflag = 1;
int noexflag;
int keepgoing;
int noruleflag;
int touchflag;
int questflag;
int ndocoms;
int ignerr;
int okdel;
char *prompt = "";
char arfile[256];
char *arfname;
char *sys_siglist[NSIG];
struct lineblock *sufflist;

static unsigned long target_time;
static unsigned commands_run;

void
setvar(char *name, char *value)
{
	(void)name;
	(void)value;
}

struct varblock *
varptr(char *name)
{
	static struct varblock variable;

	(void)name;
	return &variable;
}

char *
subst(char *source, char *destination)
{
	return strcpy(destination, source);
}

void
fixname(char *source, char *destination)
{
	(void)strcpy(destination, source);
}

int
dosys(char *command, int nohalt)
{
	(void)command;
	(void)nohalt;
	commands_run++;
	return 0;
}

void
touch(int force, char *name)
{
	(void)force;
	(void)name;
}

void
fatal(char *message)
{
	(void)message;
	exit(2);
}

void
fatal1(char *format, char *argument)
{
	(void)format;
	(void)argument;
	exit(2);
}

struct nameblock *
srchname(char *name)
{
	(void)name;
	return NULL;
}

int
suffix(char *name, char *suffix_text, char *prefix_text)
{
	(void)name;
	(void)suffix_text;
	(void)prefix_text;
	return 0;
}

char *
concat(char *first, char *second, char *buffer)
{
	(void)first;
	(void)second;
	return buffer;
}

char *
copys(char *text)
{
	return text;
}

struct depblock *
srchdir(char *pattern, int make_chain, struct depblock *next)
{
	(void)pattern;
	(void)make_chain;
	return next;
}

struct chain *
appendq(struct chain *head, char *tail)
{
	(void)tail;
	return head;
}

char *
mkqlist(struct chain *list)
{
	(void)list;
	return "";
}

TIMETYPE
exists(struct nameblock *name)
{
	(void)name;
	return STORED_TIME((time_t)target_time);
}

TIMETYPE
prestime(void)
{
	return 1;
}

int
main(int argc, char **argv)
{
	struct nameblock dependency_name, target;
	struct depblock dependency;
	struct shblock command;
	struct lineblock line;
	TIMETYPE made;

	if (argc != 3)
		return 2;
	target_time = strtoul(argv[1], NULL, 10);
	memset(&dependency_name, 0, sizeof(dependency_name));
	dependency_name.namep = "dependency";
	dependency_name.done = 2;
	dependency_name.modtime = STORED_TIME((time_t)strtoul(argv[2], NULL, 10));
	dependency.nxtdepblock = NULL;
	dependency.depname = &dependency_name;
	command.nxtshblock = NULL;
	command.shbp = "build";
	line.nxtlineblock = NULL;
	line.depp = &dependency;
	line.shp = &command;
	memset(&target, 0, sizeof(target));
	target.namep = "target";
	target.linep = &line;
	target.septype = ALLDEPS;
	if (doname(&target, 1, &made) != 0)
		return 1;
	puts(commands_run != 0 ? "rebuilt" : "current");
	return 0;
}
