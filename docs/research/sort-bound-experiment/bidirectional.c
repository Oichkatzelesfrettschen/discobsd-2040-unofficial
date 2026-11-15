#include <stddef.h>
#ifdef CONTEXT
typedef int (*comparator)(const void *, const void *, void *);
#define CMP(a, b) compare(a, b, context)
#else
typedef int (*comparator)(const void *, const void *);
#define CMP(a, b) compare(a, b)
#endif
static void swap(unsigned char *a, unsigned char *b, size_t width)
{
    while (width--)
    {
        unsigned char byte = *a;
        *a++ = *b;
        *b++ = byte;
    }
}
static void sift(unsigned char *base, size_t count, size_t width, size_t root,
                 comparator compare, void *context)
{
#ifndef CONTEXT
    (void)context;
#endif
    while (root < count / 2)
    {
        size_t child = 2 * root + 1;
        if (child + 1 < count &&
            CMP(base + child * width, base + (child + 1) * width) < 0)
            child++;
        if (CMP(base + root * width, base + child * width) >= 0)
            return;
        swap(base + root * width, base + child * width, width);
        root = child;
    }
}
static void heap(unsigned char *base, size_t count, size_t width, comparator compare,
                 void *context)
{
    size_t root = count / 2;
    while (root)
        sift(base, count, width, --root, compare, context);
    while (count > 1)
    {
        swap(base, base + (--count) * width, width);
        sift(base, count, width, 0, compare, context);
    }
}
static void sort(unsigned char *base, size_t count, size_t width, comparator compare,
                 void *context, unsigned budget)
{
#ifndef CONTEXT
    (void)context;
#endif
    while (count > 8)
    {
        if (!budget)
        {
            heap(base, count, width, compare, context);
            return;
        }
        budget--;
        size_t middle = count / 2, last = count - 1;
        if (CMP(base, base + middle * width) > 0)
            swap(base, base + middle * width, width);
        if (CMP(base + middle * width, base + last * width) > 0)
            swap(base + middle * width, base + last * width, width);
        if (CMP(base, base + middle * width) > 0)
            swap(base, base + middle * width, width);
        swap(base, base + middle * width, width);
        size_t a = 1, b = 1, c = count, d = count;
        /* The pivot stays at element zero. Equal records occupy [0,a) and
           [d,count), lesser records [a,b), and greater records [c,d). */
        for (;;)
        {
            int order;
            while (b < c && (order = CMP(base + b * width, base)) <= 0)
            {
                if (!order)
                    swap(base + (a++) * width, base + b * width, width);
                b++;
            }
            while (b < c && (order = CMP(base + (c - 1) * width, base)) >= 0)
            {
                if (!order)
                    swap(base + (--d) * width, base + (c - 1) * width, width);
                c--;
            }
            if (b == c)
                break;
            swap(base + (b++) * width, base + (--c) * width, width);
        }
        size_t left = b - a, right = d - b, span = a < left ? a : left;
        for (size_t i = 0; i < span; i++)
            swap(base + i * width, base + (b - span + i) * width, width);
        span = count - d < right ? count - d : right;
        for (size_t i = 0; i < span; i++)
            swap(base + (b + i) * width, base + (count - span + i) * width, width);
        if (left < right)
        {
            if (left > 1)
                sort(base, left, width, compare, context, budget);
            base += (count - right) * width;
            count = right;
        }
        else
        {
            if (right > 1)
                sort(base + (count - right) * width, right, width, compare, context,
                     budget);
            count = left;
        }
    }
    for (size_t i = 1; i < count; i++)
    {
        size_t j = i;
        while (j && CMP(base + (j - 1) * width, base + j * width) > 0)
        {
            swap(base + (j - 1) * width, base + j * width, width);
            j--;
        }
    }
}
#ifdef CONTEXT
void db_qsort_r(void *base, size_t count, size_t width, comparator compare, void *context)
#else
void db_qsort(void *base, size_t count, size_t width, comparator compare)
#endif
{
    unsigned budget = 0;
    if (count < 2 || !width)
        return;
    for (size_t n = count; n > 1; n /= 2)
        budget += 2;
#ifndef CONTEXT
    void *context = NULL;
#endif
    sort(base, count, width, compare, context, budget);
}
