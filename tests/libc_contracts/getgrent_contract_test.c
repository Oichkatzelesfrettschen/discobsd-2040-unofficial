/*
 * lib/libc/gen/getgrent.c compiled for the host under db_ names, with its
 * malloc(), free() and fopen() routed to this file. The group file is a
 * temporary file standing in for /etc/group. The cases pin the first-use
 * record: no allocation before the first getgrent(), one allocation for
 * every later call, NULL with ENOMEM when that allocation fails and a
 * normal entry once it succeeds, no release at endgrent(), an entry that
 * outlives endgrent() as getgrgid() returns it, and the member vector's
 * MAXGRP - 1 bound.
 */
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct group *db_getgrent(void);
void db_setgrent(void);
void db_endgrent(void);
void *grent_test_malloc(size_t);
void grent_test_free(void *);
FILE *grent_test_fopen(const char *, const char *);

static unsigned checks;
static unsigned malloc_calls;
static unsigned free_calls;
static unsigned fopen_calls;
static size_t malloc_size;
static int fail_next_malloc;
static char group_path[] = "/tmp/getgrent-contract-XXXXXX";

static void
require(int condition, const char *case_name, const char *detail)
{
    ++checks;
    if (!condition) {
        fprintf(stderr, "getgrent contract test: %s: %s\n", case_name,
            detail);
        unlink(group_path);
        exit(EXIT_FAILURE);
    }
}

void *
grent_test_malloc(size_t size)
{
    ++malloc_calls;
    malloc_size = size;
    if (fail_next_malloc) {
        fail_next_malloc = 0;
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}

void
grent_test_free(void *pointer)
{
    ++free_calls;
    free(pointer);
}

FILE *
grent_test_fopen(const char *path, const char *mode)
{
    ++fopen_calls;
    require(strcmp(path, "/etc/group") == 0, "fopen", "path is not /etc/group");
    require(strcmp(mode, "r") == 0, "fopen", "mode is not r");
    return fopen(group_path, mode);
}

static void
write_group_file(void)
{
    FILE *file;
    int descriptor, i;

    descriptor = mkstemp(group_path);
    require(descriptor >= 0, "setup", "mkstemp failed");
    file = fdopen(descriptor, "w");
    require(file != NULL, "setup", "fdopen failed");
    fputs("wheel:*:0:root,operator\n", file);
    fputs("nogroup:*:65533:\n", file);
    /*
     * 248 empty members, more than the MAXGRP - 1 the vector holds, in a
     * 255-byte line, the longest one fgets() call returns whole.
     */
    fputs("w:*:7:", file);
    for (i = 0; i < 248; i++)
        fputc(',', file);
    fputc('\n', file);
    require(fclose(file) == 0, "setup", "fclose failed");
}

static void
require_wheel(const struct group *entry, const char *case_name)
{
    require(entry != NULL, case_name, "no first entry");
    require(strcmp(entry->gr_name, "wheel") == 0, case_name, "gr_name");
    require(strcmp(entry->gr_passwd, "*") == 0, case_name, "gr_passwd");
    require(entry->gr_gid == 0, case_name, "gr_gid");
    require(entry->gr_mem != NULL && entry->gr_mem[0] != NULL &&
        strcmp(entry->gr_mem[0], "root") == 0, case_name, "first member");
    require(entry->gr_mem[1] != NULL &&
        strcmp(entry->gr_mem[1], "operator") == 0, case_name,
        "second member");
    require(entry->gr_mem[2] == NULL, case_name, "member terminator");
}

int
main(void)
{
    struct group *entry;
    char **first_vector;
    int count;

    write_group_file();

    /* setgrent() opens the file and allocates nothing. */
    db_setgrent();
    require(fopen_calls == 1, "setgrent", "group file not opened");
    require(malloc_calls == 0, "setgrent", "allocated before getgrent");

    /* The first allocation fails: NULL with ENOMEM, nothing read. */
    fail_next_malloc = 1;
    errno = 0;
    entry = db_getgrent();
    require(entry == NULL, "allocation failure", "returned an entry");
    require(errno == ENOMEM, "allocation failure", "errno is not ENOMEM");
    require(malloc_calls == 1, "allocation failure", "malloc not called");

    /* The next call allocates and returns the first line. */
    entry = db_getgrent();
    require_wheel(entry, "first use");
    require(malloc_calls == 2, "first use", "allocation not retried");
    require(malloc_size >= 200 * sizeof(char *) + 257, "first use",
        "record smaller than the member vector and line");
    first_vector = entry->gr_mem;

    entry = db_getgrent();
    require(entry != NULL && strcmp(entry->gr_name, "nogroup") == 0 &&
        entry->gr_gid == 65533 && entry->gr_mem[0] == NULL,
        "repeated use", "entry without members");
    require(entry->gr_mem == first_vector, "repeated use",
        "member vector moved");

    entry = db_getgrent();
    require(entry != NULL && strcmp(entry->gr_name, "w") == 0,
        "member bound", "wide entry missing");
    for (count = 0; entry->gr_mem[count] != NULL; count++)
        require(entry->gr_mem[count][0] == '\0', "member bound",
            "empty member is not empty");
    require(count == 199, "member bound", "vector not bounded at 199");

    require(db_getgrent() == NULL, "end of file", "entry after last line");

    /* A getgrgid()-shaped search: the entry outlives endgrent(). */
    db_setgrent();
    entry = db_getgrent();
    db_endgrent();
    require_wheel(entry, "after endgrent");
    require(free_calls == 0, "after endgrent", "record released");

    /* Reopening reads from the start without a new allocation. */
    require_wheel(db_getgrent(), "reopen");
    require(fopen_calls == 2, "reopen", "file not reopened");
    require(malloc_calls == 2, "reopen", "record allocated again");
    db_endgrent();

    unlink(group_path);
    printf("getgrent contract: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
