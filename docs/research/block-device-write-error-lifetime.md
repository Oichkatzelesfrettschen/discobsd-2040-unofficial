# Cooked-device write errors across inode aliases and last close

The host disk fixture reproduces lost error reporting on an unmounted cooked
block device. `rwip()` sends a complete buffer through `bawrite()` and returns
before a caller waits for its result. `biodone()` retains write errors only in
an active mount. `brelse()` then detaches a failed buffer from its device.
The next `fsync()` can succeed even though the requested bytes never reached
the RAM disk. The RAM disk measures the host buffer-cache contract; physical
persistence and power interruption remain separate storage contracts.

## State and ownership

The error identity is the exact `dev_t` used by `buf.b_dev` and a block
inode's `i_rdev`. Partition numbers and overlapping physical extents remain
distinct buffer-cache identities. Character-device I/O follows its driver's
direct return path and remains outside the cooked-buffer error contract.
Mapped raw disk opens and closes share the unit-operation guard.

Three lifetimes need separate treatment:

1. A buffer owns one transfer and its immediate result. Releasing or reusing
   that buffer ends the result's availability in the buffer object.
2. A block-inode alias owns a reference to an open cooked-device lifetime.
   Distinct nodes can name the same device, and duplicated descriptors can
   share one file object. An inode cache reference also survives some lookups.
3. A mount retains its existing first-write-error latch until its documented
   unmount boundary. Closing an alias of a mounted device ends neither the
   mount nor the driver's device lifetime.

The retained error means that at least one write in the holding lifetime
failed. A successful retry writes bytes but cannot prove that every earlier
lost byte was restored. Consequently `fsync()` observes the retained error
without consuming it. A later alias must inherit the same error while a
holder remains. An unrelated device must retain its independent result.

## Representation comparison

| Representation | State mechanism | Lifetime and resource obligation |
| --- | --- | --- |
| Separate device table | One device/error record and reference accounting per occupied slot | Introduces a second admission capacity and must reconcile opens in progress, duplicated descriptors, kernel opens, mounts and interrupted close. Even a saturated table must retain existing errors. |
| Independent inode fields | One error per active inode | A dedicated `int` costs one field per inode. Every alias, later open, mount handoff and inode reuse must reconcile the copies. Per-inode errors alone cannot define a device boundary. |
| Shared semantics with inode-held replicas | Existing mount error plus one transient error in each active block-inode alias | Uses bounded inode/mount scans on error and open. All copies retain the same first error; last-close processing drains outstanding writes before ending the holding lifetime. |

The selected representation uses the third mechanism. The existing
`i_un1.I_wsel` pointer belongs to pipe selection. A block-special inode cannot
be a pipe, so the `I_block` union member can occupy that transient storage.
The implementation divides those four bytes into a 16-bit first
error and a 16-bit open state. The state holds a bounded open count and one
device-operation lock bit. The fields stay outside `icommon1`, `icommon2`,
`dinode` and the
address array, which are copied to serialized inode representations.
Matched ARM layout probes measure the same inode sizes before and after:
84/92 bytes for compact single/multiple-filesystem configurations, and
100/108 bytes for wide hashed single/multiple-filesystem configurations.
The transient union remains four bytes and the disk inode remains 64 bytes;
the common-field, address and free-list/read-ahead offsets also match.

The device error is a shared invariant over these holders, rather than an
extra independently authoritative table. `biodone()` fills an empty first-error
field in the matching mount and every held block-inode alias under `splbio()`.
An occupied field retains its first result. `openi()` inherits a retained error while
holding the device-operation lock and increments the alias's count only after
a successful driver open. A newly activated/reused inode starts with cleared
transient storage. A fresh inode load clears its old mode before blocking
on disk I/O, so an incompletely loaded slot cannot impersonate an old alias.

## Completion and last close

The existing `bflush()` intentionally visits available delayed-write buffers;
general background sync keeps that bounded non-waiting behavior. Cooked-device
`fsync()` and last close need a stronger operation: flush delayed buffers,
wait for matching busy buffers to complete, repeat the scan, and report the
first retained or immediate write error. Clearing the latch must never stand
in for writing or waiting.

