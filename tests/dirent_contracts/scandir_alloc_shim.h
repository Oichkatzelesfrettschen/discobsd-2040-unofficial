#ifndef SCANDIR_ALLOC_SHIM_H
#define SCANDIR_ALLOC_SHIM_H

void scandir_alloc_reset(unsigned, int, int);
unsigned scandir_alloc_calls(void);
unsigned scandir_alloc_invalid_frees(void);
unsigned scandir_alloc_live_count(void);
unsigned scandir_alloc_live_bytes(void);
unsigned scandir_alloc_peak_bytes(void);
unsigned scandir_alloc_request_count(void);
unsigned scandir_alloc_request_size(unsigned);
int scandir_alloc_request_is_realloc(unsigned);
unsigned scandir_allocation_size(const void *);

void *test_malloc(unsigned);
void *test_realloc(void *, unsigned);
void test_free(void *);

#endif
