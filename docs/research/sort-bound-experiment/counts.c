#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
void db_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
static size_t calls;
static int cmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    calls++;
    return (x > y) - (x < y);
}
int main(void)
{
    int values[4096];
    unsigned before[4096], after[4096];
    for (unsigned mode = 0; mode < 7; mode++)
    {
        for (size_t n = 32; n <= 4096; n *= 2)
        {
            for (size_t i = 0; i < n; i++)
                before[i] = after[i] = 0;
            for (size_t i = 0; i < n; i++)
            {
                values[i] = mode == 0   ? 0
                            : mode == 1 ? (int)i
                            : mode == 2 ? (int)(n - 1 - i)
                            : mode == 3 ? (int)(i % 2)
                            : mode == 4 ? (int)(i < n / 2 ? i : n - 1 - i)
                            : mode == 5 ? (int)((i * 73 + i / 13) % n)
                                        : (int)(i % 5);
                before[values[i]]++;
            }
            calls = 0;
            db_qsort(values, n, sizeof values[0], cmp);
            for (size_t i = 0; i < n; i++)
            {
                if (values[i] < 0 || (size_t)values[i] >= n ||
                    (i && values[i - 1] > values[i]))
                    return 1;
                after[values[i]]++;
            }
            for (size_t i = 0; i < n; i++)
                if (before[i] != after[i])
                    return 2;
            if (calls < n - 1)
            {
                fprintf(stderr, "comparison counter: insufficient calls\n");
                return 3;
            }
            unsigned lg = 0;
            for (size_t v = n; v > 1; v /= 2)
                lg++;
            if (calls > 8 * n * (lg + 1) + 64 * n)
            {
                fprintf(stderr, "comparison bound: mode=%u n=%zu calls=%zu\n", mode, n,
                        calls);
                return 4;
            }
            printf("%u %zu %zu\n", mode, n, calls);
        }
    }
    printf("qsort counts: 56 pattern cases pass\n");
    return 0;
}
