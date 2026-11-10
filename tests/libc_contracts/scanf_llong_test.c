/*
 * Contract gate for the 64-bit integer directives of _doscan: an integer
 * conversion or %n under ll, j or q, which doscan.c hands to __doscan_ll
 * in doscan_llong.c.
 *
 * The scanner and the member are the tree's, compiled against the tree's
 * headers with the entry points renamed as scanf_contract_test.c describes.
 * strtoll and strtoull are not renamed, so they resolve to the host C
 * library and serve as the oracle for the generated cases: the two agree
 * with C17 7.21.6.2p12 on the subject sequence of every shape generated
 * here, and they saturate where the scanner saturates.
 *
 * Built with SCANF_LLONG_ABSENT the same file links without the member
 * and holds every such directive to a matching failure that leaves its
 * destination alone.
 */

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The tree's stdio references the table; the gate never opens a real file. */
struct _iobuf _iob[3];

/* The tree strtod reports overflow through errno, renamed away from the host's. */
int scanf_test_errno;

int db_sscanf(const char *, const char *, ...);

int
fflush(FILE *stream)
{
	(void)stream;
	return 0;
}

static unsigned long checks;
static int failures;

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		(void)write(2, message, strlen(message));
		(void)write(2, "\n", 1);
	}
}

/*
 * A 64-bit destination between two words the scanner must not touch. A
 * store narrower than the destination leaves part of FILL in value, and
 * a store misplaced by a word changes a guard.
 */
#define GUARD	0x5aa55aa5U
#define FILL	0x6b6b6b6b6b6b6b6bULL

struct guarded {
	unsigned int		before;
	unsigned long long	value;
	unsigned int		after;
};

static void
arm(struct guarded *slot)
{
	slot->before = GUARD;
	slot->value = FILL;
	slot->after = GUARD;
}

static int
intact(const struct guarded *slot)
{
	return slot->before == GUARD && slot->after == GUARD;
}

#ifdef SCANF_LLONG_ABSENT

int
main(void)
{
	static const char *const formats[] = {
		"%lld", "%lli", "%llo", "%llu", "%llx", "%llX",
		"%jd", "%ju", "%qd", "%5lld", "%*lld",
	};
	struct guarded slot;
	size_t i;
	long lv;
	int iv, r;

	for (i = 0; i < sizeof formats / sizeof formats[0]; i++) {
		arm(&slot);
		r = db_sscanf("42", formats[i], &slot.value);
		check(r == 0, "a 64-bit directive converts without its member");
		check(slot.value == FILL && intact(&slot),
		    "a 64-bit directive stores without its member");
	}

	/* The directives before it still count, and it consumes nothing. */
	arm(&slot);
	iv = 0;
	lv = 0;
	r = db_sscanf("7 42", "%d %lld", &iv, &slot.value);
	check(r == 1 && iv == 7 && slot.value == FILL,
	    "a 64-bit directive without its member hides earlier conversions");
	r = db_sscanf("7 42", "%d %ld", &iv, &lv);
	check(r == 2 && lv == 42, "%ld depends on the 64-bit member");

	arm(&slot);
	r = db_sscanf("42", "%d%lln", &iv, &slot.value);
	check(r == 1 && iv == 42 && slot.value == FILL && intact(&slot),
	    "%lln stores without its member");

	if (failures == 0) {
		(void)write(1, "scanf llong absent: pass\n", 25);
		return 0;
	}
	(void)write(2, "scanf llong absent: fail\n", 25);
	return 1;
}

#else /* !SCANF_LLONG_ABSENT */

static unsigned int rng_state = 0x2545f491U;

static unsigned int
rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/*
 * Write a subject sequence in the given base, with optional leading white
 * space, sign and prefix, followed by a character outside every base.
 * The digit count reaches past the 22 an unsigned long long needs in
 * octal, so some cases saturate. A 0x prefix is always followed by a
 * hexadecimal digit: without one the scanner's single pushback leaves it
 * after the x, which the fixed cases below hold separately.
 */
static void
make_subject(char *text, int base, int prefixed)
{
	static const char digits[] = "0123456789abcdefABCDEF";
	int count, i, digit;

	if (rng() % 4 == 0)
		*text++ = " \t\n"[rng() % 3];
	switch (rng() % 4) {
	case 0:
		*text++ = '-';
		break;
	case 1:
		*text++ = '+';
		break;
	default:
		break;
	}
	if (prefixed) {
		if (base == 16) {
			*text++ = '0';
			*text++ = (rng() & 1) ? 'x' : 'X';
		} else if (base == 8) {
			*text++ = '0';
		}
	}
	count = 1 + (int)(rng() % 24);
	for (i = 0; i < count; i++) {
		if (base == 16)
			digit = (int)(rng() % 22);
		else
			digit = (int)(rng() % (unsigned int)base);
		/* A decimal under %i must not begin like an octal. */
		if (i == 0 && base == 10 && prefixed)
			digit = 1 + (int)(rng() % 9);
		*text++ = digits[digit];
	}
	*text++ = "!,z "[rng() % 4];
	*text = '\0';
}

