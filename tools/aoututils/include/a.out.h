/*
 * Host build shim. The canonical header lives in the tree's include
 * directory; this copy exists only so it outranks the host's own
 * <a.out.h> on platforms that ship one.
 */
#include "../../../include/a.out.h"
