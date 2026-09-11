/* Loops, short-circuit operators, the conditional operator, goto, and
   comparisons used both as branches and as values. */
int printf(char *fmt, ...);

static int calls;

static int
side(int v)
{
	calls = calls + 1;
	return v;
}

int
main(void)
{
	int i;
	int j;
	int sum;

	sum = 0;
	for (i = 0; i < 10; i++) {
		if (i == 3)
			continue;
		if (i == 8)
			break;
		sum = sum + i;
	}
	printf("%d\n", sum);

	i = 0;
	sum = 0;
	while (i < 5) {
		sum = sum * 10 + i;
		i++;
	}
	printf("%d\n", sum);

	i = 0;
	do {
		i = i + 3;
	} while (i < 20);
	printf("%d\n", i);

	calls = 0;
	if (side(0) && side(1))
		printf("unexpected\n");
	printf("%d\n", calls);

	calls = 0;
	if (side(1) || side(1))
		printf("or taken\n");
	printf("%d\n", calls);

	i = 7;
	printf("%d %d\n", i > 3 ? 100 : 200, i < 3 ? 100 : 200);
	printf("%d %d %d %d\n", 1 < 2, 2 < 1, 3 == 3, 3 != 3);
	printf("%d %d\n", !0, !5);

	sum = 0;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			if (i != j)
				sum = sum + i * 10 + j;
	printf("%d\n", sum);

	i = 0;
again:
	i = i + 1;
	if (i < 4)
		goto again;
	printf("%d\n", i);
	return 0;
}
