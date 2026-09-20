#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

int
aout_contract_probe(void)
{
	char terminal_name[L_ctermid];
	char visual[8];
	char runtime_path[32];
	char secret[4] = { 1, 2, 3, 4 };

	explicit_bzero(secret, sizeof(secret));
	return (ctermid(terminal_name) == terminal_name) + raise(0) +
	    confstr(_CS_PATH, runtime_path, sizeof(runtime_path)) +
	    sysconf(_SC_OPEN_MAX) +
	    (nvis(visual, sizeof(visual), '\n', VIS_CSTYLE, '\0') != visual) +
	    strnvisx(visual, sizeof(visual), "\0", 1, VIS_CSTYLE) +
	    timingsafe_bcmp(secret, "\0\0\0\0", sizeof(secret));
}
