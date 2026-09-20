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
remove_path(char *path, int remove_parent_components)
{
    char *cursor;
    char *separator;

    if (remove_parent_components) {
        cursor = path;
        while (*cursor != '\0')
            ++cursor;
        while (cursor > path + 1 && cursor[-1] == '/')
            --cursor;
        *cursor = '\0';
    }
    for (;;) {
        if (rmdir(path) < 0)
            return report_failure(path);
        if (!remove_parent_components)
            break;
        separator = NULL;
        for (cursor = path; *cursor != '\0'; ++cursor) {
            if (*cursor == '/')
                separator = cursor;
        }
        if (separator == NULL)
            break;
        while (separator > path && separator[-1] == '/')
            --separator;
        if (separator == path)
            break;
        *separator = '\0';
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int failed = 0;
    int remove_parent_components = 0;

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
    if (argc <= 1)
        goto usage;
    while (--argc > 0) {
        char *path = *++argv;

        failed |= remove_path(path, remove_parent_components);
    }
    return failed;

usage:
    fputs("usage: rmdir [-p] directory ...\n", stderr);
    return 1;
}
