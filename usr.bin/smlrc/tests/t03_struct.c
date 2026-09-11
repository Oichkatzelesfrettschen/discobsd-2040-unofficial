/* Structures and unions reached through pointers, including nesting. */
int printf(char *fmt, ...);

struct point {
	int x;
	int y;
};

struct rec {
	char tag;
	int count;
	struct point pos;
	char name[8];
};

union bits {
	unsigned word;
	unsigned char byte[4];
};

struct rec table[3];

static void
setrec(struct rec *r, int t, int c, int x, int y)
{
	r->tag = t;
	r->count = c;
	r->pos.x = x;
	r->pos.y = y;
}

int
main(void)
{
	struct rec *r;
	union bits u;
	int i;

	for (i = 0; i < 3; i++)
		setrec(&table[i], 'A' + i, i * 100, i, -i);

	for (i = 0; i < 3; i++) {
		r = &table[i];
		printf("%c %d %d %d\n", r->tag, r->count, r->pos.x, r->pos.y);
	}

	u.word = 0x04030201;
	printf("%d %d %d %d\n", u.byte[0], u.byte[1], u.byte[2], u.byte[3]);

	r = table;
	r->count += 7;
	r->count *= 2;
	printf("%d\n", table[0].count);
	return 0;
}
