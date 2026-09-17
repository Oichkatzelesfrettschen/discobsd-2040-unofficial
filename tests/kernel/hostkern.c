/*
 * Harness implementation. This file carries no kernel header, so it reaches
 * <setjmp.h>, <stdio.h> and <string.h> freely; hostkern_abi.c is the
 * translation unit that holds these definitions to the kernel's own
 * declarations.
 */
#include "hostkern.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf panic_jmp;
static int panic_armed;
static char panic_msg[128];

char hk_printf_text[1024];
unsigned hk_printf_calls;

unsigned hk_checks;
unsigned hk_failures;

void
hk_reset_output(void)
{
	hk_printf_text[0] = '\0';
	hk_printf_calls = 0;
}

/*
 * The kernel's panic. Armed, it returns control to hk_expect_panic();
 * unarmed, it ends the gate, because every caller in sys/kern treats panic as
 * the point past which the structure it was walking is no longer describable.
 */
void
panic(char *msg)
{
	snprintf(panic_msg, sizeof(panic_msg), "%s", msg);
	if (panic_armed) {
		panic_armed = 0;
		longjmp(panic_jmp, 1);
	}
	fprintf(stderr, "FAIL unexpected panic: %s\n", msg);
	exit(1);
}

void
hk_expect_panic(const char *file, int line, const char *expected,
    void (*fn)(void), const char *what)
{
	hk_checks++;
	panic_msg[0] = '\0';
	if (setjmp(panic_jmp) == 0) {
		panic_armed = 1;
		fn();
		panic_armed = 0;
		hk_note("  %s returned instead of panicking", what);
		hk_fail(file, line, "expected a panic");
		return;
	}
	panic_armed = 0;
	if (strcmp(panic_msg, expected) != 0) {
		hk_note("  %s panicked \"%s\", expected \"%s\"", what,
		    panic_msg, expected);
		hk_fail(file, line, "wrong panic message");
	}
}

/*
 * The kernel's printf, renamed away from libc's. Output accumulates so a gate
 * can assert on a diagnostic the kernel source emits, such as the resource
 * map overflow warning.
 */
void
hk_kprintf(char *fmt, ...)
{
	va_list ap;
	size_t used;

	hk_printf_calls++;
	used = strlen(hk_printf_text);
	va_start(ap, fmt);
	vsnprintf(hk_printf_text + used, sizeof(hk_printf_text) - used, fmt, ap);
	va_end(ap);
}

void
hk_fail(const char *file, int line, const char *what)
{
	hk_failures++;
	fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what);
}

void
hk_note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

int
hk_streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

int
hk_contains(const char *haystack, const char *needle)
{
	return strstr(haystack, needle) != NULL;
}

int
hk_verdict(const char *suite)
{
	if (hk_failures) {
		fprintf(stderr, "%s: %u of %u checks failed\n", suite,
		    hk_failures, hk_checks);
		return 1;
	}
	fprintf(stderr, "%s: %u checks passed\n", suite, hk_checks);
	return 0;
}
