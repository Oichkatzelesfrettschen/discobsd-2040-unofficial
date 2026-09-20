#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int discobsd_rmdir_main(int, char **);

#define MAX_REMOVE_CALLS 8

static char removed_paths[MAX_REMOVE_CALLS][32];
static unsigned remove_calls;

int
test_rmdir(const char *path)
{
    if (remove_calls < MAX_REMOVE_CALLS) {
        snprintf(removed_paths[remove_calls], sizeof(removed_paths[0]),
            "%s", path);
    }
    ++remove_calls;
    return 0;
}

static int
check_calls(unsigned expected_count, const char *const *expected_paths)
{
    unsigned call_index;

    if (remove_calls != expected_count)
        return 0;
    for (call_index = 0; call_index < expected_count; ++call_index) {
        if (strcmp(removed_paths[call_index], expected_paths[call_index]) != 0)
            return 0;
    }
    return 1;
}

int
main(void)
{
    char program[] = "rmdir";
    char option[] = "-p";
    char path[] = "/leaf";
    char *arguments[] = {program, option, path, NULL};
    const char *top_level_calls[] = {"/leaf"};
    char trailing_path[] = "tree/parent/leaf///";
    char *trailing_arguments[] = {program, option, trailing_path, NULL};
    const char *trailing_calls[] = {
        "tree/parent/leaf", "tree/parent", "tree"
    };
    char repeated_root_path[] = "///leaf";
    char *repeated_root_arguments[] = {
        program, option, repeated_root_path, NULL
    };
    const char *repeated_root_calls[] = {"///leaf"};

    if (discobsd_rmdir_main(0, NULL) == 0) {
        fputs("rmdir contract test: empty argument vector succeeded\n", stderr);
        return EXIT_FAILURE;
    }

    if (discobsd_rmdir_main(3, arguments) != 0) {
        fputs("rmdir contract test: top-level path failed\n", stderr);
        return EXIT_FAILURE;
    }
    if (!check_calls(1, top_level_calls)) {
        fputs("rmdir contract test: root traversal reached an empty path\n",
            stderr);
        return EXIT_FAILURE;
    }

    remove_calls = 0;
    if (discobsd_rmdir_main(3, trailing_arguments) != 0 ||
        !check_calls(3, trailing_calls)) {
        fputs("rmdir contract test: trailing separators reached the syscall\n",
            stderr);
        return EXIT_FAILURE;
    }

    remove_calls = 0;
    if (discobsd_rmdir_main(3, repeated_root_arguments) != 0 ||
        !check_calls(1, repeated_root_calls)) {
        fputs("rmdir contract test: repeated root separators were removed\n",
            stderr);
        return EXIT_FAILURE;
    }
    puts("rmdir contract tests passed");
    return EXIT_SUCCESS;
}
