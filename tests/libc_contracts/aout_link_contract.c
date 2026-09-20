#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <vis.h>

int
aout_contract_probe(void)
{
	char terminal_name[L_ctermid];
	char visual[8];
	char secret[4] = { 1, 2, 3, 4 };

	explicit_bzero(secret, sizeof(secret));
	return (ctermid(terminal_name) == terminal_name) + raise(0) +
	    (nvis(visual, sizeof(visual), '\n', VIS_CSTYLE, '\0') != visual) +
	    strnvisx(visual, sizeof(visual), "\0", 1, VIS_CSTYLE) +
	    timingsafe_bcmp(secret, "\0\0\0\0", sizeof(secret));
}
