#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define main fgrep_command_main
#include "../fgrep.c"
#undef main

static void
require(condition, message)
int condition;
const char *message;
{
    if (condition)
        return;
    fprintf(stderr, "fgrep capacity test failed: %s\n", message);
    exit(1);
}

int
main(void)
{
    FILE *empty_file;
    FILE *special_file;
    unsigned long bytes;
    unsigned long allocation_limit;
    unsigned long rp2040_state_limit;
    int size_known;
    int close_status;

    empty_file = tmpfile();
    if (empty_file == NULL) {
        fprintf(stderr,
            "fgrep capacity test failed: create empty regular pattern file\n");
        return (1);
    }
    bytes = pattern_bytes(empty_file, "", &size_known);
    require(size_known && bytes == 0,
        "classify an empty regular file as a known zero-length source");
    allocation_limit = (size_t)-1 / sizeof *w;
    require(pattern_states(bytes, size_known, 0, allocation_limit) == 8,
        "reserve the minimum state table for an empty regular file");
    require(pattern_states(bytes, size_known, 1, allocation_limit) == 8,
        "avoid doubling the empty whole-line state table");
    close_status = fclose(empty_file);
    require(close_status == 0, "close empty regular pattern file");

    special_file = fopen("/dev/null", "r");
    if (special_file == NULL) {
        fprintf(stderr,
            "fgrep capacity test failed: open a non-regular pattern source\n");
        return (1);
    }
    bytes = pattern_bytes(special_file, "", &size_known);
    require(!size_known && bytes == 0,
        "classify a non-regular file as an unsized source");
    require(pattern_states(bytes, size_known, 1, allocation_limit) == MAXSIZ,
        "preserve the fixed state ceiling for an unsized source");
    close_status = fclose(special_file);
    require(close_status == 0, "close non-regular pattern source");

    require(pattern_states(3, 1, 0, allocation_limit) == 11,
        "add the non-whole-line state reserve");
    require(pattern_states(3, 1, 1, allocation_limit) == 14,
        "bound whole-line terminal states by pattern bytes");
    require(pattern_states(ULONG_MAX, 1, 1, allocation_limit) == 0,
        "reject state-count arithmetic overflow");

    require(pattern_states(allocation_limit, 1, 0, allocation_limit) == 0,
        "reject allocation-byte arithmetic overflow");

    /* RP2040 AAPCS uses 32-bit size_t and a 16-byte struct words. */
    rp2040_state_limit = UINT32_MAX / 16;
    require(pattern_states(rp2040_state_limit - 8, 1, 0,
        rp2040_state_limit) == rp2040_state_limit,
        "accept the exact RP2040 allocation boundary");
    require(pattern_states(rp2040_state_limit - 7, 1, 0,
        rp2040_state_limit) == 0,
        "reject the first state beyond the RP2040 allocation boundary");

    puts("fgrep capacity tests passed");
    return (0);
}
