/* Pointers, arrays, and pointer arithmetic. */
int printf(char *fmt, ...);

int g[8];

int
main(void)
{
	int i;
	int *p;
	int a[5];
	char buf[8];
	char *cp;

	for (i = 0; i < 8; i++)
		g[i] = i * i;
	for (i = 0; i < 5; i++)
		a[i] = 10 - i;

	p = g;
	printf("%d %d %d\n", *p, *(p + 3), p[7]);
	p = p + 4;
	printf("%d %d\n", *p, *(p - 2));
	printf("%d\n", (int)(p - g));

	p = a;
	*p = 99;
	p[4] = 77;
	printf("%d %d %d\n", a[0], a[2], a[4]);

	cp = buf;
	for (i = 0; i < 7; i++)
		*cp++ = 'a' + i;
	*cp = 0;
	printf("%s %d\n", buf, (int)(cp - buf));
	return 0;
}
