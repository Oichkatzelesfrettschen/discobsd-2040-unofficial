#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TARGET_BUFSIZ 1024
#define FIXTURE_SIZE (TARGET_BUFSIZ * 2 + 317)

int discobsd_fastcat(int);

static unsigned char input_data[FIXTURE_SIZE];
static unsigned char output_data[FIXTURE_SIZE];
static size_t input_length;
static size_t input_offset;
static size_t output_length;
static size_t write_limit;
static unsigned read_calls;
static unsigned write_calls;
static unsigned perror_calls;
static int output_descriptor;
static int fail_read;
static int fail_read_after_data;
static int fail_write;
static int zero_write;
static int wrong_read_size;
static char perror_label[32];

static void
reset_fixture(void)
{
    input_length = 0;
    input_offset = 0;
    output_length = 0;
    write_limit = (size_t)-1;
    read_calls = 0;
    write_calls = 0;
    perror_calls = 0;
    output_descriptor = STDOUT_FILENO;
    fail_read = 0;
    fail_read_after_data = 0;
    fail_write = 0;
    zero_write = 0;
    wrong_read_size = 0;
    perror_label[0] = '\0';
}

static int
check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "cat contract test: %s\n", message);
    return 1;
}

ssize_t
test_read(int descriptor, void *buffer, size_t length)
{
    size_t available;
    size_t transfer_length;

    ++read_calls;
    if (length != TARGET_BUFSIZ)
        wrong_read_size = 1;
    if (descriptor < 0 || fail_read ||
        (fail_read_after_data && input_offset == input_length)) {
        errno = EBADF;
        return -1;
    }
    if (input_offset == input_length)
        return 0;
    available = input_length - input_offset;
    transfer_length = available < length ? available : length;
    memcpy(buffer, input_data + input_offset, transfer_length);
    input_offset += transfer_length;
    return (ssize_t)transfer_length;
}

ssize_t
test_write(int descriptor, const void *buffer, size_t length)
{
    size_t transfer_length;

    ++write_calls;
    if (descriptor != output_descriptor || fail_write) {
        errno = EIO;
        return -1;
    }
    if (zero_write)
        return 0;
    transfer_length = length < write_limit ? length : write_limit;
    if (transfer_length > sizeof(output_data) - output_length) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(output_data + output_length, buffer, transfer_length);
    output_length += transfer_length;
    return (ssize_t)transfer_length;
}

int
test_fileno(FILE *stream)
{
    if (stream != stdout)
        return -1;
    return output_descriptor;
}

void
test_perror(const char *label)
{
    ++perror_calls;
    snprintf(perror_label, sizeof(perror_label), "%s", label);
}

static int
test_empty_input(void)
{
    int failures = 0;

    reset_fixture();
    failures += check(discobsd_fastcat(3) == 0,
        "empty input failed");
    failures += check(read_calls == 1 && write_calls == 0,
        "empty input performed unexpected I/O");
    failures += check(!wrong_read_size,
        "empty input used a non-target read size");
    return failures;
}

static int
test_short_writes(void)
{
    size_t byte_index;
    int failures = 0;

    reset_fixture();
    input_length = sizeof(input_data);
    write_limit = 17;
    for (byte_index = 0; byte_index < input_length; ++byte_index)
        input_data[byte_index] = (unsigned char)(byte_index * 29U + 7U);
    failures += check(discobsd_fastcat(3) == 0,
        "short-write copy failed");
    failures += check(!wrong_read_size,
        "copy used a non-target read size");
    failures += check(output_length == input_length,
        "short-write copy changed the length");
    failures += check(memcmp(output_data, input_data, input_length) == 0,
        "short-write copy changed the bytes");
    failures += check(write_calls > 3,
        "short-write copy did not retry writes");
    return failures;
}

static int
test_read_error(void)
{
    int failures = 0;

    reset_fixture();
    input_length = TARGET_BUFSIZ;
    memset(input_data, 0xa5, input_length);
    fail_read_after_data = 1;
    failures += check(discobsd_fastcat(3) == 1,
        "read error returned the wrong status");
    failures += check(output_length == input_length,
        "read error discarded preceding data");
    failures += check(perror_calls == 1 &&
        strcmp(perror_label, "cat: read error") == 0,
        "read error used the wrong diagnostic");
    return failures;
}

static int
test_invalid_input_descriptor(void)
{
    int failures = 0;

    reset_fixture();
    fail_read = 1;
    failures += check(discobsd_fastcat(-1) == 1,
        "invalid input returned the wrong status");
    failures += check(perror_calls == 1 && write_calls == 0,
        "invalid input reached the output path");
    return failures;
}

static int
test_invalid_output_descriptor(void)
{
    int failures = 0;

    reset_fixture();
    output_descriptor = -1;
    failures += check(discobsd_fastcat(3) == 2,
        "invalid output returned the wrong status");
    failures += check(read_calls == 0 && perror_calls == 1,
        "invalid output reached the input path");
    failures += check(strcmp(perror_label, "cat: write error") == 0,
        "invalid output used the wrong diagnostic");
    return failures;
}

static int
test_write_failure(int return_zero)
{
    int failures = 0;

    reset_fixture();
    input_length = 1;
    input_data[0] = 0x5a;
    fail_write = !return_zero;
    zero_write = return_zero;
    errno = 0;
    failures += check(discobsd_fastcat(3) == 2,
        return_zero ? "zero write returned the wrong status" :
        "write error returned the wrong status");
    failures += check(perror_calls == 1,
        return_zero ? "zero write did not report an error" :
        "write error did not report an error");
    failures += check(strcmp(perror_label, "cat: write error") == 0,
        return_zero ? "zero write used the wrong diagnostic" :
        "write error used the wrong diagnostic");
    if (return_zero)
        failures += check(errno == EIO,
            "zero write did not establish an I/O error");
    return failures;
}

int
main(void)
{
    int failures = 0;

    failures += test_empty_input();
    failures += test_short_writes();
    failures += test_read_error();
    failures += test_invalid_input_descriptor();
    failures += test_invalid_output_descriptor();
    failures += test_write_failure(0);
    failures += test_write_failure(1);
    if (failures != 0)
        return EXIT_FAILURE;
    puts("cat contract tests passed");
    return EXIT_SUCCESS;
}
