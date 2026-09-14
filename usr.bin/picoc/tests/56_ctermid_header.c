#include <stdio.h>

void
main()
{
	char terminal_name[L_ctermid];

	printf("%s %d\n", ctermid(terminal_name), L_ctermid);
}
