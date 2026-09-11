/* Globals, file-scope statics, function-scope statics, and initializers. */
int printf(char *fmt, ...);

int gcount = 5;
int garray[6] = { 1, 2, 3, 4, 5, 6 };
char gstr[] = "initialized";
char *gptr = "pointed at";
static int sfile = 41;
int gzero;
int gbss[4];

static int
tick(void)
{
	static int n = 100;

	n = n + 1;
	return n;
}

int
main(void)
{
	int i;
	int sum;

	printf("%d %d %s %s\n", gcount, sfile, gstr, gptr);

	sum = 0;
	for (i = 0; i < 6; i++)
		sum = sum + garray[i];
	printf("%d\n", sum);

	printf("%d %d %d\n", gzero, gbss[0], gbss[3]);

	printf("%d %d %d\n", tick(), tick(), tick());

	gcount = gcount * 2;
	sfile = sfile + 1;
	printf("%d %d\n", gcount, sfile);
	return 0;
}
