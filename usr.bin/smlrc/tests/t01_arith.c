/* Signed and unsigned arithmetic, including division and modulo, which on
   ARMv6-M are calls to the libgcc __aeabi helpers. */
int printf(char *fmt, ...);

int
main(void)
{
	int a;
	int b;
	unsigned ua;
	unsigned ub;

	a = 47;
	b = 5;
	printf("%d %d %d %d\n", a + b, a - b, a * b, -a);
	printf("%d %d\n", a / b, a % b);
	a = -47;
	printf("%d %d\n", a / b, a % b);
	a = 47;
	b = -5;
	printf("%d %d\n", a / b, a % b);

	ua = 4000000000u;
	ub = 7u;
	printf("%u %u\n", ua / ub, ua % ub);

	a = 1;
	printf("%d %d %d\n", a << 10, 1024 >> 3, -1024 >> 3);
	ua = 0x80000000u;
	printf("%u\n", ua >> 28);

	a = 0xF0F0;
	b = 0x0FF0;
	printf("%d %d %d %d\n", a & b, a | b, a ^ b, ~a);

	a = 5;
	a += 3; a -= 1; a *= 4; a /= 7; a %= 3;
	printf("%d\n", a);
	return 0;
}
