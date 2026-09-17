/*
 * Host harness for machine-independent kernel sources.
 *
 * A gate in this directory compiles a file from sys/kern unchanged and links
 * it against this harness, so the gate exercises the code the board runs
 * rather than a second copy of its algorithm written for the test.
 *
 * This header names no type the kernel also names, and it includes no system
 * header at all. That is the harness's central constraint, not a style
 * choice. sys/sys/types.h reads "typedef u_int size_t", so a kernel source
 * sees a 32-bit size_t, while <stddef.h> gives a host source a 64-bit one; a
 * gate that reached both would pass an array of one width to a kernel
 * function writing the other, and the kernel would write two of its values
 * into the gate's first element. Types cross the boundary in exactly one
 * place, hostkern_kern.c, which includes the kernel's own headers and so
 * uses the kernel's own widths.
 *
 * <stdio.h> is out of reach for a second reason: the Makefile renames the
 * kernel's printf to hk_kprintf, because a host binary links libc and a
 * kernel printf() definition would take over every libc call to that name.
 * sys/kern/subr_rmap.c's malloc() and mfree() move aside for the same reason,
 * taking a resource map where libc's take a byte count. A gate calls
 * hk_note() for its own output.
 *
 * panic() does not return. Inside hk_expect_panic() it returns control to the
 * harness, which turns each defensive check in the kernel source into an
 * assertion a gate can state. Outside one, it prints the message and exits
 * nonzero, so an unexpected panic fails the gate rather than unwinding into
 * unrelated code.
 */
#ifndef HOSTKERN_H
#define HOSTKERN_H

/*
 * Kernel entry points the harness implements. Neither names a type the
 * kernel and the host disagree about; the ones that do live in
 * hostkern_kern.c and are declared only by the kernel's own headers.
 */
void panic(char *msg);
void hk_kprintf(char *fmt, ...);

/* Text the kernel source passed to printf since the last hk_reset_output(). */
extern char hk_printf_text[1024];
extern unsigned hk_printf_calls;
void hk_reset_output(void);

/* Verdict accounting. */
extern unsigned hk_checks;
extern unsigned hk_failures;
void hk_fail(const char *file, int line, const char *what);
void hk_note(const char *fmt, ...);
int hk_verdict(const char *suite);

/* String tests for gates, which cannot reach <string.h> either. */
int hk_streq(const char *a, const char *b);
int hk_contains(const char *haystack, const char *needle);

#define HK_CHECK(cond) do {						\
	hk_checks++;							\
	if (!(cond))							\
		hk_fail(__FILE__, __LINE__, #cond);			\
} while (0)

/*
 * Run fn and require that it panics with exactly the given message. fn runs
 * inside the harness frame that holds the jump buffer, which is what makes
 * the return from panic() defined.
 */
void hk_expect_panic(const char *file, int line, const char *expected,
    void (*fn)(void), const char *what);

#define HK_EXPECT_PANIC(expected, fn)					\
	hk_expect_panic(__FILE__, __LINE__, expected, fn, #fn)

#endif /* HOSTKERN_H */
