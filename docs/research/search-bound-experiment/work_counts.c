#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern size_t search_comparisons;
void *candidate_memmem(const void *, size_t, const void *, size_t);
void *baseline_memmem(const void *, size_t, const void *, size_t);
static size_t baseline_comparisons;
int baseline_memcmp(const void *av, const void *bv, size_t n)
{
    const unsigned char *a = av, *b = bv;
    for (size_t i=0; i<n; ++i) {
        ++baseline_comparisons;
        if (a[i]!=b[i]) return (a[i]>b[i])-(a[i]<b[i]);
    }
    return 0;
}
void *baseline_memchr(const void *pv, int c, size_t n)
{
    const unsigned char *p = pv;
    for (size_t i=0; i<n; ++i) {
        ++baseline_comparisons;
        if (p[i] == (unsigned char)c) return (void *)(p+i);
    }
    return NULL;
}
int main(void)
{
    unsigned char h[8192], p[512];
    memset(h,0,sizeof h);
    for (size_t n=256;n<=sizeof h;n*=2) {
        size_t m=n/16;
        memset(p,0,m); p[m-1]=1;
        search_comparisons=baseline_comparisons=0;
        if (candidate_memmem(h,n,p,m) || baseline_memmem(h,n,p,m)) return 1;
        printf("%zu %zu %zu %zu\n",n,m,baseline_comparisons,search_comparisons);
    }
    return 0;
}
