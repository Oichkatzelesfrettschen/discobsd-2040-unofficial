#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int discobsd_rmdir_main(int, char **);

static char removed_path[32];
static unsigned remove_calls;

int
test_rmdir(const char *path)
{
    ++remove_calls;
    snprintf(removed_path, sizeof(removed_path), "%s", path);
    return 0;
}

int
main(void)
{
    char program[] = "rmdir";
    char option[] = "-p";
    char path[] = "/leaf";
    char *arguments[] = {program, option, path, NULL};

    if (discobsd_rmdir_main(3, arguments) != 0) {
        fputs("rmdir contract test: top-level path failed\n", stderr);
        return EXIT_FAILURE;
    }
    if (remove_calls != 1 || strcmp(removed_path, "/leaf") != 0) {
        fputs("rmdir contract test: root traversal reached an empty path\n",
            stderr);
        return EXIT_FAILURE;
    }
    puts("rmdir contract tests passed");
    return EXIT_SUCCESS;
}
