/*
 * Calibration input for kernel_format_check.py: a header a kernel source
 * includes. Its inline function's format must be reported.
 */
static inline void
kernel_header_helper(int i)
{
    printf("%i %s\n", i, "x");
}
