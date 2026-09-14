#include <signal.h>
#include <stdio.h>

int
aout_contract_probe(void)
{
	char terminal_name[L_ctermid];

	return (ctermid(terminal_name) == terminal_name) + raise(0);
}
