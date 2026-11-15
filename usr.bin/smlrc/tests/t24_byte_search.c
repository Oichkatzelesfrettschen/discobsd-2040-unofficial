#include <string.h>
#include <string.h>
#include <stdio.h>

static void *(*byte_search)(const void *, int, size_t) = memchr;
static void *(*sequence_search)(const void *, size_t, const void *, size_t) = memmem;

int
main(void)
{
    unsigned char bytes[6] = { 0x80, 0, 0xff, 0x80, 0, 0xff };
    unsigned char needle[3] = { 0x80, 0, 0xff };
    if (byte_search(bytes, 0x100, 6) != bytes + 1)
        return 1;
    if (byte_search(bytes, -1, 6) != bytes + 2)
        return 2;
    if (sequence_search(bytes, 6, needle, 3) != bytes)
        return 3;
    if (sequence_search(bytes + 1, 5, needle, 3) != bytes + 3)
        return 4;
    if (sequence_search(bytes, 6, needle, 0) != bytes)
        return 5;
    if (sequence_search(bytes, 2, needle, 3) != NULL)
        return 6;
    printf("byte searches\n");
    return 0;
}
