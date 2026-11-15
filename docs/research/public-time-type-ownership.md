# Public time and signal type ownership

## Observation and governing contracts

The implementation base is owned main `a706c0b851daaa25b019639e8217dda5ac17095d`.
Standalone `<sys/select.h>` leaves `timeval` and `timespec` incomplete, lacks
`suseconds_t`, and lacks `sigset_t` under the explicit 32-bit time view.
Standalone `<time.h>` also leaves `timespec` incomplete. Real ARM strict-C17
consumers that instantiate these types reject before production changes.
Source lookup uses `rg -n 'timespec|timeval|sigset_t|suseconds_t'` over the
public headers and `sys/sys/_time_t.h`.

[POSIX Issue 8 sys/select.h](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_select.h.html)
requires complete timeval and timespec structures and the time_t,
suseconds_t, and sigset_t types. Its
[time.h definition](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/time.h.html)
requires timespec; ISO C17 7.27.1 also defines that structure in `<time.h>`.
The `timespec_get` function, TIME_UTC constant, clock facilities, and complete
select/pselect semantics retain their separate implementation packages.
Adding an unsupported function declaration closes no capability.

The type invariants are one canonical definition per scalar or structure,
complete types from their required public entry points, and unchanged target
field widths, signedness, offsets, alignment, and time-view selection. Header
inclusion order never chooses a different definition. Public `<time.h>` gains
timespec without importing syscall declarations through `<sys/time.h>`.

## Minimal ownership graph

Private `_timespec.h` and `_timeval.h` depend on `_time_t.h`. Private
`_suseconds_t.h` supplies signed long microseconds, and `_sigset_t.h` owns
the existing unsigned long signal mask under its existing guard.
`<time.h>` includes only the timespec owner; `<sys/time.h>` and
`<sys/select.h>` include both structure owners. Signal and select headers
include the signal scalar owner, while `<sys/types.h>` includes the
microsecond scalar owner. Private type definitions avoid a dependency on a
partially read public header.

The existing guarded types/select cycle remains compatible. Fundamental
types precede the legacy select include in sys/types.h, and each private owner
needs only already-independent scalar definitions. Signal inclusion no longer
needs to duplicate a typedef to survive that cycle. Existing `__TIME64_NAME`
bindings and pselect declaration visibility remain unchanged.

Shared-ABI discovery needs both an argument layout and its linker identity.
A pointer signature can name an incomplete structure, and a valid layout can
still call the wrong time-width entry point. The matrix therefore checks
layout/field invariants separately from the undefined symbols in compiled
objects. A changed structure rejects through the layout oracle; a changed
assembler alias rejects through the object oracle. Canonical owners make
those paired observations independent of public inclusion order. This unit
supplies that prerequisite while leaving shared export selection open.

| Target view | timeval seconds | timeval microseconds | timespec seconds | timespec nanoseconds | Structure size/alignment |
| --- | --- | --- | --- | --- | --- |
| User wide | signed 64-bit time_t | signed long | signed 64-bit time_t | signed long | 16/8 bytes |
| User narrow | signed long | signed long | unsigned long time_t | signed long | 8/4 bytes |
| Kernel | signed long | signed long | unsigned long time_t | signed long | 8/4 bytes |
| Smaller C | signed long | signed long | unsigned long time_t | signed long | 8/4 bytes |

Narrow timeval seconds preserve the historical signed interval ABI. They
remain a documented legacy exception to the POSIX time_t member contract.
The maintained cross default selects the wide view. Smaller C has no 64-bit
integer and retains its 32-bit time policy even when `_TIME_BITS=64` appears
in preprocessor input. Its matrix labels name requested selectors, not a
claim of native wide-time support. Kernel records `__timeval32` and
`__timespec32` retain their independent layouts.

## Donor mechanisms and falsifiers

DragonFlyBSD development revision
`e0f962ddd750a94049c962632074b1b240ceb40e` includes private `_timespec.h`,
`_timeval.h`, and `_sigset.h` from its select header. Its private aggregate
headers obtain scalar types without importing public time syscalls. FreeBSD
revision `6871285f194d3f83fd270b4b5d5783503c2b86d6` likewise owns timespec
in a private header. Immutable `git show REVISION:PATH` reads supply these
observations. These related BSDs share ancestry; their agreement supplies
correlated design evidence rather than independent portability proof.

The adaptation preserves DiscoBSD's existing definitions instead of copying
a donor's time widths or multiword signal set. Importing all of sys/time.h
through time.h expands namespace and cycle obligations. Repeating public
definitions makes order and guards part of type identity. Canonical private
owners avoid both mechanisms. This composition has established BSD precedent
and supplies no claim of a new type-system algorithm.

The deciding falsifiers are a valid standalone consumer that rejects, a
different layout or linker alias, a repeated/order-dependent definition,
native input that silently widens, or a caller whose target ABI changes.
The red-to-green prediction precedes production edits: standalone time and
select structure consumers become accepted, while removed prerequisites,
changed widths/signedness, and a removed structure guard reject. The original
failure evidence stays retained rather than replaced by passing output.

## Implementation and verification scope

`tools/c17/public_time_types.py` owns standalone and repeated forms, all 24
orders of four public headers, four feature selectors, two cross time views,
exact target layout/field assertions, link-name witnesses, kernel invariants,
and cross-preprocessed Smaller C input. Invalid source and malformed diagnostic
controls calibrate the compiler boundary. Smaller C exercises its actual
32-bit layout under both requested selectors.

Caller closure includes existing time64 wrapper and binding gates, signal
headers, public-header matrices, hosted contract shims, full clean builds and
matched distributions. The selected root manifest currently contains no
installed header files, while `make includes` installs private sys headers
with its existing wildcard closure. Resource captures still decide actual
kernel, program, native archive, and root consequences. Positive growth needs
the profile's exact decision. Renode and physical execution remain separate.

The independent descriptor-set macro repair stays preserved in its own draft
PR. This unit changes type ownership and leaves its clearing macro intact;
integration reconciles both changes without discarding either invariant.
