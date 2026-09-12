/*
 * Both vendored heatshrink sources include <stdlib.h> unconditionally for
 * malloc and free. HEATSHRINK_DYNAMIC_ALLOC is 0 in heatshrink_config.h,
 * so neither is referenced and the kernel supplies neither; this header
 * exists so the include resolves under -nostdinc and the upstream .c files
 * stay byte-identical.
 */

#ifndef	_HEATSHRINK_COMPAT_STDLIB_H_
#define	_HEATSHRINK_COMPAT_STDLIB_H_

#include <sys/types.h>

#endif	/* !_HEATSHRINK_COMPAT_STDLIB_H_ */
