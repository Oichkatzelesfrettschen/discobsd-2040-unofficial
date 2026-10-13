/* Exact utility source uses target layouts and an injected syscall boundary. */
#include <sys/param.h>
#include <sys/capacity.h>
#include <sys/storage_stats.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
FILE _iob[3];
int status_errno;
int status_print(int, const char *, va_list);
int status_fprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = status_print(stream == stderr, format, arguments);
    va_end(arguments);
    return result;
}
void status_perror(const char *label)
{
    status_fprintf(stderr, "%s: injected syscall failure\n", label);
}
char *status_devname(dev_t device, mode_t mode)
{
    (void)device;
    (void)mode;
    return "fixture";
}
int status_getloadavg(u_int *loads, int count)
{
    const char *scenario = getenv("SCENARIO");
    int index;
    for (index = 0; index < count; index++)
        loads[index] = 100;
    return scenario && strcmp(scenario, "load-failure") == 0 ? -1 : count;
}
int status_sysctl(int *name, u_int length, void *old, size_t *size,
    void *value, size_t value_size)
{
    const char *scenario = getenv("SCENARIO");
    (void)length;
    status_fprintf(stderr, "visit %d %d write %u\n", name[0], name[1],
        (unsigned)value_size);
    if (name[0] == CTL_DEBUG || (scenario && strcmp(scenario, "unavailable") == 0)) {
        status_errno = EOPNOTSUPP;
        return -1;
    }
    if (name[0] == CTL_KERN && name[1] == KERN_CAPACITY) {
        struct kernel_capacity_stats stats;
        memset(&stats, 0, sizeof(stats));
        stats.version = KERNEL_CAPACITY_VERSION;
        stats.size = sizeof(stats);
        if (scenario && strcmp(scenario, "version") == 0) stats.version++;
        if (scenario && strcmp(scenario, "record-size") == 0) stats.size--;
        memcpy(old, &stats, sizeof(stats));
        *size = sizeof(stats);
    } else if (name[0] == CTL_MACHDEP && name[1] == CPU_STORAGE_STATS) {
        struct storage_stats stats;
        memset(&stats, 0, sizeof(stats));
        stats.version = STORAGE_STATS_VERSION;
        stats.size = sizeof(stats);
        if (scenario && strcmp(scenario, "version") == 0) stats.version++;
        if (scenario && strcmp(scenario, "record-size") == 0) stats.size--;
        memcpy(old, &stats, sizeof(stats));
        *size = sizeof(stats);
    } else if ((name[0] == CTL_HW && name[1] == HW_NCPU) ||
        (name[0] == CTL_KERN && (name[1] == KERN_MAXPROC || name[1] == KERN_MAXFILES)) ||
        (name[0] == CTL_MACHDEP && name[1] == CPU_MPU)) {
        *(int *)old = value ? *(int *)value : 1;
        *size = sizeof(int);
    } else {
        status_errno = EOPNOTSUPP;
        return -1;
    }
    if (scenario && strcmp(scenario, "length") == 0) (*size)--;
    return 0;
}
