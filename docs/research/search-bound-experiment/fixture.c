#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GUARD_PAGES
#define _GNU_SOURCE 1
#include <sys/mman.h>
#include <unistd.h>
#endif
extern size_t search_comparisons;
void *candidate_memmem(const void *, size_t, const void *, size_t);
static size_t cases;
static size_t maximum_count;
static void check(const unsigned char *h, size_t n, const unsigned char *p, size_t m)
{
    const unsigned char *want = NULL;
    if (m <= n) {
        for (size_t i = 0; i <= n - m; ++i) {
            size_t j = 0;
            while (j < m && h[i + j] == p[j]) ++j;
            if (j == m) { want = h + i; break; }
        }
    }
    search_comparisons = 0;
    const unsigned char *got = candidate_memmem(h, n, p, m);
    if (got != want) {
        fprintf(stderr, "oracle: n=%zu m=%zu got=%td want=%td\n", n, m,
            got ? got - h : -1, want ? want - h : -1);
        exit(1);
    }
    if (search_comparisons > 4 * n + 8 * m) {
        fprintf(stderr, "comparison bound: n=%zu m=%zu count=%zu\n", n, m, search_comparisons);
        exit(2);
    }
    if (m && m <= n && !search_comparisons) {
        fputs("counter: valid nonempty search records zero\n", stderr);
        exit(3);
    }
    if (search_comparisons > maximum_count) maximum_count = search_comparisons;
    ++cases;
}
static uint32_t state = 1;
static unsigned char next_byte(void)
{
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    return (unsigned char)(state >> 24);
}
int main(void)
{
    unsigned char hay[4096], needle[256];
    for (size_t n = 0; n <= 12; ++n) {
        for (size_t hw = 0; hw < ((size_t)1 << n); ++hw) {
            for (size_t i = 0; i < n; ++i) hay[i] = (unsigned char)((hw >> i) & 1);
            for (size_t m = 0; m <= 8; ++m) {
                for (size_t pw = 0; pw < ((size_t)1 << m); ++pw) {
                    for (size_t i = 0; i < m; ++i) needle[i] = (unsigned char)((pw >> i) & 1);
                    check(hay, n, needle, m);
                }
            }
        }
    }
    for (size_t sample = 0; sample < 20000; ++sample) {
        size_t n = ((size_t)next_byte() << 4) | (next_byte() & 15);
        size_t m = next_byte();
        for (size_t i = 0; i < n; ++i) hay[i] = next_byte();
        for (size_t i = 0; i < m; ++i) needle[i] = next_byte();
        if ((sample & 1) && m <= n) memcpy(hay + (n-m)/2, needle, m);
        check(hay, n, needle, m);
    }
    for (size_t n = 32; n <= sizeof hay; n *= 2) {
        for (size_t m = 2; m <= sizeof needle; m *= 2) {
            for (size_t pattern = 0; pattern < 8; ++pattern) {
                for (size_t i = 0; i < n; ++i) hay[i] = pattern & 1 ? (unsigned char)(i % 2) : 0;
                for (size_t i = 0; i < m; ++i) needle[i] = pattern & 1 ? (unsigned char)(i % 2) : 0;
                if (pattern & 2) needle[m-1] = 2;
                if (pattern & 4) needle[0] = 2;
                check(hay,n,needle,m);
            }
        }
    }
#ifdef GUARD_PAGES
    long page_value = sysconf(_SC_PAGESIZE);
    if (page_value < 256) return 4;
    size_t page = (size_t)page_value;
    unsigned char *maps[2];
    for (size_t i = 0; i < 2; ++i) {
        maps[i] = mmap(NULL, 2*page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (maps[i] == MAP_FAILED || mprotect(maps[i] + page, page, PROT_NONE)) return 4;
    }
    for (size_t n = 0; n <= 128; ++n) {
        for (size_t m = 0; m <= 128; ++m) {
            unsigned char *h = maps[0] + page - n;
            unsigned char *p = maps[1] + page - m;
            memset(h, 0x80, n); memset(p, 0x80, m);
            check(h,n,p,m);
            if (m) { p[m-1] = 0xff; check(h,n,p,m); }
        }
    }
    for (size_t i = 0; i < 2; ++i) if (munmap(maps[i],2*page)) return 4;
#endif
    printf("tableless search: %zu earliest-match and comparison-bound cases pass; maximum count %zu\n", cases, maximum_count);
    return 0;
}
