/*
 * One a.out for the small utilities, dispatched on the name it was
 * invoked by. Every 2.11BSD program links its own copy of libc, about
 * 16 kbytes of stdio and printf for a utility of two, so twenty of them
 * cost 400 kbytes of flash where their own code totals 60. Here they
 * share one copy. The tools keep their sources; the Makefile localizes
 * each tool's globals and renames its main.
 */

#include <string.h>
#include <unistd.h>


int cat_main(int, char **);
int chgrp_main(int, char **);
int chmod_main(int, char **);
int chown_main(int, char **);
int cp_main(int, char **);
int echo_main(int, char **);
int hostname_main(int, char **);
int kill_main(int, char **);
int ln_main(int, char **);
int ls_main(int, char **);
int mkdir_main(int, char **);
int mv_main(int, char **);
int pwd_main(int, char **);
int rm_main(int, char **);
int rmdir_main(int, char **);
int sleep_main(int, char **);
int sync_main(int, char **);
int test_main(int, char **);

static const struct tool {
	const char	*name;
	int		(*main)(int, char **);
} tools[] = {

	{ "cat", cat_main },
	{ "chgrp", chgrp_main },
	{ "chmod", chmod_main },
	{ "chown", chown_main },
	{ "cp", cp_main },
	{ "echo", echo_main },
	{ "hostname", hostname_main },
	{ "kill", kill_main },
	{ "ln", ln_main },
	{ "ls", ls_main },
	{ "mkdir", mkdir_main },
	{ "mv", mv_main },
	{ "pwd", pwd_main },
	{ "rm", rm_main },
	{ "rmdir", rmdir_main },
	{ "sleep", sleep_main },
	{ "sync", sync_main },
	{ "test", test_main },
	{ "[", test_main },
	{ 0, 0 }
};

int
main(int argc, char **argv)
{
	const struct tool *t;
	const char *name, *p;

	name = argv[0];
	for (p = name; *p; p++)
		if (*p == '/')
			name = p + 1;
	if ((strcmp(name, "box") == 0 || strcmp(name, "sysbox") == 0) &&
	    argc > 1) {
		argc--;
		argv++;
		name = argv[0];
	}
	for (t = tools; t->name; t++)
		if (strcmp(name, t->name) == 0)
			return t->main(argc, argv);
	write(2, "box: no such tool\n", 18);
	return 1;
}
