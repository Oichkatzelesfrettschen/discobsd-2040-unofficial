/*
 * Single-precision float, which Smaller C lowers to calls on the libgcc
 * soft-float helpers because ARMv6-M has no FPU.
 *
 * Results are printed as integers rather than with %f: floating point
 * conversion in printf is a separate libc member that the rp2040 link line
 * leaves out unless a program asks for it with PRINTF_FLOAT=yes, so a %f
 * here would test the link line rather than the code generator.
 */
int printf(char *fmt, ...);

float gf;
float gtab[4];

static float
scale(float v, float by)
{
	return v * by;
}

int
main(void)
{
	float a;
	float b;
	float c;
	int i;

	a = 3.5;
	b = 2.0;

	printf("%d %d %d %d\n", (int)(a + b), (int)(a - b),
	    (int)(a * b), (int)(a / b));

	printf("%d %d %d %d\n", a > b, a < b, a >= b, a == b);

	c = -a;
	printf("%d\n", (int)c);

	i = 7;
	c = i;			/* int to float */
	c = c / 2.0;
	printf("%d\n", (int)(c * 2.0));

	gf = 10.25;
	printf("%d\n", (int)(gf * 4.0));

	for (i = 0; i < 4; i++)
		gtab[i] = i * 1.5;
	printf("%d %d %d %d\n", (int)(gtab[0] * 2), (int)(gtab[1] * 2),
	    (int)(gtab[2] * 2), (int)(gtab[3] * 2));

	printf("%d\n", (int)scale(2.5, 4.0));

	a = 1.0;
	for (i = 0; i < 10; i++)
		a = a * 2.0;
	printf("%d\n", (int)a);
	return 0;
}
