#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/vis.h"

int vis_test_errno;

char *db_vis(char *, int, int, int);
char *db_nvis(char *, size_t, int, int, int);
int db_strvis(char *, const char *, int);
int db_strnvis(char *, size_t, const char *, int);
int db_strvisx(char *, const char *, size_t, int);
int db_strnvisx(char *, size_t, const char *, size_t, int);

static unsigned checks;

static void
require(int condition, const char *case_name, const char *detail)
{
    ++checks;
    if (!condition) {
        fprintf(stderr, "vis contract test: %s: %s\n", case_name, detail);
        exit(EXIT_FAILURE);
    }
}

static void
reference_vis(char output[5], unsigned char character, int flags,
    unsigned char next_character)
{
    char *destination = output;

    if ((character >= '!' && character <= '~') ||
        ((flags & VIS_SP) == 0 && character == ' ') ||
        ((flags & VIS_TAB) == 0 && character == '\t') ||
        ((flags & VIS_NL) == 0 && character == '\n') ||
        ((flags & VIS_SAFE) != 0 &&
        (character == '\b' || character == '\a' || character == '\r'))) {
        *destination++ = (char)character;
        if (character == '\\' && (flags & VIS_NOSLASH) == 0)
            *destination++ = '\\';
        *destination = '\0';
        return;
    }

    if ((flags & VIS_CSTYLE) != 0) {
        switch (character) {
        case '\n':
            *destination++ = '\\';
            *destination++ = 'n';
            break;
        case '\r':
            *destination++ = '\\';
            *destination++ = 'r';
            break;
        case '\b':
            *destination++ = '\\';
            *destination++ = 'b';
            break;
        case '\a':
            *destination++ = '\\';
            *destination++ = 'a';
            break;
        case '\v':
            *destination++ = '\\';
            *destination++ = 'v';
            break;
        case '\t':
            *destination++ = '\\';
            *destination++ = 't';
            break;
        case '\f':
            *destination++ = '\\';
            *destination++ = 'f';
            break;
        case ' ':
            *destination++ = '\\';
            *destination++ = 's';
            break;
        case '\0':
            *destination++ = '\\';
            *destination++ = '0';
            if (next_character >= '0' && next_character <= '7') {
                *destination++ = '0';
                *destination++ = '0';
            }
            break;
        default:
            destination = output;
            break;
        }
        if (destination != output) {
            *destination = '\0';
            return;
        }
    }

    if ((character & 0177) == ' ' || (flags & VIS_OCTAL) != 0) {
        *destination++ = '\\';
        *destination++ = (char)((character >> 6 & 07) + '0');
        *destination++ = (char)((character >> 3 & 07) + '0');
        *destination++ = (char)((character & 07) + '0');
    } else {
        if ((flags & VIS_NOSLASH) == 0)
            *destination++ = '\\';
        if ((character & 0200) != 0) {
            character &= 0177;
            *destination++ = 'M';
        }
        if (character < ' ' || character == 0177) {
            *destination++ = '^';
            *destination++ = character == 0177 ? '?' :
                (char)(character + '@');
        } else {
            *destination++ = '-';
            *destination++ = (char)character;
        }
    }
    *destination = '\0';
}

static void
check_legacy_domain(void)
{
    static const unsigned char next_characters[] = {'\0', '7'};
    const unsigned int all_flags = VIS_OCTAL | VIS_CSTYLE | VIS_SP | VIS_TAB |
        VIS_NL | VIS_SAFE | VIS_NOSLASH;
    char actual[5];
    char expected[5];
    unsigned int character;
    unsigned int flags;
    size_t next_index;

    for (flags = 0; flags <= all_flags; ++flags) {
        for (character = 0; character <= 0xff; ++character) {
            for (next_index = 0;
                next_index < sizeof(next_characters) /
                sizeof(next_characters[0]); ++next_index) {
                reference_vis(expected, (unsigned char)character, (int)flags,
                    next_characters[next_index]);
                require(db_vis(actual, (int)character, (int)flags,
                    next_characters[next_index]) == actual + strlen(expected),
                    "legacy-domain", "result pointer");
                require(strcmp(actual, expected) == 0, "legacy-domain",
                    "encoding mismatch");
            }
        }
    }
}

static void
check_character(const char *case_name, int character, int flags,
    int next_character, const char *expected)
{
    unsigned char before[16];
    char buffer[16];
    size_t capacity;
    size_t expected_length = strlen(expected);
    char *result;

    memset(before, 0xa5, sizeof(before));
    for (capacity = 0; capacity <= expected_length; ++capacity) {
        memcpy(buffer, before, sizeof(buffer));
        vis_test_errno = 0;
        result = db_nvis(buffer, capacity, character, flags, next_character);
        require(result == NULL, case_name, "short buffer succeeded");
        require(vis_test_errno == ENOSPC, case_name, "short buffer errno");
        require(memcmp(buffer, before, sizeof(buffer)) == 0, case_name,
            "short buffer changed destination");
    }

    memcpy(buffer, before, sizeof(buffer));
    vis_test_errno = EAGAIN;
    result = db_nvis(buffer, expected_length + 1, character, flags,
        next_character);
    require(result == buffer + expected_length, case_name,
        "exact-fit result pointer");
    require(strcmp(buffer, expected) == 0, case_name, "exact-fit encoding");
    require((unsigned char)buffer[expected_length + 1] == before[0], case_name,
        "exact-fit guard byte");
    require(vis_test_errno == EAGAIN, case_name, "success changed errno");

    memcpy(buffer, before, sizeof(buffer));
    result = db_vis(buffer, character, flags, next_character);
    require(result == buffer + expected_length, case_name,
        "legacy result pointer");
    require(strcmp(buffer, expected) == 0, case_name, "legacy encoding");
}

