#include <paths.h>
#include <stdio.h>
#include <string.h>

typedef char terminal_name_must_fit[
    sizeof(_PATH_TTY) <= L_ctermid ? 1 : -1];

char *
ctermid(char *pathname)
{
	static char terminal_name[L_ctermid];

	if (pathname == NULL)
		pathname = terminal_name;
	memcpy(pathname, _PATH_TTY, sizeof(_PATH_TTY));
	return pathname;
}
