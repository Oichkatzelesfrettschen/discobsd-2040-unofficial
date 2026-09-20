#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

long db_random(void);
void db_srandom(unsigned seed);
char *db_initstate(unsigned seed, char *state, int size);
char *db_setstate(char *state);

int random_test_errno;

enum { MAX_DEGREE = 63, STATE_BYTES = 256 };

struct reference_state {
	uint32_t words[MAX_DEGREE];
	unsigned degree;
	unsigned separation;
	unsigned front;
	unsigned rear;
};

union state_buffer {
	uint32_t words[STATE_BYTES / sizeof(uint32_t)];
	max_align_t alignment;
};

struct guarded_state {
	uint32_t before;
	union state_buffer buffer;
	uint32_t after;
};

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static uint32_t
reference_next(struct reference_state *reference)
{
	uint32_t value;

	if (reference->degree == 0) {
		reference->words[0] = reference->words[0] * UINT32_C(1103515245)
		    + UINT32_C(12345);
		return reference->words[0] & UINT32_C(0x7fffffff);
	}
	reference->words[reference->front] += reference->words[reference->rear];
	value = reference->words[reference->front] >> 1;
	if (++reference->front == reference->degree)
		reference->front = 0;
	if (++reference->rear == reference->degree)
		reference->rear = 0;
	return value & UINT32_C(0x7fffffff);
}

static void
reference_init(struct reference_state *reference, unsigned seed, unsigned size)
{
	unsigned index;

	if (size < 32) {
		reference->degree = 0;
		reference->separation = 0;
	} else if (size < 64) {
		reference->degree = 7;
		reference->separation = 3;
	} else if (size < 128) {
		reference->degree = 15;
		reference->separation = 1;
	} else if (size < 256) {
		reference->degree = 31;
		reference->separation = 3;
	} else {
		reference->degree = 63;
		reference->separation = 1;
	}
	reference->words[0] = seed;
	for (index = 1; index < reference->degree; index++)
		reference->words[index] = UINT32_C(1103515245)
		    * reference->words[index - 1] + UINT32_C(12345);
	reference->front = reference->separation;
	reference->rear = 0;
	for (index = 0; index < 10 * reference->degree; index++)
		(void)reference_next(reference);
}

static void
check_sequence(unsigned seed, unsigned size)
{
	struct guarded_state guarded = {
		.before = UINT32_C(0x13579bdf),
		.after = UINT32_C(0x2468ace0),
	};
	struct reference_state reference;
	unsigned index;

	reference_init(&reference, seed, size);
	CHECK(db_initstate(seed, (char *)guarded.buffer.words, size) != NULL);
	for (index = 0; index < 96; index++)
		CHECK((uint32_t)db_random() == reference_next(&reference));
	CHECK(guarded.before == UINT32_C(0x13579bdf));
	CHECK(guarded.after == UINT32_C(0x2468ace0));
}

static void
check_switch_and_rejection(void)
{
	union state_buffer first = { { 0 } };
	union state_buffer second = { { 0 } };
	union state_buffer invalid = { { 0 } };
	union state_buffer unaligned = { { 0 } };
	uint32_t negative_size[2] = {
		UINT32_C(0x13579bdf), UINT32_C(0x2468ace0)
	};
	struct reference_state first_reference;
	struct reference_state second_reference;
	char *first_previous;
	char *second_previous;
	unsigned index;

	reference_init(&first_reference, UINT32_C(0x80000000), 128);
	first_previous = db_initstate(UINT32_C(0x80000000),
	    (char *)first.words, 128);
	CHECK(first_previous != NULL);
	for (index = 0; index < 12; index++)
		CHECK((uint32_t)db_random() == reference_next(&first_reference));

	reference_init(&second_reference, UINT32_C(0xffffffff), 64);
	second_previous = db_initstate(UINT32_C(0xffffffff),
	    (char *)second.words, 64);
	CHECK(second_previous == (char *)first.words);
	for (index = 0; index < 9; index++)
		CHECK((uint32_t)db_random() == reference_next(&second_reference));

	CHECK(db_setstate(second_previous) == (char *)second.words);
	CHECK((uint32_t)db_random() == reference_next(&first_reference));
	CHECK(db_setstate((char *)second.words) == (char *)first.words);

	invalid.words[0] = 315;
	random_test_errno = 0;
	CHECK(db_setstate((char *)invalid.words) == NULL);
	CHECK(random_test_errno == EINVAL);
	CHECK((uint32_t)db_random() == reference_next(&second_reference));
	invalid.words[0] = 36;
	random_test_errno = 0;
	CHECK(db_setstate((char *)invalid.words) == NULL);
	CHECK(random_test_errno == EINVAL);
	invalid.words[0] = 5;
	CHECK(db_setstate((char *)invalid.words) == NULL);

	random_test_errno = 0;
	CHECK(db_initstate(1, NULL, 128) == NULL);
	CHECK(random_test_errno == EINVAL);
	random_test_errno = 0;
	CHECK(db_initstate(1, (char *)first.words, 7) == NULL);
	CHECK(random_test_errno == EINVAL);
	random_test_errno = 0;
	CHECK(db_initstate(1, (char *)negative_size, -1) == NULL);
	CHECK(random_test_errno == EINVAL);
	CHECK(negative_size[0] == UINT32_C(0x13579bdf));
	CHECK(negative_size[1] == UINT32_C(0x2468ace0));
	random_test_errno = 0;
	CHECK(db_initstate(1, (char *)unaligned.words + 1, 128) == NULL);
	CHECK(random_test_errno == EINVAL);
	CHECK((uint32_t)db_random() == reference_next(&second_reference));
}

int
main(void)
{
	static const unsigned sizes[] = { 8, 31, 32, 63, 64, 127, 128, 255, 256 };
	static const unsigned seeds[] = { 0, 1, UINT32_C(0x80000000),
	    UINT32_C(0xffffffff) };
	unsigned seed_index;
	unsigned size_index;

	for (seed_index = 0; seed_index < sizeof(seeds) / sizeof(seeds[0]);
	    seed_index++)
		for (size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]);
		    size_index++)
			check_sequence(seeds[seed_index], sizes[size_index]);
	check_switch_and_rejection();
	if (failures != 0) {
		fprintf(stderr, "random contract: %d failure(s)\n", failures);
		return 1;
	}
	puts("random contract passed");
	return 0;
}
