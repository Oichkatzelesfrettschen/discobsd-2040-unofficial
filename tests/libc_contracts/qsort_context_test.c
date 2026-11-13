#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void db_qsort_r(void *, size_t, size_t,
    int (*)(const void *, const void *, void *), void *);
void db_qsort(void *, size_t, size_t, int (*)(const void *, const void *));

struct sort_context {
    unsigned char *base;
    size_t count;
    size_t width;
    size_t calls;
    int direction;
    int nested;
};

static unsigned cases;

static int
compare_plain(const void *left, const void *right)
{
    return (*(const unsigned char *)left > *(const unsigned char *)right) -
        (*(const unsigned char *)left < *(const unsigned char *)right);
}

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "qsort context: %s\n", message);
        exit(1);
    }
}

static int
compare(const void *left, const void *right, void *opaque)
{
    struct sort_context *context = opaque;
    uintptr_t begin, end, a, b;
    int order;

    require(context != NULL, "callback loses context");
    begin = (uintptr_t)context->base;
    end = begin + context->count * context->width;
    a = (uintptr_t)left;
    b = (uintptr_t)right;
    require(a >= begin && a < end && b >= begin && b < end,
        "callback argument lies outside the input array");
    require((a - begin) % context->width == 0 &&
        (b - begin) % context->width == 0,
        "callback argument misses an element boundary");
    context->calls++;
    if (context->nested) {
        unsigned char inner[] = { 1, 9, 3, 2, 6, 5, 4, 7, 8 };
        struct sort_context child = { inner, sizeof inner, 1, 0, -1, 0 };
        size_t i;

        db_qsort_r(inner, sizeof inner, 1, compare, &child);
        require(child.calls != 0, "nested comparator never executes");
        for (i = 0; i < sizeof inner; i++)
            require(inner[i] == 9 - i, "nested context changes ordering");
        db_qsort(inner, sizeof inner, 1, compare_plain);
        for (i = 0; i < sizeof inner; i++)
            require(inner[i] == i + 1, "nested ordinary sort changes ordering");
    }
    order = (*(const unsigned char *)left > *(const unsigned char *)right) -
        (*(const unsigned char *)left < *(const unsigned char *)right);
    if (context->direction < 0)
        order = -order;
    return order < 0 ? INT_MIN : order > 0 ? INT_MAX : 0;
}

static int
compare_null(const void *left, const void *right, void *context)
{
    require(context == NULL, "null context changes identity");
    return compare_plain(left, right);
}

static int
compare_plain_nested(const void *left, const void *right)
{
    unsigned char inner[] = { 9, 1, 7 };

    db_qsort_r(inner, sizeof inner, 1, compare_null, NULL);
    require(inner[0] == 1 && inner[1] == 7 && inner[2] == 9,
        "context sort inside ordinary sort loses ordering");
    return compare_plain(left, right);
}

static void
null_context(void)
{
    unsigned char values[] = { 9, 1, 7, 3, 8, 2 };
    const unsigned char expected[] = { 1, 2, 3, 7, 8, 9 };

    db_qsort(values, sizeof values, 1, compare_plain_nested);
    require(memcmp(values, expected, sizeof values) == 0,
        "ordinary sort around context callbacks loses ordering");
    cases++;
}

static void
sort_case(const unsigned char *keys, size_t count, size_t width,
    int direction, int nested, size_t displacement)
{
    unsigned char storage[2048 * 48 + 2], original[2048 * 48];
    unsigned char seen[2048] = { 0 };
    unsigned before[256] = { 0 }, after[256] = { 0 };
    unsigned char *base = storage + displacement;
    struct sort_context context = { base, count, width, 0, direction, nested };
    size_t i, byte;

    require(count <= 2048 && width <= 48, "invalid test dimensions");
    memset(storage, 0xa5, sizeof storage);
    for (i = 0; i < count; i++) {
        for (byte = 0; byte < width; byte++)
            base[i * width + byte] = (unsigned char)(i * 17 + byte * 13);
        base[i * width] = keys[i];
        if (width >= 3) {
            base[i * width + 1] = (unsigned char)i;
            base[i * width + 2] = (unsigned char)(i >> 8);
        }
        before[keys[i]]++;
    }
    memcpy(original, base, count * width);
    db_qsort_r(base, count, width, compare, &context);
    require(count != 0 || context.calls == 0, "empty input calls comparator");
    require(count < 2 || context.calls != 0, "nontrivial sort skips comparator");
    for (i = 0; i < count; i++) {
        unsigned key = base[i * width];
        after[key]++;
        if (i != 0)
            require(direction > 0 ? base[(i - 1) * width] <= key :
                base[(i - 1) * width] >= key, "output violates requested order");
        if (width >= 3) {
            size_t serial = base[i * width + 1] |
                ((size_t)base[i * width + 2] << 8);
            require(serial < count && !seen[serial], "record is lost or duplicated");
            seen[serial] = 1;
            require(memcmp(base + i * width, original + serial * width, width) == 0,
                "record bytes change during sorting");
        }
    }
    require(memcmp(before, after, sizeof before) == 0, "key multiplicities change");
    require(storage[displacement + count * width] == 0xa5,
        "sort writes beyond the array");
    require(displacement == 0 || storage[0] == 0xa5,
        "sort writes before the array");
    cases++;
}

static void
exhaustive(void)
{
    static const size_t widths[] = { 1, 3, 7, 48 };
    unsigned char keys[8];
    size_t count, width_index, word, words = 1, i;

    for (count = 0; count <= sizeof keys; count++) {
        if (count != 0)
            words *= 3;
        for (word = 0; word < words; word++) {
            size_t digits = word;
            for (i = 0; i < count; i++) {
                keys[i] = (unsigned char)(digits % 3);
                digits /= 3;
            }
            for (width_index = 0; width_index < sizeof widths / sizeof widths[0];
                width_index++) {
                sort_case(keys, count, widths[width_index], 1, 0, 1);
                sort_case(keys, count, widths[width_index], -1, 0, 0);
            }
        }
    }
}

static void
large_patterns(void)
{
    unsigned char keys[2048];
    size_t pattern, i;

    for (pattern = 0; pattern < 5; pattern++) {
        for (i = 0; i < sizeof keys; i++) {
            switch (pattern) {
            case 0: keys[i] = 7; break;
            case 1: keys[i] = (unsigned char)(i / 8); break;
            case 2: keys[i] = (unsigned char)(255 - i / 8); break;
            case 3: keys[i] = (unsigned char)(i % 5); break;
            default: keys[i] = (unsigned char)((i * 73 + i / 13) % 256); break;
            }
        }
        sort_case(keys, sizeof keys, 7, 1, 0, 1);
        sort_case(keys, sizeof keys, 48, -1, 0, 1);
    }
    sort_case(keys, 73, 48, 1, 1, 1);
}

int
main(void)
{
    exhaustive();
    large_patterns();
    null_context();
    printf("qsort context: %u order, permutation and context cases pass\n", cases);
    return 0;
}
