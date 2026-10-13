#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _iobuf _iob[3];
int output_errno;
static unsigned failures;
static unsigned write_calls;
static unsigned fail_call;
static int short_write;
static unsigned captured_length;
static char captured[4096];

int
_fwalk(int (*function)(FILE *))
{
    (void)function;
    return 0;
}

int
output_fstat(int descriptor, struct stat *status)
{
    (void)descriptor;
    memset(status, 0, sizeof(*status));
    status->st_blksize = BUFSIZ;
    return 0;
}

ssize_t
output_write(int descriptor, const void *data, size_t length)
{
    (void)descriptor;
    ++write_calls;
    if (write_calls == fail_call) {
        output_errno = EIO;
        return short_write ? (ssize_t)(length - 1) : -1;
    }
    if (length > sizeof(captured) - captured_length)
        return -1;
    memcpy(captured + captured_length, data, length);
    captured_length += length;
    return (ssize_t)length;
}

static void
check(int condition, const char *message)
{
    if (!condition) {
        ++failures;
        (void)write(2, message, strlen(message));
        (void)write(2, "\n", 1);
    }
}

static int
call_vfprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int
call_doprnt(FILE *stream, const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = _doprnt(format, arguments, stream);
    va_end(arguments);
    return result;
}

static int
call_vprintf(const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

static int
call_vsprintf(char *buffer, const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return result;
}

static void
reset(FILE *stream)
{
    memset(stream, 0, sizeof(*stream));
    stream->_flag = _IOWRT | _IONBF;
    stream->_file = 42;
    write_calls = 0;
    captured_length = 0;
    fail_call = 0;
    short_write = 0;
    output_errno = 0;
}

static void
check_released(FILE *stream)
{
    check(stream->_base == NULL && stream->_ptr == NULL &&
        stream->_cnt == 0 && stream->_bufsiz == 0 &&
        (stream->_flag & _IONBF), "FAIL output temporary buffer release");
}

int
main(void)
{
    FILE stream;
    char text[2 * BUFSIZ + 2];
    char buffer[16];
    unsigned operation;
    int result;

    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    for (operation = 0; operation < 3; ++operation) {
        reset(&stream);
        if (operation == 0)
            result = fprintf(&stream, "%s:%d", "x", 42);
        else if (operation == 1)
            result = call_vfprintf(&stream, "%s:%d", "x", 42);
        else
            result = fputs("x:42", &stream);
        check(operation == 2 ? result >= 0 : result == 4,
            "FAIL output success return");
        check(captured_length == 4 && memcmp(captured, "x:42", 4) == 0 &&
            write_calls == 1, "FAIL output bounded write count");
        check_released(&stream);

        reset(&stream);
        stream._base = stream._ptr = (char *)stream._ub;
        stream._bufsiz = 1;
        result = operation == 0 ? fprintf(&stream, "%s", "abc") :
            operation == 1 ? call_vfprintf(&stream, "%s", "abc") :
            fputs("abc", &stream);
        check(result >= 0 && stream._base == (char *)stream._ub &&
            stream._ptr == (char *)stream._ub && stream._bufsiz == 1,
            "FAIL output retained stream buffer");

        for (short_write = 0; short_write <= 1; ++short_write) {
            int mode = short_write;
            reset(&stream);
            short_write = mode;
            fail_call = 1;
            result = operation == 0 ? fprintf(&stream, "%s", "abc") :
                operation == 1 ? call_vfprintf(&stream, "%s", "abc") :
                fputs("abc", &stream);
            check(result == EOF && ferror(&stream) && output_errno == EIO,
                "FAIL output final flush error");
            check_released(&stream);
        }
    }
    for (operation = 0; operation < 3; ++operation) {
        unsigned ownership;
        for (ownership = 0; ownership < 2; ++ownership) {
            unsigned initial_mode;
            char *retained = ownership ? malloc(16) : buffer;
            check(retained != NULL, "FAIL output fixture allocation");
            if (retained == NULL)
                return 1;
            for (initial_mode = 0; initial_mode < 2; ++initial_mode) {
                unsigned inject_failure;
                for (inject_failure = 0; inject_failure < 2; ++inject_failure) {
                    reset(&stream);
                    stream._flag = _IORW | _IONBF |
                        (ownership ? _IOMYBUF : 0) |
                        (initial_mode ? _IOREAD | _IOEOF : 0);
                    stream._base = retained;
                    stream._ptr = retained + 3;
                    stream._bufsiz = 16;
                    fail_call = inject_failure;
                    result = operation == 0 ? fprintf(&stream, "%s", "abc") :
                        operation == 1 ? call_vfprintf(&stream, "%s", "abc") :
                        fputs("abc", &stream);
                    check(inject_failure ? result == EOF : result >= 0,
                        "FAIL output update stream result");
                    check(stream._base == retained && stream._ptr == retained + 3 &&
                        stream._bufsiz == 16 && stream._cnt == 0,
                        "FAIL output update stream retained buffer");
                    check((stream._flag & (_IORW | _IONBF | _IOWRT)) ==
                        (_IORW | _IONBF | _IOWRT) &&
                        !(stream._flag & (_IOREAD | _IOEOF)) &&
                        !!(stream._flag & _IOMYBUF) == !!ownership &&
                        !!ferror(&stream) == !!inject_failure,
                        "FAIL output update stream flag propagation");
                }
            }
            if (ownership)
                free(retained);
        }
    }
    reset(&stream);
    result = call_vfprintf(&stream, "%s", text);
    check(result == (int)sizeof(text) - 1 && write_calls == 3 &&
        captured_length == sizeof(text) - 1,
        "FAIL output large formatted chunks");
    check_released(&stream);

    reset(&stream);
    fail_call = 1;
    result = fputs(text, &stream);
    check(result == EOF && write_calls == 1 && ferror(&stream),
        "FAIL output stops after fputs failure");
    check_released(&stream);

    reset(&stream);
    stream._flag = _IOWRT;
    stream._base = stream._ptr = buffer;
    stream._bufsiz = sizeof(buffer);
    stream._cnt = sizeof(buffer);
    check(fprintf(&stream, "%s:%d", "x", 42) == 4 &&
        stream._ptr == buffer + 4 && write_calls == 0,
        "FAIL output buffered count");
    reset(stdout);
    check(printf("%s:%d", "x", 42) == 4 && captured_length == 4,
        "FAIL output printf count");
    reset(stdout);
    check(call_vprintf("%s:%d", "x", 42) == 4 && captured_length == 4,
        "FAIL output vprintf count");
    reset(&stream);
    fail_call = 1;
    {
        int counted = -1;
        check(call_doprnt(&stream, "abc%n", &counted) == EOF &&
            counted == -1 && write_calls == 1 && output_errno == EIO,
            "FAIL output formatter stops before later count store");
    }
    reset(&stream);
    stream._flag = _IOREAD;
    check(call_doprnt(&stream, "x") == EOF && write_calls == 0,
        "FAIL output formatter rejects read-only stream");
    output_errno = 0;
    check(sprintf(buffer, "%2147483648d", 1) == EOF &&
        buffer[0] == '\0' && output_errno == EOVERFLOW,
        "FAIL output sprintf propagates overflow");
    output_errno = 0;
    check(call_vsprintf(buffer, "%2147483648d", 1) == EOF &&
        buffer[0] == '\0' && output_errno == EOVERFLOW,
        "FAIL output vsprintf propagates overflow");
    {
        char *large = malloc(32769);
        check(large != NULL, "FAIL output large fixture allocation");
        if (large == NULL)
            return 1;
        check(sprintf(large, "%32768s", "") == 32768 &&
            large[0] == ' ' && large[32767] == ' ' && large[32768] == '\0',
            "FAIL output sprintf capacity exceeds historical short bound");
        free(large);
    }
    check(sprintf(buffer, "%lld:%d", 1LL, 7) == 3 &&
        strcmp(buffer, "1:7") == 0,
        "FAIL output linked wide helper consumes its argument");
    return failures != 0;
}
