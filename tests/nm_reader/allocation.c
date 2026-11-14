/* Allocation ownership controls for the native symbol reader. */
#include <stdio.h>
#include <stdlib.h>

static unsigned calls, live, fail_at;

static void *
reader_malloc(size_t bytes)
{
    void *result;

    calls++;
    if (calls == fail_at)
        return NULL;
    result = malloc(bytes);
    if (result != NULL)
        live++;
    return result;
}

static void
reader_free(void *pointer)
{
    if (pointer != NULL) {
        if (live == 0)
            abort();
        live--;
    }
    free(pointer);
}

#define malloc reader_malloc
#define free reader_free
#define main nm_main
#include "../../usr.bin/nm/nm.c"
#undef main
#undef free
#undef malloc

int
main(int argc, char **argv)
{
    char *arguments[3];
    int status;

    if (argc != 3)
        return 2;
    fail_at = (unsigned)strtoul(argv[1], NULL, 10);
    arguments[0] = argv[0];
    arguments[1] = argv[2];
    arguments[2] = NULL;
    status = nm_main(2, arguments);
    if (live != 0 || calls != (fail_at ? fail_at : 3) ||
        (fail_at ? status == 0 : status != 0)) {
        fprintf(stderr, "allocation control: calls=%u live=%u status=%d\n",
            calls, live, status);
        return 1;
    }
    return 0;
}
