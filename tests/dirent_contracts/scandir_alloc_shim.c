#include "scandir_alloc_shim.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ALLOCATIONS 128
#define MAX_REQUESTS 256

struct tracked_allocation {
	void *pointer;
	size_t size;
	int live;
};

struct allocation_request {
	unsigned size;
	int reallocation;
};

static struct tracked_allocation allocations[MAX_ALLOCATIONS];
static struct allocation_request requests[MAX_REQUESTS];
static unsigned allocation_calls;
static unsigned failure_ordinal;
static unsigned invalid_frees;
static unsigned live_bytes;
static unsigned live_count;
static unsigned peak_bytes;
static unsigned request_count;
static int allocation_failure_errno;
static int free_errno;

extern int dirent_test_errno;

static struct tracked_allocation *
find_allocation(const void *pointer)
{
	unsigned allocation_index;

	for (allocation_index = 0; allocation_index < MAX_ALLOCATIONS;
	    ++allocation_index) {
		if (allocations[allocation_index].live &&
		    allocations[allocation_index].pointer == pointer)
			return &allocations[allocation_index];
	}
	return NULL;
}

static struct tracked_allocation *
find_free_slot(void)
{
	unsigned allocation_index;

	for (allocation_index = 0; allocation_index < MAX_ALLOCATIONS;
	    ++allocation_index) {
		if (!allocations[allocation_index].live)
			return &allocations[allocation_index];
	}
	return NULL;
}

static void
record_request(unsigned size, int reallocation)
{
	if (request_count < MAX_REQUESTS) {
		requests[request_count].size = size;
		requests[request_count].reallocation = reallocation;
	}
	++request_count;
}

static int
should_fail(void)
{
	++allocation_calls;
	if (failure_ordinal != 0 && allocation_calls == failure_ordinal) {
		dirent_test_errno = allocation_failure_errno;
		return 1;
	}
	return 0;
}

void
scandir_alloc_reset(unsigned fail_at, int fail_errno, int cleanup_errno)
{
	unsigned allocation_index;

	for (allocation_index = 0; allocation_index < MAX_ALLOCATIONS;
	    ++allocation_index) {
		if (allocations[allocation_index].live)
			free(allocations[allocation_index].pointer);
		allocations[allocation_index].pointer = NULL;
		allocations[allocation_index].size = 0;
		allocations[allocation_index].live = 0;
	}
	memset(requests, 0, sizeof(requests));
	allocation_calls = 0;
	failure_ordinal = fail_at;
	invalid_frees = 0;
	live_bytes = 0;
	live_count = 0;
	peak_bytes = 0;
	request_count = 0;
	allocation_failure_errno = fail_errno;
	free_errno = cleanup_errno;
}

void *
test_malloc(unsigned size)
{
	struct tracked_allocation *slot;
	void *pointer;

	record_request(size, 0);
	if (should_fail() || size == 0) {
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	slot = find_free_slot();
	if (slot == NULL) {
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	pointer = malloc((size_t)size);
	if (pointer == NULL) {
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	slot->pointer = pointer;
	slot->size = size;
	slot->live = 1;
	live_bytes += size;
	++live_count;
	if (live_bytes > peak_bytes)
		peak_bytes = live_bytes;
	return pointer;
}

void *
test_realloc(void *pointer, unsigned size)
{
	struct tracked_allocation *slot;
	void *replacement;
	size_t copy_size;

	record_request(size, 1);
	if (should_fail() || size == 0) {
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	slot = find_allocation(pointer);
	if (slot == NULL) {
		++invalid_frees;
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	replacement = malloc((size_t)size);
	if (replacement == NULL) {
		dirent_test_errno = allocation_failure_errno;
		return NULL;
	}
	if (live_bytes + size > peak_bytes)
		peak_bytes = live_bytes + size;
	copy_size = slot->size < (size_t)size ? slot->size : (size_t)size;
	memcpy(replacement, pointer, copy_size);
	free(pointer);
	live_bytes -= (unsigned)slot->size;
	live_bytes += size;
	slot->pointer = replacement;
	slot->size = size;
	return replacement;
}

void
test_free(void *pointer)
{
	struct tracked_allocation *slot;

	if (pointer == NULL)
		return;
	slot = find_allocation(pointer);
	if (slot == NULL) {
		++invalid_frees;
		return;
	}
	free(pointer);
	live_bytes -= (unsigned)slot->size;
	--live_count;
	slot->pointer = NULL;
	slot->size = 0;
	slot->live = 0;
	if (free_errno != 0)
		dirent_test_errno = free_errno;
}

unsigned
scandir_alloc_calls(void)
{
	return allocation_calls;
}

unsigned
scandir_alloc_invalid_frees(void)
{
	return invalid_frees;
}

unsigned
scandir_alloc_live_count(void)
{
	return live_count;
}

unsigned
scandir_alloc_live_bytes(void)
{
	return live_bytes;
}

unsigned
scandir_alloc_peak_bytes(void)
{
	return peak_bytes;
}

unsigned
scandir_alloc_request_count(void)
{
	return request_count;
}

unsigned
scandir_alloc_request_size(unsigned request_index)
{
	if (request_index >= request_count || request_index >= MAX_REQUESTS)
		return 0;
	return requests[request_index].size;
}

int
scandir_alloc_request_is_realloc(unsigned request_index)
{
	if (request_index >= request_count || request_index >= MAX_REQUESTS)
		return 0;
	return requests[request_index].reallocation;
}

unsigned
scandir_allocation_size(const void *pointer)
{
	struct tracked_allocation *slot;

	slot = find_allocation(pointer);
	return slot == NULL ? 0 : (unsigned)slot->size;
}
