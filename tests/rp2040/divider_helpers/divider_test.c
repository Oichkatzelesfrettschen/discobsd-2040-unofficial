/*
 * Operand-pair test for the AEABI integer division helpers of
 * lib/libc/arm/gen/aeabi_div.S, run as a Thumb-1 program under qemu-arm.
 *
 * The helpers under test (__aeabi_idiv, __aeabi_idivmod, __aeabi_uidiv and
 * __aeabi_uidivmod) are the library source assembled with DIVIDER_MODEL;
 * qemu-user has no SIO block, so harness.S supplies a RAM register block
 * and a model of the unsigned divide in place of the hardware wait. Two
 * oracles judge every result: the shift-and-subtract helpers the library
 * used before the hardware divider (reference_div.S, renamed ref_), and
 * a restoring division written here from the C99 6.5.5 rules. Neither
 * oracle and nothing else in this file divides with an operator, because
 * the compiler would lower one to the helpers under test.
 *
 * The test establishes the helpers' sign, zero-divisor, INT_MIN and
 * register-preservation behavior and their use of the divider's register
 * offsets and result order. It does not establish the eight-cycle wait,
 * the divider's arithmetic or its overflow behavior, or the user MPU grant;
 * those need the board.
 */

typedef unsigned int u32;
typedef int s32;

typedef struct {
	u32 quot;
	u32 rem;
} divmod_t;

extern divmod_t __aeabi_uidivmod(u32, u32);
extern divmod_t __aeabi_idivmod(s32, s32);
extern u32 __aeabi_uidiv(u32, u32);
extern s32 __aeabi_idiv(s32, s32);
extern divmod_t ref_aeabi_uidivmod(u32, u32);
extern divmod_t ref_aeabi_idivmod(s32, s32);
extern u32 ref_aeabi_uidiv(u32, u32);
extern s32 ref_aeabi_idiv(s32, s32);

extern void harness_write(const char *text, u32 length);
extern u32 harness_call(const void *helper, u32 a, u32 b, u32 out[2]);

/* The register block the library's DIVIDER_MODEL build addresses. */
#define MODEL_UDIVIDEND	0x60u
#define MODEL_UDIVISOR	0x64u
#define MODEL_QUOTIENT	0x70u
#define MODEL_REMAINDER	0x74u
#define MODEL_POISON	0xdeadbeefu
#define MODEL_SIZE	0x80u

u32 divider_model_sio[MODEL_SIZE / 4u];

static u32 model_steps;
static u32 model_zero_divisors;

static u32
model_get(u32 offset)
{
	return divider_model_sio[offset >> 2];
}

static void
model_set(u32 offset, u32 value)
{
	divider_model_sio[offset >> 2] = value;
}

/* Restoring division: no operator that the compiler lowers to a helper. */
static divmod_t
oracle_udivmod(u32 n, u32 d)
{
	divmod_t result = { 0u, 0u };
	int bit;

	if (d == 0u) {
		result.rem = n;
		return result;
	}
	for (bit = 31; bit >= 0; bit--) {
		result.rem = (result.rem << 1) | ((n >> bit) & 1u);
		if (result.rem >= d) {
			result.rem -= d;
			result.quot |= 1u << bit;
		}
	}
	return result;
}

/* C99 6.5.5: truncation toward zero, remainder takes the dividend's sign. */
static divmod_t
oracle_sdivmod(s32 n, s32 d)
{
	u32 magnitude_n = n < 0 ? 0u - (u32)n : (u32)n;
	u32 magnitude_d = d < 0 ? 0u - (u32)d : (u32)d;
	divmod_t result = oracle_udivmod(magnitude_n, magnitude_d);

	if (d == 0) {
		result.quot = 0u;
		result.rem = (u32)n;	/* the dividend, sign included */
		return result;
	}
	if ((n < 0) != (d < 0))
		result.quot = 0u - result.quot;
	if (n < 0)
		result.rem = 0u - result.rem;
	return result;
}

