#include <stdio.h>

int
main(void)
{
	int a = 355, b = 113, q, r;

	q = a / b;
	r = a % b;
	printf("%d / %d = %d rem %d\n", a, b, q, r);
	return 0;
}
