/* picoc main program - this varies depending on your operating system and
 * how you're using picoc */
 
/* include only picoc.h here - should be able to use it with only the external interfaces, no internals from interpreter.h */
#include "picoc.h"

/* platform-dependent code for running programs is in this file */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef PICOC_STACK_SIZE
#define PICOC_STACK_SIZE (128*1024)              /* space for the the stack */
#endif

static void RunSourceFiles(int argc, char **argv, int first_source_argument,
                           int dont_run_main)
{
    int source_argument;

    if (PicocPlatformSetExitPoint())
        return;

    for (source_argument = first_source_argument;
         source_argument < argc && strcmp(argv[source_argument], "-") != 0;
         source_argument++)
        PicocPlatformScanFile(argv[source_argument]);

    if (!dont_run_main)
        PicocCallMain(argc - source_argument, &argv[source_argument]);
}

int main(int argc, char **argv)
{
    int ParamCount = 1;
    int DontRunMain = FALSE;
    int StackSize = getenv("STACKSIZE") ? atoi(getenv("STACKSIZE")) : PICOC_STACK_SIZE;
    
    if (argc < 2)
    {
        printf("Format: picoc <csource1.c>... [- <arg1>...]    : run a program (calls main() to start it)\n"
               "        picoc -s <csource1.c>... [- <arg1>...] : script mode - runs the program without calling main()\n"
               "        picoc -i                               : interactive mode\n");
        exit(1);
    }
    
    PicocInitialise(StackSize);
    
    if (strcmp(argv[ParamCount], "-s") == 0 || strcmp(argv[ParamCount], "-m") == 0)
    {
        DontRunMain = TRUE;
        PicocIncludeAllSystemHeaders();
        ParamCount++;
    }
        
    if (argc > ParamCount && strcmp(argv[ParamCount], "-i") == 0)
    {
        PicocIncludeAllSystemHeaders();
        PicocParseInteractive(TRUE);
    }
    else
    {
        RunSourceFiles(argc, argv, ParamCount, DontRunMain);
    }
    
    PicocCleanup();
    return PicocExitValue;
}
