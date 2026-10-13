# Directory close result and ownership

`closedir()` returns the result of one `close()` attempt and releases the
stream allocation on either result. The wrapper preserves the errno value
from `close()` across `free()`. The public declaration changes from `void`
to `int`; `DIR` remains 1,040 bytes with its intact 1 KiB block.

Issue 7 and Issue 8 require an integer return and failure reporting. Existing
callers that discard the result remain valid C. A caller assigning the old
function to a pointer returning void must update its type. The ARM calling
convention retains the argument in r0 and supplies the new result in r0;
old callers that ignore r0 retain their call sequence. Rebuilt consumers
establish the source transition. Other architecture ABIs remain unmeasured.

`sys/kern/kern_descrip.c:close` removes the descriptor from `u.u_ofile`
before `closef()` returns its error. Retrying after a close error can address
an unrelated descriptor if the number is reused. The directory wrapper
therefore consumes its stream exactly once and never retries. A caller must
not use the pointer after either return. Invalid or already-freed DIR
pointers remain outside the interface contract.

## Calibrated behavior

The stream fixture injects EBADF, EIO and EINTR from close and ENOMEM from
free. The original implementation at
`236890cd777e6023e9bbc7f43638ad84aa813431` fails all three errno-preservation
assertions. The changed fixture also checks zero on success, minus one on
failure, one close and one free after either result. Native, ILP32 and
ASan/UBSan executions pass. Four persistent mutations suppress the failure
result, suppress errno restoration, skip free or repeat close; each must
exit 1 with its named assertion. Existing opener and scanner controls run
alongside the close controls.

The Smaller C `t18_stdio` consumer allocates a DIR with descriptor -1 and
checks `closedir() == -1` and EBADF. The consumer compiles through Smaller C,
links the production libc member and executes under qemu-arm. The copied
archive refill mutation still fails at its expected assertion; restoring the
archive passes. Its map check also requires `closedir.o`.

The native consumer exposes an independent test-runtime defect: the Linux
EABI shim returns raw negative errno values from close. The shim now
translates Linux EINTR, EIO, EBADF, ENOSPC and EDQUOT to the DiscoBSD namespace
and returns -1. An unmodeled error exits the test with status 125. Other
syscall shims retain their own limitations. The QEMU syscall trace observes
EBADF for close(-1). The consumer passes after the boundary repair.
On-device preprocessing and board execution remain separate open gates.

## Paired resource measurements

The captured baseline is the clean opener distribution at `236890cd777e6023e9bbc7f43638ad84aa813431`.
The candidate changes closedir, its public declaration and its tests/manual.
Both distributions use the same Cortex-M0+ GNU17 configuration. The
measurement tools report arm-none-eabi GCC 16.2.0, host GCC 16.2.1,
bmake 20260912 and qemu-arm 11.1.2. The baseline worktree retires after
integration; the CSV, JSON comparison receipt and extracted archive objects
retain the comparison evidence. A fresh checkout at the baseline revision
replays the raw binaries.
`libc-closedir-linked-extents.csv` records all 275 installed OMAGIC files,
including SHA-256 and text/data/BSS/file extents for each side.

| Surface | Before | After |
| --- | ---: | ---: |
| closedir ELF text | 18 | 32 |
| closedir ELF data/BSS | 0/0 | 0/0 |
| closedir static frame (`-fstack-usage`) | 8 | 24 |
| Full ELF libc bytes / members | 406756 / 380 | 406816 / 380 |
| Full a.out libc bytes / members | 115902 / 370 | 115934 / 370 |
| Selected board libc bytes / members | 56058 / 151 | 56058 / 151 |
| Root free blocks | 265 | 265 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

All 150 extracted board archive objects compare byte-identically; closedir
is absent from the selected archive. Archive container hashes vary, so the
member comparison establishes the object-content result. Named packed
consumers grow by 3 to 27 bytes. Multicall aliases share their owner's
storage and must not multiply the storage charge. The resource cost buys
observable close failure without permanent RAM or another allocation.

## Replay and remaining work

Select and export PYTHON before each gate. Build separate clean baseline
and candidate worktrees with `bmake -j4 MACHINE=rp2040 distribution`.
Run `bmake MACHINE=rp2040 check-dirent-contracts check-scandir-contracts
check-dirent-contracts-cross check-test-execution check-lint`, then
`bmake MACHINE=rp2040 check`. The parallel aggregate passes after the QEMU
close-boundary repair. A serial confirmation avoids inherited bmake jobserver
option diagnostics from child probes. The existing host shell and libtcl links emit
glibc diagnostics for tmpnam, getwd and mktemp; production C compilation treats
warnings as errors. Compare `size`,
archive member lists and extracted objects, the OMAGIC headers, and
`fsutil -v --partition=1` output. Compile closedir with the evaluated libc
CC/CFLAGS/CPPFLAGS plus `-fstack-usage` for the static-frame comparison.

`getwd()` separately reads `dir->d_name` after closedir frees its owning
stream. The lifetime correction requires its own regression and focused
integration. The close-result unit preserves scandir's existing decision
to return the completed entry list after a close error; earlier scanner
failures retain their deciding errno through cleanup.
