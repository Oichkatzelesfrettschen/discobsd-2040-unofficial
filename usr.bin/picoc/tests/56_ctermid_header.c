#include <stdio.h>

#if L_ctermid
#define L_CTERMID_IS_MACRO 1
#else
#define L_CTERMID_IS_MACRO 0
#endif

void
main()
{
	char terminal_name[L_ctermid];

	printf("%s %d\n", ctermid(terminal_name), L_CTERMID_IS_MACRO);
}
