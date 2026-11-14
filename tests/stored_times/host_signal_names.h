/*
 * Forced into the two units of the make fixture ahead of their own text.
 *
 * usr.bin/make/doname.c declares the signal name table as the target libc
 * has it, `extern char *sys_siglist[]`. A host whose <signal.h> declares a
 * table of that name with another type (macOS: const char *const [NSIG])
 * rejects the redeclaration. Reading the host's <signal.h> here, before
 * the name is redirected, leaves the host's own declaration untouched;
 * doname.c's declaration and its uses then name the table make_order.c
 * defines.
 */
#include <signal.h>

#define sys_siglist stored_times_siglist
