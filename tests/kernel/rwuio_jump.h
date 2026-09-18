/*
 * The harness side of rwuio_setjmp.h: the jump the file operation stub
 * makes, declared without <setjmp.h> so a unit that carries the kernel's
 * sys/types.h can include it.
 */
void hk_qsave_longjmp(const void *env, int value);
