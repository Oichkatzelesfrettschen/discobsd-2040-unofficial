/* Checkpoints execute in their caller's live frame, using ILP32 label storage. */
#ifndef UFS_DISK_CHECKPOINT_H
#define UFS_DISK_CHECKPOINT_H

#include <sys/types.h>

_Static_assert(sizeof(void *) == 4, "disk checkpoint fixture is ILP32");
_Static_assert(sizeof(label_t) >= 5 * sizeof(void *), "builtin checkpoint storage");
_Static_assert(_Alignof(label_t) >= _Alignof(void *), "builtin checkpoint alignment");

#undef setjmp
#define setjmp(state) __builtin_setjmp((void *)(state))

#endif
