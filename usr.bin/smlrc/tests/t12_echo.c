/*
 * bin/echo/echo.c, with its definition of main written as a prototype.
 *
 * Smaller C's parser accepts no old-style parameter declaration list, so
 * the tree's copy cannot be compiled as it stands; the MIPS back end
 * rejects it at the same token, which places the limitation in the front
 * end rather than in either code generator. Nothing else is changed, so
 * what this exercises is the code the back end produces for a real program.
 */
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
    register int i, nflg;

    nflg = 0;
    if(argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n' && !argv[1][2]) {
        nflg++;
        argc--;
        argv++;
    }
    for(i=1; i<argc; i++) {
        fputs(argv[i], stdout);
        if (i < argc-1)
            putchar(' ');
    }
    if(nflg == 0)
        putchar('\n');
    exit(0);
}