`vn_closefile()` currently releases the inode before `closei()`. A device close
that waits for I/O therefore needs the order reversed: remove the closing
file from the alias search, retain the inode through the drain and driver
close, then release the inode. Other open aliases prevent driver close and
retain the shared error. An active mount also retains its existing latch.
The final unmounted close reports the error, invalidates drained buffers and
ends the error lifetime even when close reports failure; the descriptor is
consumed under the existing close contract. Reopening starts a new holding
lifetime and supplies neither repair nor durability proof for old contents.

An open count belongs to each successful `openi()` acquisition, rather than
to inode cache references or duplicated descriptors. A duplicated descriptor
retains its file object's existing acquisition. A failed driver open retains
the old count. `bdevlock()` serializes a driver unit across cooked and mapped raw inode aliases;
opens and closes retain that lock across their driver operations and drains.
An opener waits for a final close to finish before starting a new holding
lifetime. A closer waits for an in-progress open before deciding whether its
reference is last. An inode reference protects each lock owner and waiter.
The count's explicit bound prevents carry into the lock bit before driver
admission. Runtime mount acquisition and unmount retain their device inode
through the same lock. A newly published mount inherits the held error;
`iflush()` counts successful cooked opens independently of lookup references.
The initial root mount runs before concurrent processes and has no device
inode yet. The mount's existing removed-media policy remains a separate
explicit release boundary.

Unit-guard acquisition, error publication, the open inheritance lookup/store
pair, and mount error/identity publication use `splbio()`. Open/close counts
change under the device-operation lock. Buffer drains use noninterruptible
`PRIBIO + 1` sleeps (`21 <= PZERO`, which is 25). A driver checkpoint can
still return nonlocally: open and close save and restore the caller's
`u.u_qsave`, release the lock, and preserve the existing error precedence.
Tests exercise real nonlocal returns through call-site builtin checkpoints
in live ILP32 frames; a nested control checks copied-checkpoint restoration.
The representation adds neither a dynamic-allocation path nor an additional
device-slot capacity. Target code size, compiler
frames, inode layout and runtime peaks remain separate measurement surfaces.

## Deciding tests

- The real `rwip`, `fsync`, buffer cache and close path must reject the
  unmounted asynchronous-error reproduction and retain its error across
  repeated observation and successful later writes.
- Simultaneous aliases and an alias opened after failure must agree. Closing
  the original alias must preserve a surviving alias's error; the final
  unmounted close must report it and a fresh open must start clean.
- A second distinct error must preserve the first result. Other device
  numbers, inode reuse and pipe-selector storage must remain independent.
- Deferred completion must force `fsync` and last close to wait, keep the
  closing inode referenced, publish the completion error and release the
  driver only afterward.
- Mounted aliases must preserve the mount latch and the existing unmount/
  removed-media policy. Raw character aliases retain their existing contract.
- Mutations that omit propagation, inheritance, draining, first-error
  retention, lifetime reset or inode-reference ordering must fail a named
  assertion. A dormant injection or uncontrolled hang establishes nothing.

The host scheduler retains two production call stacks, separate user-area
snapshots and interrupt-priority state. Its deciding close-versus-mount
schedule blocks mount acquisition while final close drains a deferred write,
then observes driver close before the next driver open and mount publication.
The scheduler bounds switches at 64. The host context stacks are test
infrastructure and contribute neither firmware code nor target RAM.

## Driver-unit guard and mounted-device admission

`sdopen()` initializes shared card and partition state when the unit's open
mask is empty. `flopen()` similarly initializes a unit's partition table.
The operation guard therefore covers the driver unit even before a partition
extent is known. `blkdev_same_unit()` identifies the flash and SD units by
major number and the minor number above the low three partition bits.
Other RP2040 block devices retain exact-device guards. The STM32 SD driver
selects its unit using minor bit 3, so its guard uses that same selector;
other STM32 devices retain exact-device guards.
`chrtoblk()` gives mapped raw nodes the same unit identity. The transient
inode union holds their lock bit; cooked open counts and errors retain their
exact block-device identity. Pipe selector storage has its separate inode type.

