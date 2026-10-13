/*
 * Measure the process extent sampled at syscalls, including its exec floor.
 * A touched frame that unwinds before a trap demonstrates the blind spot.
 * The output workload keeps a common caller frame above the stdio wrappers
 * so the initial allocation floor does not hide every batching difference.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CALIBRATION_BYTES 8192
#define CALLER_BYTES 2048
#define OUTPUT_BYTES 2049

static struct kinfo_proc process_info;
static char output[OUTPUT_BYTES + 1];
static volatile unsigned char witness;

static int
sample_extent(unsigned *extent)
{
    int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0 };
    size_t length = sizeof(process_info);

    name[3] = getpid();
    memset(&process_info, 0, sizeof(process_info));
    if (sysctl(name, 4, &process_info, &length, NULL, 0) < 0)
        return -1;
    if (length != sizeof(process_info) ||
        process_info.kp_proc.p_pid != name[3])
        return -1;
    *extent = process_info.kp_proc.p_ssize;
    return 0;
}

static __attribute__((noinline)) void
calibrate(int trap)
{
    volatile unsigned char padding[CALIBRATION_BYTES];
    unsigned index;

    for (index = 0; index < sizeof(padding); index++)
        padding[index] = (unsigned char)index;
    if (trap)
        witness = (unsigned char)getpid();
    witness = padding[sizeof(padding) - 1];
}

static int
format_variadic(FILE *stream, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static __attribute__((noinline)) int
run_output(FILE *stream, const char *operation, unsigned length)
{
    volatile unsigned char padding[CALLER_BYTES];
    unsigned index;
    int result;

    for (index = 0; index < sizeof(padding); index++)
        padding[index] = (unsigned char)index;
    if (strcmp(operation, "fprintf") == 0)
        result = fprintf(stream, "%s", output);
    else if (strcmp(operation, "vfprintf") == 0)
        result = format_variadic(stream, "%s", output);
    else
        result = fputs(output, stream);
    witness = padding[sizeof(padding) - 1];
    if (ferror(stream) || result < 0)
        return -1;
    if (strcmp(operation, "fputs") != 0 && result != (int)length)
        return -1;
    return 0;
}

int
main(int argc, char **argv)
{
    unsigned before, after, length = 0;
    char *end;
    unsigned long parsed;
    FILE *stream = NULL;
    int result = 0;
    int calibration;

    if (argc < 2)
        goto usage;
    calibration = strcmp(argv[1], "trap") == 0 ||
        strcmp(argv[1], "leaf") == 0;
    if (calibration) {
        if (argc != 2)
            goto usage;
    } else {
        if (argc != 3 || (strcmp(argv[1], "fprintf") != 0 &&
            strcmp(argv[1], "vfprintf") != 0 &&
            strcmp(argv[1], "fputs") != 0))
            goto usage;
        if (argv[2][0] < '0' || argv[2][0] > '9')
            goto usage;
        parsed = strtoul(argv[2], &end, 10);
        if (*end != '\0' || parsed > OUTPUT_BYTES)
            goto usage;
        length = (unsigned)parsed;
        memset(output, 'x', length);
        output[length] = '\0';
        stream = fopen("/dev/null", "w");
        if (stream == NULL)
            return 1;
        setbuf(stream, NULL);
    }
    if (sample_extent(&before) < 0) {
        if (stream != NULL)
            (void)fclose(stream);
        return 2;
    }
    if (calibration)
        calibrate(strcmp(argv[1], "trap") == 0);
    else
        result = run_output(stream, argv[1], length);
    if (sample_extent(&after) < 0)
        result = -1;
    if (stream != NULL && fclose(stream) < 0)
        result = -1;
    if (result < 0)
        return 3;
    printf("sampled-stack %s length=%u before=%u after=%u delta=%u\n",
        argv[1], length, before, after, after >= before ? after - before : 0);
    if (after < before)
        return 4;
    if (calibration && strcmp(argv[1], "trap") == 0 &&
        (after < CALIBRATION_BYTES || after <= before))
        return 5;
    return 0;
usage:
    fprintf(stderr, "usage: stdiocap trap|leaf|fprintf|vfprintf|fputs [0..2049]\n");
    return 64;
}
