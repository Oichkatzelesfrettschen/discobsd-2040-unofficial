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
paths, and two operation resets.

| Translation unit | User initialization | Kernel initialization | Operation reset |
| --- | ---: | ---: | ---: |
| `exec_script.c` | 0 | 1 | 0 |
| `kern_exec.c` | 1 | 0 | 0 |
| `kern_sig.c` | 0 | 1 | 0 |
| `ufs_mount.c` | 2 | 0 | 0 |
| `ufs_syscalls.c` | 20 | 0 | 0 |
| `ufs_syscalls2.c` | 2 | 0 | 0 |
| `vfs_vnops.c` | 0 | 0 | 2 |
| **Total** | **25** | **2** | **2** |

`tests/rp2040/namei_path/check_origin.py` reads every kernel C translation
unit and compares the normalized operation, modifiers, and pathname expression
at all 29 transitions. The checker also rejects the old initializers, direct
`ni_dirp` or `ni_nameiop` assignment through either structure-access form, and
an initializer in an unexpected translation unit. Four self-mutations change
an origin, change a pathname expression, restore a direct write, and add an
unexpected caller; every mutation must be rejected.

The strict-C17 fixture includes `sys/namei.h` twice and executes user and
kernel initialization plus both origin-preserving reset cases. The existing
address and undefined-behavior sanitizer cases retain invalid-pointer,
process-window boundary, missing-terminator, maximum-length, and kernel-copy
coverage. One compiled mutation sends a user pointer through `copystr`; a
second drops the origin during an operation reset. Both executables must fail
their oracles.

## Dependency and static-resource evidence

The paired measurements bind parent `3957321ea844991b78325a8d9ab484f5aa71c9ba`
to implementation commit `157bea52324ac25552b6a7f406deb394696e170f`.
Both clean distributions use Arm GCC 16.2.0, GNU ld 2.47.20260726, and bmake
20260912. Parent clean and distribution stages completed in 4.5 and 104.5
seconds; candidate stages completed in 4.5 and 103.0 seconds.

Both dependency closures account for 36 shipped artifact groups, 173 PICO
C/header paths, 165 PICO_UART C/header paths, and the same 151-member,
43,010-byte native board `libc.a`. The parent clean and distribution receipt
SHA-256 values are
`4fc04af8756ef00339cde196716f233ccfa0feb06c549349bf3a490d58177540`
and `7bdf9c5afc54a9f5c933a6156c41070eae51192da57c38d9f1a2873d8e5bb7e6`.
The candidate values are
`e660efa8a5e4caba07298376802f5fcb4564caa4288944350c82c42e6e343f96`
and `de4fc86be5c58962e2eebc35152a4b939b592659067486168df1af530d249ca1`.

The parent source, closure, and schema-2 resource receipts have SHA-256 values
`8e1435565c2ce1edf3173086c9552416365a642d5f0620f61c426a6260582802`,
`17890b7f194c8b75a877e2d1dd8ef8996c50a5a819256de63175d8a29b746909`,
and `038d6379af1138e1c377f94dbfa49e75c20ec5230885ebf04ec45098e2b3d9cb`.
The candidate values are
`088ba3ddd313c6b12386857a41897eb15ccb90ac63eb7cdea875e98ececfcaa4`,
`fa4f4befb99002338f95c78097e9838cd0baf393230d359c66a804c6be8b4fa7`,
and `e2648ef2f50811982e508b329fcf15bba4269cd80d9ba21c7493a1ea81d1b632`.
The comparison receipt has SHA-256
`c9922e8f2a37fc99ecfca6d809d3d2b7309ad6d45e8df05705d099894e9a8a13`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,920 | 104,920 | 0 | 26,152 bytes |
| PICO kernel SRAM allocation / extent | 32,216 / 32,232 | 32,216 / 32,232 | 0 / 0 | unchanged |
| PICO scratch allocation / extent | 8,192 / 8,192 | 8,192 / 8,192 | 0 / 0 | unchanged |
| PICO_UART loaded flash span | 92,436 | 92,436 | 0 | 38,636 bytes |
| PICO_UART kernel SRAM allocation / extent | 15,336 / 15,344 | 15,336 / 15,344 | 0 / 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

The exact recorded PICO and PICO_UART compiler commands were replayed with
`-fstack-usage` and reproducible path maps for `ufs_namei.c` and the seven
caller translation units. All 16 object comparisons retain identical text,
data, BSS, symbol sizes, and static frames. The parent and candidate object
receipt SHA-256 values are
`264e14db691ac56ecf7e480f42dd364e52c60bf2eaa9a5ca2b3c4484e3c1d442`
and `26fbb130c0ca4a0315b04ba0aa046f9765265ec33ea7e3815a5d66b4550b93a2`.
The zero-delta comparison receipt has SHA-256
`ee1732b8ac1ff8451f307996488455e1189e573b2dd668142f1e27c08e5672b6`.

The static results establish the compiled interface and measured resource
surfaces. The host fixture does not execute each syscall, the complete lookup
algorithm, MPU enforcement, or RP2040 silicon. Runtime kernel stack, heap, and
storage traffic remain unmeasured. The broader `namei` traversal and name-cache
semantics remain migration-open, and directory-relative archive identity is a
separate additive interface unit. Physical-board execution requires separate
authorization for the exact image and workload.
