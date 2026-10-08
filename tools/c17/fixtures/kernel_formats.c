/*
 * Calibration input for kernel_format_check.py. Each call marked with the
 * reject expectation must be reported; no other line may be.
 */
void printf(char *fmt, ...);
void tprintf(struct tty *tp, char *fmt, ...);
void log(int level, char *fmt, ...);

void
accepted(struct tty *tp, int i, long l, char *s, void *p)
{
    printf("%d %u %x %X %o %c %s %p %%\n", i, i, i, i, i, i, s, p);
    printf("%ld %lu %lx %-8s %08x %*d %.*s %#o %+d\n",
        l, l, l, s, i, i, i, i, s, i, i);
    printf("split " "literal %d\n", i);
    tprintf(tp, "%s: %d\n", s, i);
    log(3, "%s\n", s);
    /* printf("%f in a comment is not a call\n"); */
}

void
rejected(struct tty *tp, int i, long long ll, double d, char *fmt)
{
    printf("%i\n", i); /* expect: reject */
    printf("%lld\n", ll); /* expect: reject */
    printf("%f\n", d); /* expect: reject */
    printf("%zu\n", (unsigned)i); /* expect: reject */
    printf("%hhd\n", i); /* expect: reject */
    printf("%n\n", &i); /* expect: reject */
    printf("%b\n", i, "\10\1ONE"); /* expect: reject */
    printf("%b\n", i); /* expect: reject */
    printf("%D\n", fmt); /* expect: reject */
    printf(fmt, i); /* expect: reject */
    tprintf(tp, "%e\n", d); /* expect: reject */
    log(1, "%jd\n", ll); /* expect: reject */
}
