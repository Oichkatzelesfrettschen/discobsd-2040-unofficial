#include <stdio.h>

int
main(void)
{
    return fprintf(stdout, "%s:%d", "x", 42) != 4;
}
