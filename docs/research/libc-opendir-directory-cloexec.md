# Opened-descriptor directory admission and close-on-exec

`opendir()` validates the descriptor it opens and sets `FD_CLOEXEC` before
allocating its directory stream. Every failure after a successful open closes
that descriptor once and restores the deciding errno. The public signature
and 1,040-byte target `DIR` layout retain their existing ABI.

The source baseline is `72b6bbae3b8b4737e5b84ae63d9d96e0ca3904b7`.
The measured opener source hashes are
`83078ac5dfcc116099ccf36d0fadcb4ac24508a153f61da8c63428d331628a58`
for the baseline and
`f0fa380f36fb87011dfd1a7f60c99c2219591f473d2b952b69c5bcb73b605dc8`
for the candidate. The toolchain uses ARM GCC 16.2.0, host GCC 16.2.1,
Python 3.14.7 and bmake 20260912 with the default RP2040 configuration,
GNU17 production flags and `-Os` userland.

## Descriptor identity and lifetime

The inherited opener admits any successfully opened file. The candidate
calls `fstat()` on the opened descriptor and checks `S_ISDIR(st_mode)` before
allocating. A pathname lookup followed by a separate `stat()` would leave
descriptor identity unverified if a path changes between those operations.
The descriptor check binds validation to the object the stream actually uses.

The kernel already supplies the flag mechanism. `ufalloc()` in
`sys/kern/kern_descrip.c` clears a new descriptor's `u_pofile` entry.
Its `F_SETFD` branch replaces the descriptor's close-on-exec bit while
preserving its other kernel bits. `execsigs()` processing in
`sys/kern/exec_subr.c` is followed by the `UF_EXCLOSE` descriptor-close loop.
The production opener therefore needs one `F_SETFD` call; a production
`F_GETFD` readback would add a syscall without adding a return-time invariant.
The test reads back the flag through the shim's `F_GETFD` implementation.

The sequence is open, fstat, directory-type validation, F_SETFD, malloc,
initialization and publication. The post-open failure path saves errno before
close and restores it afterward. The path allocates only a successfully
admitted directory; failed metadata, invalid type and refused flag setup
consume zero stream allocations. A successful stream owns one descriptor
and one DIR allocation until closedir releases them.

Issue 7 and Issue 8 require directory admission. Issue 8 incorporates Austin
Group Defect 368 and requires FD_CLOEXEC for an associated opendir descriptor.
The tree implements separate open and fcntl syscalls; the flag is established
on return, while the interval between those syscalls remains a distinct
inheritance boundary. Atomic `O_CLOEXEC` admission requires a separately
designed kernel/public-flag change. The source search runs:

```sh
rg -n 'F_SETFD|FD_CLOEXEC' sys/sys/fcntl.h sys/kern/kern_descrip.c
rg -n 'u_pofile|UF_EXCLOSE' sys/kern/kern_descrip.c sys/kern/exec_subr.c
```

The host fixture checks syscall ordering and cleanup through deterministic
shims. Source inspection establishes the existing kernel flag path; a board
exec workload supplies a stronger execution class after explicit opt-in.
`closedir()`'s int result and `getwd()`'s post-close entry pointer remain
separate caller/lifetime units.

## Calibration and proof surface

The new fixture compiles the unchanged production opener and exits one at
named type, flag, ordering, metadata cleanup and flag-error assertions.
The candidate passes those assertions at native and ILP32 widths and under
ASan/UBSan. Five generated source mutations each fail the assertion for the
contract removed: directory-type admission, flag setup, flag-error handling,
descriptor cleanup and deciding errno. Cleanup calls deliberately overwrite
errno with EBUSY, so a cleanup mutation cannot pass through an incidental
unchanged error value.

The inherited stream tests continue to exercise readdir validation and
cookies. Their directory-block fixture explicitly carries struct direct's
alignment before typed accesses. The target layout gate pins the unchanged
block and DIR extents. The cross import gate requires fstat/fcntl in the
opener and rejects lseek in telldir; positive symbols, missing symbols,
forbidden symbols and an inspector failure calibrate that verdict.

## Paired resource account

The candidate clean distribution returns zero. The baseline is the captured
clean scanner distribution built from the same source implementation at the
named baseline, before the admission edit. The source hashes, frame outputs,
extracted archive objects and linked-extents ledger retain the comparison
evidence after the merged scanner worktree retires. A fresh checkout of the
named baseline and recorded toolchain replays the raw build. Both use identical manifest
selection and toolchain flags. The metadata validation adds stack storage
for a real struct stat and increases linked text; permanent stream storage
retains the 1,040-byte ABI.