/* Called by harness.S in place of the divider's eight-cycle wait. */
void
divider_model_compute(void)
{
	u32 dividend = model_get(MODEL_UDIVIDEND);
	u32 divisor = model_get(MODEL_UDIVISOR);
	divmod_t result;

	model_steps++;
	if (divisor == 0u) {
		/* The helpers answer a zero divisor in software. */
		model_zero_divisors++;
		model_set(MODEL_QUOTIENT, MODEL_POISON);
		model_set(MODEL_REMAINDER, MODEL_POISON);
		return;
	}
	result = oracle_udivmod(dividend, divisor);
	model_set(MODEL_QUOTIENT, result.quot);
	model_set(MODEL_REMAINDER, result.rem);
}

static void
model_arm(void)
{
	u32 index;

	for (index = 0; index < MODEL_SIZE / 4u; index++)
		divider_model_sio[index] = MODEL_POISON;
}

/* Registers the helper must leave alone: everything but the four it uses. */
static int
model_block_intact(void)
{
	u32 offset;

	for (offset = 0; offset < MODEL_SIZE; offset += 4u) {
		if (offset == MODEL_UDIVIDEND || offset == MODEL_UDIVISOR ||
		    offset == MODEL_QUOTIENT || offset == MODEL_REMAINDER)
			continue;
		if (model_get(offset) != MODEL_POISON)
			return 0;
	}
	return 1;
}

static u32 failures;
static u32 comparisons;

static void
put_hex(u32 value)
{
	char text[11];
	u32 digit;

	text[0] = '0';
	text[1] = 'x';
	for (digit = 0; digit < 8u; digit++)
		text[2u + digit] = "0123456789abcdef"[(value >> (28u - 4u * digit)) & 15u];
	text[10] = ' ';
	harness_write(text, 11u);
}

static void
put_text(const char *text)
{
	u32 length = 0;

	while (text[length] != '\0')
		length++;
	harness_write(text, length);
}

static void
report(const char *name, u32 a, u32 b, u32 want0, u32 want1, u32 got0,
    u32 got1, u32 mask, int intact)
{
	failures++;
	if (failures > 20u)
		return;
	put_text(name);
	put_text(" a=");
	put_hex(a);
	put_text("b=");
	put_hex(b);
	put_text("want=");
	put_hex(want0);
	put_hex(want1);
	put_text("got=");
	put_hex(got0);
	put_hex(got1);
	put_text("clobber=");
	put_hex(mask);
	put_text(intact ? "\n" : "block-written\n");
}

/*
 * Run one helper through the preservation wrapper and compare against the
 * expected pair. check_remainder is clear for the quotient-only entries,
 * which leave r1 undefined.
 */
static void
check(const char *name, const void *helper, u32 a, u32 b, divmod_t want,
    int check_remainder)
{
	u32 out[2];
	u32 mask;
	int intact;

	model_arm();
	mask = harness_call(helper, a, b, out);
	intact = model_block_intact();
	comparisons++;
	if (mask != 0u || !intact || out[0] != want.quot ||
	    (check_remainder && out[1] != want.rem))
		report(name, a, b, want.quot, want.rem, out[0], out[1], mask,
		    intact);
}

