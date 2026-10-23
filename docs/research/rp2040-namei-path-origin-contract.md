# RP2040 `namei` pathname-origin contract

The pathname lookup interface formerly encoded user versus kernel address
space through two similarly named macros and several direct writes to
`struct nameidata`. The direct writes in `link`, `rename`, and `vn_open`
made preservation of `NI_USERPATH` a caller convention. A new call could use
the kernel-copy path for a user pointer without changing the public ABI or
producing a compiler diagnostic.

`sys/sys/namei.h` now provides three interfaces with distinct contracts:

- `NAMEI_INIT_USER` sets the operation, modifiers, pathname, and
  `NI_USERPATH`.
- `NAMEI_INIT_KERNEL` sets the operation, modifiers, and kernel-owned
  pathname while clearing `NI_USERPATH`.
- `NAMEI_RESET_OPERATION` changes only the operation and modifiers while
  retaining the established pathname and origin.

A preprocessing assertion keeps `NI_USERPATH` disjoint from every operation
and modifier bit. `struct nameidata`, its fields, the bit values, syscall
numbers, and calling conventions remain unchanged.

## Complete kernel transition inventory

The migration replaces every initializer and direct origin-field write under
`sys/kern`. The finite inventory contains 25 user paths, two kernel-owned
paths, and three operation-reset cases. `vn_open` shares one reset expression
between its exclusive and nonexclusive create cases so Thumb-1 code generation
does not duplicate the origin-preservation sequence.

| Translation unit | User initialization | Kernel initialization | Operation-reset case |
| --- | ---: | ---: | ---: |
| `exec_script.c` | 0 | 1 | 0 |
| `kern_exec.c` | 1 | 0 | 0 |
| `kern_sig.c` | 0 | 1 | 0 |
| `ufs_mount.c` | 2 | 0 | 0 |
| `ufs_syscalls.c` | 20 | 0 | 0 |
| `ufs_syscalls2.c` | 2 | 0 | 0 |
| `vfs_vnops.c` | 0 | 0 | 3 |
| **Total** | **25** | **2** | **3** |

`tests/rp2040/namei_path/check_origin.py` reads every kernel C translation
unit and compares the normalized operation, modifiers, and pathname expression
at 29 call sites covering all 30 transitions. The checker also rejects the old
initializers, direct `ni_dirp` or `ni_nameiop` plain or compound assignment
through either structure-access form, and an initializer in an unexpected
translation unit. Five self-mutations change an origin, change a pathname
expression, restore a direct plain write, add a compound origin-clearing write,
and add an unexpected caller; every mutation must be rejected.

The strict-C17 fixture includes `sys/namei.h` twice and executes user and
kernel initialization plus both origin-preserving reset cases. The existing
address and undefined-behavior sanitizer cases retain invalid-pointer,
process-window boundary, missing-terminator, maximum-length, and kernel-copy
coverage. One compiled mutation sends a user pointer through `copystr`; a
second drops the origin during an operation reset. Both executables must fail
their oracles. The five inventory mutations and two compiled mutations provide
seven calibrated rejection cases.

## Dependency and static-resource evidence

The paired measurements bind parent `3957321ea844991b78325a8d9ab484f5aa71c9ba`
to corrected implementation commit `45fa6c10d5ddaa1d9b8cd8b177626203933aa96f`.
Both clean distributions use Arm GCC 16.2.0, GNU ld 2.47.20260726, and bmake
20260912. Parent clean and distribution stages completed in 4.5 and 104.5
seconds; candidate stages completed in 4.6 and 103.0 seconds.

Both dependency closures account for 36 shipped artifact groups, 173 PICO
C/header paths, 165 PICO_UART C/header paths, and the same 151-member,
43,010-byte native board `libc.a`. The parent clean and distribution receipt
SHA-256 values are
`4fc04af8756ef00339cde196716f233ccfa0feb06c549349bf3a490d58177540`
and `7bdf9c5afc54a9f5c933a6156c41070eae51192da57c38d9f1a2873d8e5bb7e6`.
The candidate values are
`3c81cce3cff2e2e2f5d5a9d9a2587f6a5224e1a7eea8a48e1ba41fc863b6cf21`
and `7eb0a4865be538096df27c2ed454217003389ce0913a3181d9ce2c2dbd4cccb8`.

The parent source, closure, and schema-2 resource receipts have SHA-256 values
`8e1435565c2ce1edf3173086c9552416365a642d5f0620f61c426a6260582802`,
`17890b7f194c8b75a877e2d1dd8ef8996c50a5a819256de63175d8a29b746909`,
and `038d6379af1138e1c377f94dbfa49e75c20ec5230885ebf04ec45098e2b3d9cb`.
The candidate values are
`335fd57e6fc3e09e7b1695b6f648b5efc8553ed426a88b51be8582ec8c5dbba4`,
`1877eff281c13de9340c80ffa62a308ef71ef96412ffdd82d239bc883b25337b`,
and `9c5347e39635b959c195a31597cc85227b4da454362cf7fb55009c68d5595eb4`.
The comparison receipt has SHA-256
`5db9a2fb4ef2fb07eae27d5ee701998b379e6c30fd9b2739a482fb8b51e425e2`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,920 | 104,920 | 0 | 26,152 bytes |
| PICO kernel SRAM allocation / extent | 32,216 / 32,232 | 32,216 / 32,232 | 0 / 0 | unchanged |
| PICO scratch allocation / extent | 8,192 / 8,192 | 8,192 / 8,192 | 0 / 0 | unchanged |
| PICO_UART loaded flash span | 92,436 | 92,428 | -8 | 38,644 bytes |
| PICO_UART kernel SRAM allocation / extent | 15,336 / 15,344 | 15,336 / 15,344 | 0 / 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

The exact recorded PICO and PICO_UART compiler commands were replayed with
`-fstack-usage` and reproducible path maps for `ufs_namei.c` and the seven
caller translation units. Fourteen object comparisons retain identical text,
data, BSS, symbol sizes, and static frames. Both `vfs_vnops.c` objects lose
four text bytes in `vn_open`; their data, BSS, and static frames remain
unchanged. The parent and candidate object receipt SHA-256 values are
`264e14db691ac56ecf7e480f42dd364e52c60bf2eaa9a5ca2b3c4484e3c1d442`
and `e4dad9ce63a4229908cc1f0f26b4af7945168126c2bb917d646da20074ac610d`.
The comparison receipt has SHA-256
`986bc40980ef22df882b188f89016947df624a220526dbb5973878a5526d106b`.

The static results establish the compiled interface and measured resource
surfaces. The host fixture does not execute each syscall, the complete lookup
algorithm, MPU enforcement, or RP2040 silicon. Runtime kernel stack, heap, and
storage traffic remain unmeasured. The broader `namei` traversal and name-cache
semantics remain migration-open, and directory-relative archive identity is a
separate additive interface unit. Physical-board execution requires separate
authorization for the exact image and workload.