static void
sweep(const char *format, int base, int issigned, int automatic)
{
	char format_n[16];
	char text[48];
	struct guarded slot;
	unsigned long long want;
	char *end;
	int i, n, r, subject_base;

	(void)strcpy(format_n, format);
	(void)strcat(format_n, "%n");
	for (i = 0; i < 20000; i++) {
		subject_base = base;
		if (automatic)
			subject_base = (rng() % 3 == 0) ? 8 :
			    (rng() % 2 == 0) ? 10 : 16;
		make_subject(text, subject_base,
		    automatic || (base == 16 && (rng() & 1)));
		if (issigned)
			want = (unsigned long long)strtoll(text, &end, base);
		else
			want = strtoull(text, &end, base);
		arm(&slot);
		n = -1;
		r = db_sscanf(text, format_n, &slot.value, &n);
		check(r == 1, "a generated subject does not convert");
		check(slot.value == want,
		    "a generated subject converts to another value than "
		    "the host library's");
		check(n == (int)(end - text),
		    "a generated subject consumes another extent than the "
		    "host library's");
		check(intact(&slot), "a 64-bit store leaves its destination");
	}
}

int
main(void)
{
	struct guarded slot, second;
	long long llv;
	unsigned long long ullv;
	intmax_t jv;
	uintmax_t ujv;
	struct {
		unsigned int	before;
		void		*pointer;
		unsigned int	after;
	} cell;
	int iv, n, r;

	/*
	 * The defect the member removes: the word accumulator stored the
	 * low word of the destination and left the rest as it found it.
	 */
	arm(&slot);
	r = db_sscanf("5", "%lld", &slot.value);
	check(r == 1 && slot.value == 5 && intact(&slot),
	    "%lld leaves the high word of its destination unwritten");

	llv = 0;
	r = db_sscanf("9223372036854775807", "%lld", &llv);
	check(r == 1 && llv == LLONG_MAX, "%lld does not reach LLONG_MAX");
	llv = 0;
	r = db_sscanf("-9223372036854775808", "%lld", &llv);
	check(r == 1 && llv == LLONG_MIN, "%lld does not reach LLONG_MIN");
	llv = 0;
	r = db_sscanf("9223372036854775808", "%lld", &llv);
	check(r == 1 && llv == LLONG_MAX,
	    "%lld does not saturate one above LLONG_MAX");
	llv = 0;
	r = db_sscanf("-9223372036854775809", "%lld", &llv);
	check(r == 1 && llv == LLONG_MIN,
	    "%lld does not saturate one below LLONG_MIN");
	llv = 0;
	r = db_sscanf("4294967296", "%lld", &llv);
	check(r == 1 && llv == 4294967296LL,
	    "%lld does not carry out of the low word");

	ullv = 0;
	r = db_sscanf("18446744073709551615", "%llu", &ullv);
	check(r == 1 && ullv == ULLONG_MAX, "%llu does not reach ULLONG_MAX");
	ullv = 0;
	r = db_sscanf("18446744073709551616", "%llu", &ullv);
	check(r == 1 && ullv == ULLONG_MAX,
	    "%llu does not saturate one above ULLONG_MAX");
	ullv = 0;
	r = db_sscanf("99999999999999999999999999999999", "%llu%n", &ullv, &n);
	check(r == 1 && ullv == ULLONG_MAX && n == 32,
	    "%llu does not consume the digits past its saturation");
	ullv = 0;
	r = db_sscanf("-1", "%llu", &ullv);
	check(r == 1 && ullv == ULLONG_MAX,
	    "%llu does not negate in the unsigned type");
	ullv = 0;
	r = db_sscanf("FFFFffffFFFFffff", "%llx", &ullv);
	check(r == 1 && ullv == ULLONG_MAX, "%llx does not convert 16 digits");
	ullv = 0;
	r = db_sscanf("0x10000000000", "%llX", &ullv);
	check(r == 1 && ullv == 0x10000000000ULL,
	    "%llX does not accept a 0x prefix");
	ullv = 0;
	r = db_sscanf("1777777777777777777777", "%llo", &ullv);
	check(r == 1 && ullv == ULLONG_MAX, "%llo does not convert 22 digits");

	/* %i selects its base from the prefix. */
	llv = 0;
	r = db_sscanf("0x7fffffffffffffff", "%lli", &llv);
	check(r == 1 && llv == LLONG_MAX, "%lli does not select base 16");
	llv = 0;
	r = db_sscanf("-0777777777777", "%lli", &llv);
	check(r == 1 && llv == -0777777777777LL, "%lli does not select base 8");
	llv = 0;
	r = db_sscanf("123456789012", "%lli", &llv);
	check(r == 1 && llv == 123456789012LL, "%lli does not select base 10");

	/* The other spellings of a 64-bit destination. */
	jv = 0;
	r = db_sscanf("-1234567890123", "%jd", &jv);
	check(r == 1 && jv == -1234567890123LL, "%jd does not store an intmax_t");
	ujv = 0;
	r = db_sscanf("1234567890123", "%ju", &ujv);
	check(r == 1 && ujv == 1234567890123ULL,
	    "%ju does not store a uintmax_t");
	llv = 0;
	r = db_sscanf("-1234567890123", "%qd", &llv);
	check(r == 1 && llv == -1234567890123LL, "%qd does not store a quad");

	/* Width, white space, suppression and the directives around it. */
	arm(&slot);
	arm(&second);
	r = db_sscanf("12345678901234567890", "%10lld%lld",
	    &slot.value, &second.value);
	check(r == 2 && slot.value == 1234567890ULL &&
	    second.value == 1234567890ULL && intact(&slot) && intact(&second),
	    "a field width does not bound %lld");
	llv = 0;
	r = db_sscanf("-5", "%1lld", &llv);
	check(r == 0 && llv == 0, "a width that admits only a sign converts");
	llv = 0;
	n = -1;
	r = db_sscanf(" \t\n 77 ", "%lld%n", &llv, &n);
	check(r == 1 && llv == 77 && n == 6,
	    "%lld does not skip leading white space outside its width");
	llv = 0;
	r = db_sscanf("   77", "%2lld", &llv);
	check(r == 1 && llv == 77, "leading white space spends the field width");
	iv = 0;
	r = db_sscanf("5000000000 6", "%*lld %d", &iv);
	check(r == 1 && iv == 6, "a suppressed %lld takes an argument");
	llv = 0;
	iv = 0;
	r = db_sscanf("3,5000000000,4", "%d,%lld,%d", &n, &llv, &iv);
	check(r == 3 && n == 3 && llv == 5000000000LL && iv == 4,
	    "%lld does not hand the stream back to the next directive");

	/* %n under ll: all 64 bits, and outside the returned count. */
	arm(&slot);
	iv = 0;
	r = db_sscanf("1234 ", "%d%lln", &iv, &slot.value);
	check(r == 1 && iv == 1234 && slot.value == 4 && intact(&slot),
	    "%lln does not store the count in a long long");
	arm(&slot);
	r = db_sscanf("1234", "%d%*lln", &iv, &slot.value);
	check(r == 1 && slot.value == FILL, "a suppressed %lln stores");

	/* Failures: nothing stored, and the verdict C17 7.21.6.2p16 names. */
	arm(&slot);
	r = db_sscanf("", "%lld", &slot.value);
	check(r == EOF && slot.value == FILL,
	    "%lld on empty input is not an input failure");
	arm(&slot);
	r = db_sscanf("   ", "%lld", &slot.value);
	check(r == EOF && slot.value == FILL,
	    "%lld on white space alone is not an input failure");
	arm(&slot);
	r = db_sscanf("x", "%lld", &slot.value);
	check(r == 0 && slot.value == FILL,
	    "%lld on a non-digit is not a matching failure");
	arm(&slot);
	r = db_sscanf("-x", "%lld", &slot.value);
	check(r == 0 && slot.value == FILL,
	    "%lld on a lone sign is not a matching failure");
	iv = 0;
	arm(&slot);
	r = db_sscanf("8 ", "%d %lld", &iv, &slot.value);
	check(r == 1 && iv == 8 && slot.value == FILL,
	    "an input failure after a conversion hides the count");

	/*
	 * 0x with no hexadecimal digit: the zero converts and, with one
	 * pushback, the x stays consumed, as in the word conversion.
	 */
	llv = -1;
	n = -1;
	r = db_sscanf("0xg", "%lli%n", &llv, &n);
	check(r == 1 && llv == 0 && n == 2,
	    "0x with no digit does not convert zero");

	/* A pointer destination keeps its own width under a stray ll. */
	cell.before = GUARD;
	cell.pointer = NULL;
	cell.after = GUARD;
	r = db_sscanf("1f", "%llp", &cell.pointer);
	check(r == 1 && cell.pointer == (void *)0x1f &&
	    cell.before == GUARD && cell.after == GUARD,
	    "%llp stores past a pointer");

	sweep("%lld", 10, 1, 0);
	sweep("%llu", 10, 0, 0);
	sweep("%llo", 8, 0, 0);
	sweep("%llx", 16, 0, 0);
	sweep("%lli", 0, 1, 1);
	sweep("%jd", 10, 1, 0);
	sweep("%qx", 16, 0, 0);

	/* The <inttypes.h> spellings reach the same member. */
	llv = 0;
	r = db_sscanf("-9223372036854775808", "%" SCNd64, &llv);
	check(r == 1 && llv == LLONG_MIN, "SCNd64 does not scan an int64_t");
	ujv = 0;
	r = db_sscanf("ffffffffffffffff", "%" SCNxMAX, &ujv);
	check(r == 1 && ujv == UINTMAX_MAX, "SCNxMAX does not scan a uintmax_t");

	if (failures == 0) {
		(void)write(1, "scanf llong contracts: pass\n", 28);
		return 0;
	}
	(void)write(2, "scanf llong contracts: fail\n", 28);
	return 1;
}

#endif /* !SCANF_LLONG_ABSENT */
