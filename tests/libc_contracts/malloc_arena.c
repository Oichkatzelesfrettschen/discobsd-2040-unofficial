/*
 * lib/libc/gen/malloc.c compiled for the host over the test's sbrk().
 *
 * The host's unistd.h declares sbrk(intptr_t), and on Darwin marks it
 * deprecated. A -Dsbrk=test_sbrk on the command line would rewrite that
 * declaration too, so test_sbrk would inherit the host's parameter width
 * and its deprecation, and -Werror would refuse the object on Darwin. This
 * unit includes the host headers first, so the host's sbrk keeps its own
 * name, then declares test_sbrk with the target's int parameter and only
 * afterwards renames the allocator's calls. The allocation names move
 * aside on the command line, which the host headers tolerate.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

void *test_sbrk(int);
#define sbrk test_sbrk

#include "../../lib/libc/gen/malloc.c"
