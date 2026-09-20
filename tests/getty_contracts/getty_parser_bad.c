/* The calibrated parser fixture reports every public flag as absent. */

#include "gettytab.h"

int
getflag(char *identifier)
{
	(void)identifier;
	return -1;
}
