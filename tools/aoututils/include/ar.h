/*
 * Host build shim. The canonical header lives in the tree's include
 * directory; this copy exists only so it outranks the host's own
 * <ar.h> on platforms that ship one.
 */
#include "../../../include/ar.h"
