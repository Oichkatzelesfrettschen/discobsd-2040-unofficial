/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Remove directory
 */
#include <stdio.h>
#include <unistd.h>

static int
report_failure(const char *path)
{
    fputs("rmdir: ", stderr);
    perror(path);
    return 1;
}

static int
remove_parents(char *path)
{
    char *cursor;
    char *separator;

    for (;;) {
        separator = NULL;
        for (cursor = path; *cursor != '\0'; ++cursor) {
            if (*cursor == '/')
                separator = cursor;
        }
        if (separator == NULL)
            break;
        *separator = '\0';
        if (separator[1] == '\0')
            continue;
        if (*path == '\0')
            break;
        if (rmdir(path) < 0)
            return report_failure(path);
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int failed = 0;
    int remove_parent_components = 0;
    const char *program_name = argv[0];

    while (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0') {
        char *option = argv[1] + 1;

        --argc;
        ++argv;
        if (option[0] == '-' && option[1] == '\0')
            break;
        while (*option != '\0') {
            if (*option++ != 'p') {
                goto usage;
            }
            remove_parent_components = 1;
        }
    }
    if (argc == 1)
        goto usage;
    while (--argc > 0) {
        char *path = *++argv;

        if (rmdir(path) < 0) {
            failed = report_failure(path);
        } else if (remove_parent_components) {
            failed |= remove_parents(path);
        }
    }
    return failed;

usage:
    fprintf(stderr, "usage: %s [-p] directory ...\n", program_name);
    return 1;
}
