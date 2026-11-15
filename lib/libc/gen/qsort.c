/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * qsort.c:
 * Our own version of the system qsort routine which is faster by an average
 * of 25%, with lows and highs of 10% and 50%.
 * The THRESHold below is the insertion sort threshold, and has been adjusted
 * for records of size 48 bytes.
 * The MTHREShold is where we stop finding a better median.
 */
#include <stdlib.h>
#ifdef _DISCOBSD_QSORT_R
typedef int (*comparator)(const void *, const void *, void *);
#define CMP(a, b) state->compare(a, b, state->context)
#else
typedef int (*comparator)(const void *, const void *);
#define CMP(a, b) state->compare(a, b)
#endif
struct qsort_state
{
    size_t width;
    comparator compare;
#ifdef _DISCOBSD_QSORT_R
    void *context;
#endif
};
static void swap(unsigned char *a, unsigned char *b, size_t width)
{
    while (width--)
    {
        unsigned char byte = *a;
        *a++ = *b;
        *b++ = byte;
    }
}
static void sift(unsigned char *base, size_t count, size_t root,
                 const struct qsort_state *state)
{
    size_t width = state->width;
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
static void heap(unsigned char *base, size_t count, const struct qsort_state *state)
{
    size_t width = state->width;
    size_t root = count / 2;
    while (root)
        sift(base, count, --root, state);
    while (count > 1)
    {
        swap(base, base + (--count) * width, width);
        sift(base, count, 0, state);
    }
}
struct qsort_split
{
    size_t left, right;
};
/* The qualified compiler keeps partition scratch outside recursive frames. */
#if defined(__GNUC__) || defined(__clang__)
#define QSORT_PARTITION_NOINLINE __attribute__((__noinline__))
#else
#define QSORT_PARTITION_NOINLINE
#endif
static QSORT_PARTITION_NOINLINE struct qsort_split
partition(unsigned char *base, size_t count, const struct qsort_state *state)
{
    size_t width = state->width;
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
    struct qsort_split result = {left, right};
    return result;
}
static void sort(unsigned char *base, size_t count, const struct qsort_state *state,
                 unsigned budget)
{
    size_t width = state->width;
    while (count > 8)
    {
        if (!budget)
        {
            heap(base, count, state);
            return;
        }
        budget--;
        struct qsort_split parts = partition(base, count, state);
        size_t left = parts.left, right = parts.right;
        if (left < right)
        {
            if (left > 1)
                sort(base, left, state, budget);
            base += (count - right) * width;
            count = right;
        }
        else
        {
            if (right > 1)
                sort(base + (count - right) * width, right, state, budget);
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
#ifdef _DISCOBSD_QSORT_R
void qsort_r(void *base, size_t count, size_t width, comparator compare, void *context)
#else
void qsort(void *base, size_t count, size_t width, comparator compare)
#endif
{
    unsigned budget = 0;
    if (count < 2 || !width)
        return;
    for (size_t n = count; n > 1; n /= 2)
        budget += 2;
    struct qsort_state state;
    state.width = width;
    state.compare = compare;
#ifdef _DISCOBSD_QSORT_R
    state.context = context;
#endif
    sort(base, count, &state, budget);
}
