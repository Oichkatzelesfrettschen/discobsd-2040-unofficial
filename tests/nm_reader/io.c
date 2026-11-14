/* Stream failure and allocation ownership controls for the native reader. */
#include <stdio.h>
#include <stdlib.h>

static unsigned io_calls, io_failure, outstanding;

static int
fail_io(void)
{
    return ++io_calls == io_failure;
}

static int
reader_seek(FILE *stream, long offset, int whence)
{
    return fail_io() ? -1 : fseek(stream, offset, whence);
}

static long
reader_tell(FILE *stream)
{
    return fail_io() ? -1L : ftell(stream);
}

static size_t
reader_read(void *buffer, size_t size, size_t count, FILE *stream)
{
    return fail_io() ? 0 : fread(buffer, size, count, stream);
}

static int
reader_getc(FILE *stream)
{
    return fail_io() ? EOF : fgetc(stream);
}

static int
reader_close(FILE *stream)
{
    int failure = fail_io();
    int status = fclose(stream);

    return failure ? EOF : status;
}

static int
reader_flush(FILE *stream)
{
    return fail_io() ? EOF : fflush(stream);
}

static void *
reader_allocate(size_t size)
{
    void *pointer = malloc(size);

    if (pointer)
        outstanding++;
    return pointer;
}

static void
reader_release(void *pointer)
{
    if (pointer) {
        if (outstanding == 0)
            abort();
        outstanding--;
    }
    free(pointer);
}

#undef getc
#define getc reader_getc
#define fread reader_read
#define fseek reader_seek
#define ftell reader_tell
#define fclose reader_close
#define fflush reader_flush
#define malloc reader_allocate
#define free reader_release
#define main nm_main
#include "../../usr.bin/nm/nm.c"
#undef main
#undef free
#undef malloc
#undef fflush
#undef fclose
#undef ftell
#undef fseek
#undef fread
#undef getc

int
main(int argc, char **argv)
{
    char *arguments[3];
    int status;

    if (argc != 3)
        return 2;
    io_failure = (unsigned)strtoul(argv[1], NULL, 10);
    arguments[0] = argv[0];
    arguments[1] = argv[2];
    arguments[2] = NULL;
    status = nm_main(2, arguments);
    fprintf(stderr, "io-control calls=%u outstanding=%u status=%d\n",
        io_calls, outstanding, status);
    return outstanding != 0 || (io_failure ?
        (io_calls < io_failure || status == 0) : status != 0);
}
