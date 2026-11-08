/*
 * Layout contract for struct exec_params, the per-exec state that sys/kern
 * keeps on the kernel stack and that the exec loaders fill in. The header
 * compiles as strict ISO C17 here, so a GNU-only construct in
 * sys/sys/exec.h or in a header it includes fails this gate, and the
 * assertions pin the size and member offsets the Cortex-M0+ ILP32 ABI gives
 * the structure: the interpreter-script and ELF states share one anonymous
 * union at offset 92 that ends at the group id at 176.
 */
#define KERNEL 1
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>

#ifndef EXEC_LAYOUT_EXPECTED_SIZE
#define EXEC_LAYOUT_EXPECTED_SIZE 284
#endif

_Static_assert(sizeof(struct exec_params) == EXEC_LAYOUT_EXPECTED_SIZE,
    "struct exec_params size changed");
_Static_assert(_Alignof(struct exec_params) == 4,
    "struct exec_params alignment changed");
_Static_assert(__builtin_offsetof(struct exec_params, userfname) == 0,
    "struct exec_params userfname offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, hdr) == 12,
    "struct exec_params hdr offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, hdr_len) == 76,
    "struct exec_params hdr_len offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, argc) == 80,
    "struct exec_params argc offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, flags) == 88,
    "struct exec_params flags offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, sh) == 92,
    "struct exec_params sh offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, elf) == 92,
    "struct exec_params elf offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, gid) == 176,
    "struct exec_params gid offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, uid) == 180,
    "struct exec_params uid offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, alloc) == 184,
    "struct exec_params alloc offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, spool) == 192,
    "struct exec_params spool offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, ep_taddr) == 224,
    "struct exec_params ep_taddr offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, ip) == 240,
    "struct exec_params ip offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, text) == 244,
    "struct exec_params text offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, bss) == 260,
    "struct exec_params bss offset changed");
_Static_assert(__builtin_offsetof(struct exec_params, stack) == 276,
    "struct exec_params stack offset changed");

void
exec_layout_contract(void)
{
}
