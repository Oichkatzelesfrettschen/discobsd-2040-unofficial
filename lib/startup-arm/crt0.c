/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdlib.h>
#include <unistd.h>

/*
 * C runtime startoff.  When an a.out is loaded by the kernel, the kernel
 * sets up the stack as follows:
 *
 *	--------------------------------- top of memory
 *	| (NULL)			| end of command-line args and env
 *	|-------------------------------|
 *	| 				|
 *	| environment strings		|
 *	| 				|
 *	|-------------------------------|
 *	| 				|
 *	| argument strings		|
 *	| 				|
 *	|-------------------------------|
 *	| envv[envc] (NULL)		| end of environment vector tag, a 0
 *	|-------------------------------|
 *	| envv[envc-1]			| pointer to last environment string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| envv[0]			| pointer to first environment string
 *	|-------------------------------|
 *	| argv[argc] (NULL)		| end of argument vector tag, a 0
 *	|-------------------------------|
 *	| argv[argc-1]			| pointer to last argument string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| argv[0]			| pointer to first argument string
 *	|-------------------------------|
 *	| &envv[0]			| pointer to array of env pointers
 *	|-------------------------------|
 *	| &argv[0]			| pointer to array of arg pointers
 *	|-------------------------------|
 * sp->	| argc				| number of command-line arguments
 *	---------------------------------
 *
 * Arguments are passed in registers $a1, $a2 and $a3.
 *
 * Crt0 simply moves the env to environ variable, calculates
 * the __progname and then calls main.
 */
extern int main (int, char **, char **);

#ifdef DISCOBSD_SHARED_LIBC
/* The shared entry installs r9 and binds both names to the caller's BSS. */
#define CRT_ENTRY __xip_start_main
extern char **environ;
extern const char *__progname;
#else
#define CRT_ENTRY _start
char **environ;
const char *__progname = "";
#endif

void CRT_ENTRY (int, char **, char **);

/* The entry function. */
void
CRT_ENTRY (argc, argv, env)
	int argc;
	char **argv;
	char **env;
{
#ifdef DISCOBSD_SHARED_LIBC
	__progname = "";
#endif
	environ = env;
	if (argc > 0 && argv[0] != 0) {
		const char *s;

		__progname = argv[0];
		for (s = __progname; *s != '\0'; s++)
			if (*s == '/')
				__progname = s + 1;
	}
	exit (main (argc, argv, environ));
}
