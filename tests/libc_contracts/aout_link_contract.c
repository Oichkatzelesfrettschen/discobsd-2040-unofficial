#include <signal.h>
#include <stdio.h>
#include <vis.h>

int
aout_contract_probe(void)
{
	char terminal_name[L_ctermid];
	char visual[8];

	return (ctermid(terminal_name) == terminal_name) + raise(0) +
	    (nvis(visual, sizeof(visual), '\n', VIS_CSTYLE, '\0') != visual) +
	    strnvisx(visual, sizeof(visual), "\0", 1, VIS_CSTYLE);
}