| Artifact | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| ELF and a.out opendir text/data/BSS | 60 / 0 / 0 | 112 / 0 / 0 | +52 text |
| Opener static frame | 24 | 72 | +48 |
| Full ELF archive bytes / entries | 406644 / 380 | 406756 / 380 | +112 bytes |
| Full a.out archive bytes / entries | 115814 / 370 | 115902 / 370 | +88 bytes |
| Selected/installed archive bytes / entries | 56058 / 151 | 56058 / 151 | 0 |
| PICO kernel text/data/BSS | 104646 / 248 / 39848 | 104646 / 248 / 39848 | 0 |
| PICO_UART kernel text/data/BSS | 92220 / 192 / 14832 | 92220 / 192 / 14832 | 0 |
| Root free 1,024-byte blocks | 265 | 265 | 0 |
| `bin/box` text/data/BSS | 33468 / 1396 / 6804 | 33540 / 1396 / 6804 | +72 text |
| `bin/sh` text/data/BSS | 33264 / 2608 / 7532 | 33316 / 2608 / 7532 | +52 text |
| `usr/bin/login` text/data/BSS | 21832 / 689 / 3427 | 21884 / 689 / 3427 | +52 text |
| `sbin/adminbox` text/data/BSS | 25648 / 1716 / 2532 | 25700 / 1716 / 2532 | +52 text |
| `usr/bin/du` text/data/BSS | 8960 / 204 / 1160 | 9032 / 204 / 1160 | +72 text |

The opener imports close, errno, fcntl, fstat, malloc and open. Its local
cost is 52 text bytes; a consumer's marginal cost also includes either newly
needed syscall stub. [libc-opendir-linked-extents.csv](libc-opendir-linked-extents.csv)
records the same 275 regular OMAGIC program paths, header extents, file
lengths and hashes. Forty-four file hashes change; pdc embeds __TIME__/__DATE__
and retains its extents, while linked opener consumers change their text.
The install tree exceeds the selected manifest, and the CSV sum supplies
an installed-build account rather than simultaneous RAM or shipped flash.

The selected board archive excludes opendir and fcntl. Its 150 extracted
object members match byte for byte; its 151st entry is the symbol index.
Archive timestamps change complete archive hashes. Packed consumers grow
by 44 through 60 bytes in the observed listing; filesystem block rounding
retains 265 free blocks. Hard-link aliases refer to the same installed file,
so summing every listed alias would multiply the storage account.

Static frames establish individual compiler frames, with caller/callee
nesting, allocator stack and board high-water remaining separate accounts.
Successful admission allocates the same single DIR as the baseline; failed
metadata/type/flag checks avoid that allocation. Board timing, process peak,
kernel high-water and flash traffic remain unmeasured.

## Replay

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-dirent-contracts
bmake MACHINE=rp2040 check-dirent-contracts-cross
bmake MACHINE=rp2040 check-test-execution check-lint
bmake -j4 MACHINE=rp2040 distribution
bmake -j4 MACHINE=rp2040 check
arm-none-eabi-size lib/libc/gen/opendir.o
arm-none-eabi-nm -u lib/libc/gen/opendir.o
tools/bin/rp2040/size lib/libc_aout/libc/opendir.o
tools/bin/rp2040/ar t distrib/obj/boardlibc.rp2040/libc.a
tools/bin/rp2040/fsutil -v --partition=1 distrib/rp2040/sdcard.img
```

Use clean separate baseline/candidate worktrees for consumer links. Read the
evaluated CC/CFLAGS/CPPFLAGS in lib/libc/gen with bmake's `-v` options, compile
each revision's opener into a retained evidence directory with those flags
plus `-fstack-usage`, and read the matching .su. The linked CSV reads the eight
little-endian 32-bit a.out header words, admits OMAGIC 0407, takes words one
through three as text/data/BSS, and requires an equal program-path denominator.
The execution inventory reaches native/control/sanitizer companions through
the designated ILP32 recipe; unsupported optional runtimes report skips.

The directory native, ILP32 and ASan/UBSan lanes, five mutation controls,
calibrated cross inspection, execution-inventory tests and repository lint
return zero. The complete non-hardware aggregate returns zero after the clean
candidate distribution. Smaller C t18_stdio and its damaged-archive control
pass in that aggregate. The touched manual passes mandoc lint.

The aggregate's libtcl host fixtures emit inherited glibc linker diagnostics
for getwd and mktemp. Those diagnostics belong to the libtcl host-link lane;
the production admission compile passes with -Wall -Wextra -Werror.