`openi()` checks securelevel disk policy and `ufs_mountedon()` after acquiring the guard and keeps the
guard through the driver call. Mount publication and unmount driver close
hold the same guard. `ufs_mountedon()` uses `blkoverlap()` for extents, so a
write opener waiting behind a newly published overlapping mount receives
`EBUSY`; an adjacent partition waits for unit setup and then opens. This
separates driver initialization from the filesystem's extent policy.
A guard keyed only by extent cannot cover setup before the table exists.
A guard keyed by exact device leaves raw, whole-unit and partition aliases
able to pass the mount check during publication.

The deciding host schedule pauses the production mount path inside driver
open before mount publication. A second production `openi()` attempts a
cooked partition, cooked whole unit, raw partition, raw whole unit or adjacent
cooked partition. The first four must wait and return `EBUSY` at securelevel
1, with exactly the mount's driver acquisition; the adjacent partition must
wait and then acquire the driver. The expanded schedule reports 12 failed
checks with the exact-device guard and passes with the unit guard. These
observations execute the RAM disk adapter, cooperative scheduler and ILP32
kernel sources. They cover the specified switch points, rather than every
production scheduling interleaving.

## Host verification and resource capture

The Linux ILP32 fixture retained by commit `70ea03e2` runs
`bmake -C tests/kernel MACHINE=rp2040 -j4 check-ufs-disk-ilp32 check-ufs-disk-mutants`
with `PYTHON` selected and exported. The fixture passes 1,944 checks and rejects
45 mutants. The policy-raise reproduction first fails four checks, and the
post-guard securelevel checks make both cooked and mapped raw writers return
`EPERM`. The new lookup controls cover denied credentials, a missing name,
a directory in place of the device node and an unsupported driver major;
each leaves device acquisition closed, followed by a successful mount/unmount.
A retained lookup reference across final close observes an ended error
lifetime before the same inode opens again. Raw driver interruption returns
through the live checkpoint and releases its unit guard.

The source review independently identifies the policy-raise window. The
parent reproduces the finding, repairs both admission paths and reruns the
focused suite and all mutations. The review supplies source evidence; the
executed fixture supplies the named schedule evidence. The fixture replaces
name lookup, disk strategy, scheduling and credential services, while linking
the kernel buffer, inode, mount, descriptor and I/O paths.

Clang's core, Unix and dead-code analyzer checks pass for the six changed
kernel translation units at the fixture's ILP32 configuration. The initial
analysis cannot infer the global-error postcondition of an opaque `suser()`
call and flags the inode out parameter. `kern_prot2.c:suser()` sets `EPERM`
on failure. `getmdev()` returns the referenced inode directly, so each caller
checks the pointer before acquiring its guard. The broader insecure-API
check flags preexisting `bzero` calls; the selected analyzer classes evaluate
memory, path and control-flow properties separately from that API preference.

`block-device-write-error-resources.json` records matched Cortex-M0+ object
compilations, exact source hashes, compiler flags, layout arrays and static
frames against the fixed audit baseline. The seven measured objects add
1,272 text bytes and retain their object data/BSS sizes under the PICO flags.
The mount array, defined by `machdep.c`, grows four bytes per mount. The four
inode configurations retain their sizes and serialized offsets. The compiler
reports `openi` frames of 24/40 bytes before/after, `closei` frames of 24/32,
and `syncip` frames of 24/8; `device_driver_call` has a 72-byte frame.
These are object and individual-frame measurements. Linked image size,
configuration-dependent elimination, runtime stack peaks and physical
persistence retain separate evidence requirements.

The preceding logged build passes with exit zero, including the PICO and
PICO_UART links; the pointer-return helper requires final revision-bound
build and aggregate results before publication. The production atomic-recovery stop
point in `storage-recovery-comparison.md` remains independent.

The shared-source review extends the unit guard to STM32 SD devices.
`sys/arch/stm32/dev/sd.c:sdunit()` masks minor bit 3; distinct partition minors
and minors differing only in ignored upper bits select the same card state.
The exact-device implementation fails two assertions in the extracted-function
probe. The corrected selector is tested beside the RP2040 unit function,
including separate units, majors and unknown device classes. The probe
executes the extracted identity function at ILP32; it supplies no STM32
driver execution or board result. The extended fixture passes 1,950 checks
and rejects 46 mutants, including the STM32 exact-device reversal.