static void
check_pair(u32 a, u32 b)
{
	divmod_t unsigned_want = oracle_udivmod(a, b);
	divmod_t signed_want = oracle_sdivmod((s32)a, (s32)b);

	check("uidivmod", (const void *)__aeabi_uidivmod, a, b, unsigned_want, 1);
	check("uidiv", (const void *)__aeabi_uidiv, a, b, unsigned_want, 0);
	check("idivmod", (const void *)__aeabi_idivmod, a, b, signed_want, 1);
	check("idiv", (const void *)__aeabi_idiv, a, b, signed_want, 0);
	check("ref-uidivmod", (const void *)ref_aeabi_uidivmod, a, b,
	    unsigned_want, 1);
	check("ref-idivmod", (const void *)ref_aeabi_idivmod, a, b, signed_want,
	    1);
	/* The new entries against the old helpers directly. */
	{
		u32 old_out[2];
		u32 new_out[2];

		(void)harness_call((const void *)ref_aeabi_idivmod, a, b, old_out);
		model_arm();
		(void)harness_call((const void *)__aeabi_idivmod, a, b, new_out);
		comparisons++;
		if (old_out[0] != new_out[0] || old_out[1] != new_out[1])
			report("new-vs-old-idivmod", a, b, old_out[0], old_out[1],
			    new_out[0], new_out[1], 0u, 1);
		(void)harness_call((const void *)ref_aeabi_uidivmod, a, b, old_out);
		model_arm();
		(void)harness_call((const void *)__aeabi_uidivmod, a, b, new_out);
		comparisons++;
		if (old_out[0] != new_out[0] || old_out[1] != new_out[1])
			report("new-vs-old-uidivmod", a, b, old_out[0], old_out[1],
			    new_out[0], new_out[1], 0u, 1);
	}
}

#define EDGE_MAX 512u
static u32 edges[EDGE_MAX];
static u32 edge_count;

static void
add_edge(u32 value)
{
	u32 index;

	for (index = 0; index < edge_count; index++)
		if (edges[index] == value)
			return;
	if (edge_count < EDGE_MAX)
		edges[edge_count++] = value;
}

static void
build_edges(void)
{
	static const u32 fixed[] = {
		0u, 1u, 2u, 3u, 5u, 6u, 7u, 9u, 10u, 11u, 100u, 127u, 128u,
		255u, 256u, 1000u, 12345u, 65535u, 65536u, 65537u, 1000000u,
		1000000007u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu,
		0xffffffffu, 0x55555555u, 0xaaaaaaaau, 0x0000ffffu, 0xffff0000u,
		0x7fff0000u, 0x00010001u, 0xdeadbeefu, 0x12345678u, 0x9abcdef0u,
	};
	u32 index;
	u32 power;

	for (index = 0; index < sizeof(fixed) / sizeof(fixed[0]); index++) {
		add_edge(fixed[index]);
		add_edge(0u - fixed[index]);
	}
	for (power = 0; power < 32u; power++) {
		u32 value = 1u << power;

		add_edge(value);
		add_edge(value - 1u);
		add_edge(value + 1u);
		add_edge(0u - value);
		add_edge(0u - value + 1u);
		add_edge(0u - value - 1u);
	}
}

static u32 rng_state = 0x9e3779b9u;

static u32
rng_next(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* A random operand of random width, so small divisors are common. */
static u32
rng_operand(void)
{
	return rng_next() >> (rng_next() & 31u);
}

#define RANDOM_PAIRS 4000u

int
main(void)
{
	u32 outer;
	u32 inner;
	u32 pair;

	build_edges();
	for (outer = 0; outer < edge_count; outer++)
		for (inner = 0; inner < edge_count; inner++)
			check_pair(edges[outer], edges[inner]);
	for (pair = 0; pair < RANDOM_PAIRS; pair++) {
		u32 a = rng_operand();
		u32 b = rng_operand();

		check_pair(a, b);
		check_pair(0u - a, b);
		check_pair(a, 0u - b);
		check_pair(0u - a, 0u - b);
	}
	if (model_steps == 0u) {
		put_text("the divider model never ran\n");
		failures++;
	}
	put_text("edge values=");
	put_hex(edge_count);
	put_text("comparisons=");
	put_hex(comparisons);
	put_text("model steps=");
	put_hex(model_steps);
	put_text("zero divisors reaching the divider=");
	put_hex(model_zero_divisors);
	put_text("failures=");
	put_hex(failures);
	put_text("\n");
	if (model_zero_divisors != 0u)
		failures++;
	return failures != 0u;
}
