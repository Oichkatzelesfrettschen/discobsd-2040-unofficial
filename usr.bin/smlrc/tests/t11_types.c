/* Narrow types, sign and zero extension, long, and casts. */
int printf(char *fmt, ...);

char gc;
signed char gsc;
unsigned char guc;
short gs;
unsigned short gus;
long gl;
unsigned long gul;

int
main(void)
{
	signed char sc;
	unsigned char uc;
	short s;
	unsigned short us;
	long l;

	sc = 200;		/* wraps to -56 */
	uc = 200;
	printf("%d %d\n", sc, uc);

	s = 40000;		/* wraps to -25536 */
	us = 40000;
	printf("%d %d\n", s, us);

	gsc = -1;
	guc = 255;
	gs = -1;
	gus = 65535;
	printf("%d %d %d %d\n", gsc, guc, gs, gus);

	l = 100000;
	l = l * 20;
	printf("%ld\n", l);
	gl = -2000000000;
	gul = 4000000000u;
	printf("%ld %lu\n", gl, gul);

	printf("%d %d\n", (int)(char)300, (int)(unsigned char)300);
	printf("%d\n", (int)(short)70000);
	printf("%d %d\n", (int)'A', (int)sizeof(int));
	printf("%d %d %d\n", (int)sizeof(char), (int)sizeof(short),
	    (int)sizeof(long));
	return 0;
}
