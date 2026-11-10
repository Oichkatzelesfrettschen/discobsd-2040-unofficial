/*
 * lib/libc/gen/aligned_alloc.c over the tree's arena allocator, both under
 * db_ names, with the break owned by arena_host.c.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void *db_malloc(size_t);
void db_free(void *);
void *db_realloc(void *, size_t);
void *db_aligned_alloc(size_t, size_t);
void arena_install(char *, size_t);
size_t arena_break(void);

#define	ARENA_BYTES	(256 * 1024)

static unsigned long checks;
static unsigned long failures;

static void
expect(int condition, const char *contract)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "aligned_alloc test: %s\n", contract);
	}
}

static const size_t alignments[] = {
	1, 2, 4, 8, 16, 32, 64, 128, 256, 1024, 4096
};
static const size_t sizes[] = { 1, 3, 4, 7, 8, 9, 24, 100, 257, 1000 };
#define	COUNT(array)	(sizeof(array) / sizeof((array)[0]))
#define	BLOCKS		(COUNT(alignments) * COUNT(sizes))

static int
filled(const unsigned char *block, size_t size, unsigned char value)
{
	size_t index;

	for (index = 0; index < size; index++)
		if (block[index] != value)
			return 0;
	return 1;
}

static void
arguments(void)
{
	static const size_t invalid[] = { 0, 3, 6, 12, 24, 4097 };
	size_t index;
	void *block;

	for (index = 0; index < COUNT(invalid); index++) {
		errno = 0;
		expect(db_aligned_alloc(invalid[index], 64) == NULL &&
		    errno == EINVAL, "an alignment that is no power of two "
		    "is NULL with EINVAL");
	}
	errno = 0;
	expect(db_aligned_alloc(64, 0) == NULL && errno == 0,
	    "a zero size is NULL with errno untouched");
	errno = 0;
	expect(db_aligned_alloc(4096, SIZE_MAX - 100) == NULL &&
	    errno == ENOMEM, "a size the alignment overflows is ENOMEM");
	errno = 0;
	expect(db_aligned_alloc(64, SIZE_MAX / 2) == NULL && errno == ENOMEM,
	    "a size beyond the arena is ENOMEM");
	block = db_aligned_alloc(64, 40);
	expect(block != NULL, "a request after refusals succeeds");
	db_free(block);
}

/*
 * Every alignment with every size, all live at once and each filled with
 * its own byte, so an overlap between an aligned block, the lead-in in
 * front of it or the tail split off behind it changes a neighbor.
 */
static void
placement(int reverse)
{
	unsigned char *blocks[BLOCKS];
	size_t alignment, size, index;

	index = 0;
	for (alignment = 0; alignment < COUNT(alignments); alignment++) {
		for (size = 0; size < COUNT(sizes); size++, index++) {
			blocks[index] = db_aligned_alloc(alignments[alignment],
			    sizes[size]);
			expect(blocks[index] != NULL, "allocation succeeds");
			if (blocks[index] == NULL)
				continue;
			expect(((uintptr_t)blocks[index] &
			    (alignments[alignment] - 1)) == 0,
			    "the address is a multiple of the alignment");
			memset(blocks[index], (int)(index + 1), sizes[size]);
		}
	}
	for (index = 0; index < BLOCKS; index++)
		expect(blocks[index] != NULL && filled(blocks[index],
		    sizes[index % COUNT(sizes)], (unsigned char)(index + 1)),
		    "a block keeps its bytes while its neighbors are written");
	for (index = 0; index < BLOCKS; index++)
		db_free(blocks[reverse ? BLOCKS - 1 - index : index]);
}

/* realloc() grows an aligned block as it grows any other arena block. */
static void
resize(void)
{
	unsigned char *block, *grown;

	block = db_aligned_alloc(256, 100);
	expect(block != NULL, "aligned block for resize");
	if (block == NULL)
		return;
	memset(block, 0x5a, 100);
	grown = db_realloc(block, 5000);
	expect(grown != NULL && filled(grown, 100, 0x5a),
	    "realloc keeps the bytes of an aligned block");
	db_free(grown);
}

#define	SETTLE_ROUNDS	128
#define	STABLE_ROUNDS	64

static void
rounds(int count)
{
	int round;

	for (round = 0; round < count; round++) {
		placement(round & 1);
		resize();
	}
}

/*
 * The break is the leak measurement.  A roving first-fit search packs the
 * same requests differently from one round to the next, so the break may
 * rise during the first rounds with no block lost; malloc() alone does the
 * same.  It then holds, while a lead-in or tail block that stayed busy
 * would raise it on every round.  Once every block is free the arena has
 * to coalesce, so one request for all of it but the two header words of
 * slack is served without moving the break.
 */
int
main(void)
{
	char storage[ARENA_BYTES];
	size_t settled;
	void *whole;

	arena_install(storage, sizeof(storage));
	arguments();
	rounds(SETTLE_ROUNDS);
	settled = arena_break();
	expect(settled != 0, "the allocator took arena bytes");
	rounds(STABLE_ROUNDS);
	expect(arena_break() == settled,
	    "settled rounds reuse the arena without moving the break");
	whole = db_malloc(settled - 64);
	expect(whole != NULL && arena_break() == settled,
	    "the freed arena coalesces into one block");
	db_free(whole);
	if (failures != 0) {
		fprintf(stderr, "aligned_alloc test: %lu of %lu checks failed\n",
		    failures, checks);
		return 1;
	}
	printf("aligned_alloc test: %lu checks passed, break %lu bytes\n",
	    checks, (unsigned long)settled);
	return 0;
}
