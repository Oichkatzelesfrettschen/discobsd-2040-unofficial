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
#include <limits.h>
size_t search_comparisons;
void *tree_memchr(const void *, int, size_t);
int byte_compare(const void *, const void *, size_t);
void *byte_find(const void *, int, size_t);

int byte_compare(const void *av, const void *bv, size_t n)
{
    const unsigned char *a = av, *b = bv;
    for (size_t i = 0; i < n; i++) {
        search_comparisons++;
        if (a[i] != b[i]) return (a[i] > b[i]) - (a[i] < b[i]);
    }
    return 0;
}
void *byte_find(const void *v, int c, size_t n)
{
    const unsigned char *s = v;
    for (size_t i = 0; i < n; i++) {
        search_comparisons++;
        if (s[i] == (unsigned char)c) return (void *)(s+i);
    }
    return NULL;
}
static int check_memchr(void)
{
    unsigned char data[256];
    size_t checks = 0;
    for (size_t i = 0; i < sizeof data; i++) data[i] = (unsigned char)i;
    for (int c = -512; c <= 511; c++) {
        for (size_t n = 0; n <= sizeof data; n++) {
            const unsigned char *want = (unsigned char)c < n ?
                data + (unsigned char)c : NULL;
            if (tree_memchr(data, c, n) != want) {
                fprintf(stderr, "byte conversion: c=%d n=%zu\n", c, n);
                return 5;
            }
            checks++;
        }
    }
    const int extremes[] = { INT_MIN, INT_MAX };
    for (size_t i = 0; i < sizeof extremes / sizeof extremes[0]; i++) {
        int c = extremes[i];
        if (tree_memchr(data, c, sizeof data) != data + (unsigned char)c) {
            fputs("byte conversion: extreme int\n", stderr);
            return 5;
        }
        checks++;
    }
#ifdef GUARD_PAGES
    long page_value = sysconf(_SC_PAGESIZE);
    if (page_value < 256) return 4;
    size_t page = (size_t)page_value;
    unsigned char *map = mmap(NULL, 2*page, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED || mprotect(map+page, page, PROT_NONE)) return 4;
    for (size_t n = 0; n <= 256; n++) {
        unsigned char *p = map + page - n;
        memset(p, 0x80, n);
        if (tree_memchr(p, 0x1ff, n) != NULL ||
            tree_memchr(p, 0x180, n) != (n ? p : NULL)) {
            fputs("byte conversion: protected extent\n", stderr);
            return 5;
        }
        checks++;
    }
    if (munmap(map, 2*page)) return 4;
#endif
    printf("memchr: %zu conversion and extent cases pass\n", checks);
    return 0;
}
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
    if (search_comparisons > 2 * n + 7 * m) {
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
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "memchr") == 0) return check_memchr();
    if (argc == 1) { int status = check_memchr(); if (status) return status; }
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
    printf("byte search: %zu earliest-match and comparison-bound cases pass; maximum count %zu\n", cases, maximum_count);
    return 0;
}
