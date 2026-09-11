/* switch, including fallthrough, default, and a sparse label set. */
int printf(char *fmt, ...);

static int
classify(int n)
{
	int r;

	r = 0;
	switch (n) {
	case 0:
		r = 100;
		break;
	case 1:
	case 2:
		r = 200;
		break;
	case 17:
		r = 300;
		/* fallthrough */
	case 18:
		r = r + 7;
		break;
	case -5:
		r = -500;
		break;
	default:
		r = 999;
		break;
	}
	return r;
}

int
main(void)
{
	int i;
	int v[7];

	v[0] = 0; v[1] = 1; v[2] = 2; v[3] = 17;
	v[4] = 18; v[5] = -5; v[6] = 42;

	for (i = 0; i < 7; i++)
		printf("%d:%d\n", v[i], classify(v[i]));
	return 0;
}