static int
bounded_string(char *destination, size_t destination_length,
    const char *source, size_t source_length, int flags, int exact_length)
{
    if (exact_length) {
        return db_strnvisx(destination, destination_length, source,
            source_length, flags);
    }
    return db_strnvis(destination, destination_length, source, flags);
}

static int
legacy_string(char *destination, const char *source, size_t source_length,
    int flags, int exact_length)
{
    if (exact_length)
        return db_strvisx(destination, source, source_length, flags);
    return db_strvis(destination, source, flags);
}

static void
check_string(const char *case_name, const char *source, size_t source_length,
    int flags, int exact_length, const char *expected)
{
    unsigned char before[32];
    char buffer[32];
    size_t capacity;
    size_t expected_length = strlen(expected);
    int result;

    memset(before, 0x5a, sizeof(before));
    for (capacity = 0; capacity <= expected_length; ++capacity) {
        memcpy(buffer, before, sizeof(buffer));
        vis_test_errno = 0;
        result = bounded_string(buffer, capacity, source, source_length,
            flags, exact_length);
        require(result == -1, case_name, "short buffer succeeded");
        require(vis_test_errno == ENOSPC, case_name, "short buffer errno");
        require(memcmp(buffer, before, sizeof(buffer)) == 0, case_name,
            "short buffer changed destination");
    }

    memcpy(buffer, before, sizeof(buffer));
    vis_test_errno = EAGAIN;
    result = bounded_string(buffer, expected_length + 1, source, source_length,
        flags, exact_length);
    require(result == (int)expected_length, case_name,
        "exact-fit return length");
    require(strcmp(buffer, expected) == 0, case_name, "exact-fit encoding");
    require((unsigned char)buffer[expected_length + 1] == before[0], case_name,
        "exact-fit guard byte");
    require(vis_test_errno == EAGAIN, case_name, "success changed errno");

    memcpy(buffer, before, sizeof(buffer));
    result = legacy_string(buffer, source, source_length, flags, exact_length);
    require(result == (int)expected_length, case_name, "legacy return length");
    require(strcmp(buffer, expected) == 0, case_name, "legacy encoding");
}

int
main(void)
{
    static const struct {
        const char *name;
        int character;
        int flags;
        int next_character;
        const char *expected;
    } character_cases[] = {
        {"graphic", 'A', 0, 0, "A"},
        {"backslash", '\\', 0, 0, "\\\\"},
        {"cstyle-newline", '\n', VIS_CSTYLE | VIS_NL, 0, "\\n"},
        {"cstyle-null-octal", 0, VIS_CSTYLE, '7', "\\000"},
        {"octal-space", ' ', VIS_SP, 0, "\\040"},
        {"encoded-tab", '\t', VIS_TAB, 0, "\\^I"},
        {"encoded-newline", '\n', VIS_NL, 0, "\\^J"},
        {"safe-backspace", '\b', VIS_SAFE, 0, "\b"},
        {"cstyle-space", ' ', VIS_SP | VIS_CSTYLE, 0, "\\s"},
        {"meta-control", 0x80, 0, 0, "\\M^@"},
        {"signed-meta-control", (char)0x80, 0, 0, "\\M^@"},
        {"meta-graphic", 0xa1, 0, 0, "\\M-!"},
        {"noslash-meta", 0x80, VIS_NOSLASH, 0, "M^@"},
        {"delete", 0x7f, 0, 0, "\\^?"},
        {"high-octal", 0xff, VIS_OCTAL, 0, "\\377"},
        {"literal-tab", '\t', 0, 0, "\t"}
    };
    const char text_source[] = "A\n";
    const char embedded_source[] = {'A', '\0', '7'};
    const char high_source[] = {(char)0xa1, (char)0x80, '\0'};
    char empty_buffer[2] = {'x', 'y'};
    size_t case_index;

    check_legacy_domain();

    for (case_index = 0;
        case_index < sizeof(character_cases) / sizeof(character_cases[0]);
        ++case_index) {
        check_character(character_cases[case_index].name,
            character_cases[case_index].character,
            character_cases[case_index].flags,
            character_cases[case_index].next_character,
            character_cases[case_index].expected);
    }

    vis_test_errno = 0;
    require(db_nvis(NULL, 0, 'A', 0, 0) == NULL,
        "null-zero-character", "zero capacity succeeded");
    require(vis_test_errno == ENOSPC, "null-zero-character", "wrong errno");

    check_string("terminated-string", text_source, sizeof(text_source) - 1,
        VIS_CSTYLE | VIS_NL, 0, "A\\n");
    check_string("embedded-null", embedded_source, sizeof(embedded_source),
        VIS_CSTYLE, 1, "A\\0007");
    check_string("high-bytes", high_source, sizeof(high_source) - 1, 0, 0,
        "\\M-!\\M^@");

    vis_test_errno = 0;
    require(db_strnvisx(NULL, 0, embedded_source, sizeof(embedded_source),
        VIS_CSTYLE) == -1, "null-zero-string", "zero capacity succeeded");
    require(vis_test_errno == ENOSPC, "null-zero-string", "wrong errno");

    vis_test_errno = EAGAIN;
    require(db_strnvisx(empty_buffer, 1, embedded_source, 0, 0) == 0,
        "empty-string", "empty input failed");
    require(empty_buffer[0] == '\0' && empty_buffer[1] == 'y',
        "empty-string", "empty input bounds");
    require(vis_test_errno == EAGAIN, "empty-string", "success changed errno");

    printf("vis contract tests: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
