/*
 * SP alignment at a call boundary. AAPCS32 5.2.1.2 makes SP 8-byte aligned
 * at every public interface, and the Thumb-1 back end builds its argument
 * area out of single-word pushes, so the property belongs to the code
 * generator rather than to the instruction set.
 *
 * Each probe in t17_align.gnu.c is a naked function written by the cross
 * compiler that returns the caller's sp & 7. Every line below prints PASS
 * when the call site left SP aligned and FAIL with the observed remainder
 * when it did not, so the expected output is the oracle.
 *
 * The shapes covered are the ones whose stack-passed word counts and call
 * depths differ: zero through ten arguments, a call issued from an odd depth
 * inside another call's argument list, an indirect call through a function
 * pointer, a variadic call, a structure passed by value, and a call made
 * from a function with a frame too large for a Thumb-1 immediate.
 */
int printf(char *fmt, ...);

struct big {
	int a;
	int b;
	int c;
};

/* Five bytes, so the helper's rounding of the pushed size to a word boundary
   is not the identity it is for a structure whose size is already a word
   multiple. */
struct odd {
	char a;
	char b;
	char c;
	char d;
	char e;
};

int p0(void);
int p1(int);
int p2(int, int);
int p3(int, int, int);
int p4(int, int, int, int);
int p5(int, int, int, int, int);
int p6(int, int, int, int, int, int);
int p7(int, int, int, int, int, int, int);
int p8(int, int, int, int, int, int, int, int);
int p9(int, int, int, int, int, int, int, int, int);
int p10(int, int, int, int, int, int, int, int, int, int);
int pv(int, ...);
int ps(struct big, int, int);
int pso(struct odd, int, int, int);
int psm(int, struct odd, int, int);

static int (*fp5)(int, int, int, int, int);
static int (*fp6)(int, int, int, int, int, int);

static int
chk(char *name, int m)
{
	if (m)
		printf("FAIL %s sp&7=%d\n", name, m);
	else
		printf("PASS %s\n", name);
	return m;
}

/*
 * Arguments are evaluated right to left, so the probe call here runs with
 * the name already pushed: one word deep, the depth at which a call passing
 * an even number of stack words still needs a pad.
 */
static int
chknested(int m, char *name)
{
	return chk(name, m);
}

/* A frame past the reach of SUB (SP minus immediate), which makes the
   prologue take its register path; the pad must still land correctly. */
static int
deepframe(void)
{
	int v[200];
	int i;

	for (i = 0; i < 200; i++)
		v[i] = i;
	return chk("p5 from a large frame", p5(v[1], v[2], v[3], v[4], v[5]));
}

int
main(void)
{
	struct big s;
	struct odd o;
	int m;

	m = p0();  chk("p0", m);
	m = p1(1); chk("p1", m);
	m = p2(1, 2); chk("p2", m);
	m = p3(1, 2, 3); chk("p3", m);
	m = p4(1, 2, 3, 4); chk("p4", m);
	m = p5(1, 2, 3, 4, 5); chk("p5", m);
	m = p6(1, 2, 3, 4, 5, 6); chk("p6", m);
	m = p7(1, 2, 3, 4, 5, 6, 7); chk("p7", m);
	m = p8(1, 2, 3, 4, 5, 6, 7, 8); chk("p8", m);
	m = p9(1, 2, 3, 4, 5, 6, 7, 8, 9); chk("p9", m);
	m = p10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10); chk("p10", m);

	chknested(p0(), "p0 nested");
	chknested(p4(1, 2, 3, 4), "p4 nested");
	chknested(p5(1, 2, 3, 4, 5), "p5 nested");
	chknested(p6(1, 2, 3, 4, 5, 6), "p6 nested");

	fp5 = p5;
	fp6 = p6;
	m = fp5(1, 2, 3, 4, 5); chk("indirect p5", m);
	m = fp6(1, 2, 3, 4, 5, 6); chk("indirect p6", m);
	chknested(fp5(1, 2, 3, 4, 5), "indirect p5 nested");
	chknested(fp6(1, 2, 3, 4, 5, 6), "indirect p6 nested");

	m = pv(1, 2, 3, 4, 5); chk("variadic 5", m);
	m = pv(1, 2, 3, 4, 5, 6, 7); chk("variadic 7", m);
	chknested(pv(1, 2, 3, 4, 5), "variadic 5 nested");
	chknested(pv(1, 2, 3, 4, 5, 6, 7), "variadic 7 nested");

	s.a = 1;
	s.b = 2;
	s.c = 3;
	m = ps(s, 4, 5); chk("struct by value", m);
	chknested(ps(s, 4, 5), "struct by value nested");

	o.a = 1;
	o.b = 2;
	o.c = 3;
	o.d = 4;
	o.e = 5;
	m = pso(o, 6, 7, 8); chk("odd struct first", m);
	m = psm(6, o, 7, 8); chk("odd struct in the middle", m);
	chknested(pso(o, 6, 7, 8), "odd struct first nested");
	chknested(psm(6, o, 7, 8), "odd struct in the middle nested");

	/* A probe reached through nested calls, each of which moves the depth. */
	m = p3(p0(), p1(1), p2(1, 2)); chk("p3 of probes", m);

	deepframe();
	return 0;
}
