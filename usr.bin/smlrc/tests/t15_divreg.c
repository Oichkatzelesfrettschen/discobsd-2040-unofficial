/*
 * Division and modulo in expressions that contain no function call.
 *
 * ARMv6-M has no divide instruction, so each of these is a BL to an
 * __aeabi helper that takes and returns its operands in r0 and r1. Where
 * the rest of the expression holds a value in a register at the same time,
 * that value has to be somewhere the helper's caller-saved clobbers cannot
 * reach. A statement whose only call is the hidden one is the case that
 * distinguishes a back end that accounts for it from one that does not,
 * and it is why every expression here keeps its result in a variable and
 * prints it afterwards rather than dividing inside the printf argument.
 */
int printf(char *fmt, ...);

int a;
int b;
unsigned ua;
unsigned ub;

int
main(void)
{
	int r;
	int s;
	int t;
	unsigned u;
	int arr[4];
	int i;

	a = 1000;
	b = 7;
	ua = 4000000000u;
	ub = 9u;

	r = (a + 5) + (a / b);
	printf("%d\n", r);

	r = (a / b) + (a + 5);
	printf("%d\n", r);

	r = (a * 2) | (a / b);
	printf("%d\n", r);

	r = (a % b) + (a / b) + (a + 1);
	printf("%d\n", r);

	r = ((a / b) * (a % b)) - ((a + 3) / (b - 4));
	printf("%d\n", r);

	u = (ua / ub) + (ua % ub);
	printf("%u\n", u);

	s = a;
	s /= b;
	t = a;
	t %= b;
	r = s + t + (a - 1);
	printf("%d\n", r);

	for (i = 0; i < 4; i++)
		arr[i] = (i + 100) / (i + 1) + (i * 3);
	printf("%d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);

	r = a;
	r = r / b + r % b + r / (b + 1);
	printf("%d\n", r);
	return 0;
}
