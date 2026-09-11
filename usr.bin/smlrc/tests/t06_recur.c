/* Recursion, exercising the frame and the stacked-argument path. */
int printf(char *fmt, ...);

static int
fact(int n)
{
	if (n <= 1)
		return 1;
	return n * fact(n - 1);
}

static int
fib(int n)
{
	if (n < 2)
		return n;
	return fib(n - 1) + fib(n - 2);
}

static int
ack(int m, int n)
{
	if (m == 0)
		return n + 1;
	if (n == 0)
		return ack(m - 1, 1);
	return ack(m - 1, ack(m, n - 1));
}

/* Six parameters put two of them on the stack. */
static int
sum6(int a, int b, int c, int d, int e, int f)
{
	if (a == 0)
		return 0;
	return a + b + c + d + e + f + sum6(a - 1, b, c, d, e, f);
}

int
main(void)
{
	printf("%d %d %d\n", fact(7), fib(15), ack(2, 3));
	printf("%d\n", sum6(3, 1, 2, 3, 4, 5));
	return 0;
}
