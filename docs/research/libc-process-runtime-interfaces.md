# Libc process and runtime interfaces

Commit `80a526fa` changes `getpgrp()`, selected `sysconf()` limits and the
reduced board libc archive. The measurement compares that commit with base
commit `145e19b7470ce6c783299ca7d7e4e33b72f57317`. Both images came from clean
RP2040 distribution builds. The measured tools were Python 3.14.7, bmake
20260912, arm-none-eabi-gcc 16.2.0 and GNU ld 2.47.20260726.

The build sequence was:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 clean
bmake MACHINE=rp2040 distribution
```

## Interface boundary

`getpgrp()` now has the public C17 prototype `pid_t getpgrp(void)`. Syscall 81
takes zero arguments and returns only the calling process group. The kernel
test poisons the former argument slot and proves that the syscall leaves the
slot unchanged. The metadata mutation restores the former one-argument entry
and must fail. All maintained callers use the zero-argument form; PicoC carries
the same signature in its compatibility declarations.

`sysconf()` reports ten bounded selectors. The established argument, clock,
group, descriptor and job-control values remain unchanged. The RP2040 profile
adds one configured processor, one online processor, 32 signals, a 1,024-byte
software page and `_POSIX_DEVICE_CONTROL=-1`. The unsupported Device Control
option returns `-1` while preserving `errno`; an unknown selector returns `-1`
and sets `EINVAL`. A compact branch tree avoids a Thumb-1 jump-table helper.
C17 static assertions bind the encoded signal and page values to `NSIG` and
`DEV_BSIZE`.

`confstr()` remains limited to `_CS_PATH`. The host witness covers every output
length, null output, truncation, termination and unknown selector. DPUCP-2024
therefore records `sysconf()` and `confstr()` as partial and open. The profile
closes only the exercised `getpgrp()` row; the profile makes no POSIX or UNIX
conformance claim.

`_SC_CHILD_MAX` remains absent. `fork1()` counts effective `p_uid` values and
rejects only after that count exceeds `MAXUPRC`. With `MAXUPRC=20`, the kernel
can admit a twenty-first process, and the accounting does not implement the
required real-UID boundary. A separate kernel repair must define and enforce a
truthful limit before the selector can be added.

## Reduced archive mechanism

The first candidate increased the 151-member board libc from 54,086 to 54,122
bytes. Member comparison isolated the 36-byte growth to `sysconf.o`: the
constant table reduced target text but introduced a second a.out section and a
local table symbol. A branch-only form still retained every compiler-generated
`.L*` label in the a.out symbol table.

The tree assembler's `-X` option removes `L*` and `.*` local symbols after their
relocations have been bound. `mkboardlibc.py` now applies that existing format
mechanism uniformly to every member rather than special-casing `sysconf.o`.
The archive gate assembles the same fixture with and without `-X`, proves that
the default object retains its local label, proves that `-X` retains the
exported symbol while removing the local label, rejects any `.L*` symbol in the
production archive, links a real formatted-output consumer and confirms that
removing `vfprintf.o` still breaks that link.

The clean result retains 151 members and shrinks the shipped archive by 11,076
bytes. The member set and external link surface remain unchanged.

## Constrained surfaces

| surface | base | candidate | delta |
| --- | ---: | ---: | ---: |
| PICO kernel text | 104646 | 104598 | -48 |
| PICO kernel data | 248 | 248 | 0 |
| PICO kernel BSS | 39848 | 39848 | 0 |
| board libc archive bytes | 54086 | 43010 | -11076 |
| board libc members | 151 | 151 | 0 |
| largest process, `warp`, loaded bytes | 101300 | 101300 | 0 |
| packed root data blocks | 692 | 682 | -10 |
| packed root inode-list blocks | 9 | 9 | 0 |
| packed root reported free blocks | 287 | 297 | +10 |

The root identities are `692 + 287 + 9 = 988` for the base and
`682 + 297 + 9 = 988` for the candidate. `warp` remains 46,156 bytes below the
147,456-byte text-plus-data-plus-BSS process-window ceiling before stack use.
The kernel's 104,846 text-plus-data bytes remain within the 128 KiB kernel
flash region; BSS occupies its separately accounted SRAM surface.

The full build archive grows from 410,038 to 410,054 bytes because the isolated
ELF `sysconf.o` grows from 64 to 80 text bytes. The full archive is not shipped
in the RP2040 root. The reduced a.out member contains 80 text bytes and no data
or BSS; local-symbol removal makes its complete object 148 bytes, 20 bytes
smaller than the 168-byte base member. `confstr.o` remains 94 text bytes.
`kern_prot.o` falls from 600 to 556 text bytes.

| program | base loaded | candidate loaded | base raw | candidate raw | base packed | candidate packed | block delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `warp` | 101300 | 101300 | 80736 | 80736 | 66337 | 66337 | 0 |
| `compress` | 42676 | 42676 | 12380 | 12380 | 10671 | 10671 | 0 |
| `more` | 21472 | 21472 | 20104 | 20104 | 17460 | 17462 | 0 |
| `uucico` | 70348 | 70348 | 52404 | 52404 | 44786 | 44787 | 0 |

The public-header rebuild preserves every affected loaded and raw executable
size. The packed byte streams for `more` and `uucico` differ by two and one
bytes respectively without changing either file's allocated block count. The
complete root saves ten data blocks because the installed board archive is
smaller. `compress` keeps its `getpgrp()` call behind the existing `notdef`
guard. The RP2040 build intentionally excludes PicoC and csh, so their updated
source interfaces add no image bytes.

## Evidence and residuals

`check-libc-runtime-limits` executes the known-good source at native and ILP32
widths, executes a permissive-selector negative fixture and rejects five
focused mutations. `check-libc-runtime-limits-cross` compiles the production
source for Cortex-M0+ and permits only the `errno` import. `check-kernel`
executes 1,347 assertions, including the zero-argument process-group behavior.
`check-kernel-metadata`, `check-board-libc-output`,
`check-c17-source-denominator` and `check-dpucp-profile` provide their separate
structural and compilation evidence.

The clean distribution build and the focused host, ILP32 and cross gates ran on
the build host. Qemu-user, Renode and physical-board execution remain separate
evidence classes and were not run for this change. No serial device was opened
and no flash command was executed.
