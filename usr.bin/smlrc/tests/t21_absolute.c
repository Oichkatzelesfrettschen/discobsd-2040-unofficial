#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>

static int (*int_absolute)(int) = abs;
static long (*long_absolute)(long) = labs;

int
main(void)
{
    if (int_absolute(-2147483647) != 2147483647)
        return 1;
    if (long_absolute(-2147483647L) != 2147483647L)
        return 2;
    if (int_absolute(0) != 0 || long_absolute(0L) != 0L)
        return 3;
    printf("absolute values\n");
    return 0;
}
