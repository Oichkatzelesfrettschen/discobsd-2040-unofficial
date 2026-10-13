#include <errno.h>
#include <stdio.h>
#include <strings.h>

static unsigned
reference_fold(unsigned byte)
{
    static const char uppercase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char lowercase[] = "abcdefghijklmnopqrstuvwxyz";
    unsigned index;

    for (index = 0; index < sizeof(uppercase) - 1; ++index)
        if (byte == (unsigned char)uppercase[index])
            return (unsigned char)lowercase[index];
    return byte;
}

static int
sign_of(int value)
{
    return (value > 0) - (value < 0);
}

static unsigned
check_boundaries(void)
{
    char empty[1] = { 0 };
    char first[1] = { 'A' };
    char second[1] = { 'a' };
    unsigned failures = 0;

    errno = EDOM;
    failures += strncasecmp(first, second, 0) != 0;
    failures += strncasecmp(first, second, 1) != 0;
    failures += strcasecmp(empty, empty) != 0;
    failures += strncasecmp(empty, empty, (size_t)-1) != 0;
    failures += strcasecmp("aBcD", "AbCd") != 0;
    failures += strncasecmp("aBcDX", "AbCdY", 4) != 0;
    failures += sign_of(strncasecmp("aBcDX", "AbCdY", 5)) != -1;
    failures += sign_of(strcasecmp("prefix", "PREFIXtail")) != -1;
    failures += sign_of(strcasecmp("PREFIXtail", "prefix")) != 1;
    failures += errno != EDOM;
    return failures;
}

int
main(void)
{
    unsigned left_byte, right_byte;
    unsigned unbounded_differences = 0, bounded_differences = 0;
    unsigned errno_changes = 0;
    unsigned boundary_failures;
    char left[2] = { 0, 0 }, right[2] = { 0, 0 };
    int expected, result;

    for (left_byte = 0; left_byte < 256; ++left_byte) {
        left[0] = (char)left_byte;
        for (right_byte = 0; right_byte < 256; ++right_byte) {
            right[0] = (char)right_byte;
            expected = sign_of((int)reference_fold(left_byte) -
                (int)reference_fold(right_byte));
            errno = EDOM;
            result = strcasecmp(left, right);
            unbounded_differences += sign_of(result) != expected;
            errno_changes += errno != EDOM;
            errno = EDOM;
            result = strncasecmp(left, right, 1);
            bounded_differences += sign_of(result) != expected;
            errno_changes += errno != EDOM;
        }
    }
    boundary_failures = check_boundaries();
    if (unbounded_differences != 0 || bounded_differences != 0)
        fputs("FAIL casefold byte oracle\n", stderr);
    if (errno_changes != 0)
        fputs("FAIL casefold errno preservation\n", stderr);
    if (boundary_failures != 0)
        fputs("FAIL casefold count and NUL boundaries\n", stderr);
    printf("pairs=65536 strcasecmp_sign_differences=%u "
        "strncasecmp_sign_differences=%u errno_changes=%u\n",
        unbounded_differences, bounded_differences, errno_changes);
    return unbounded_differences != 0 || bounded_differences != 0 ||
        errno_changes != 0 || boundary_failures != 0;
}
