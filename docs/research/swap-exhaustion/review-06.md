# Exchange-headroom admission review

## Options

### Owned departure extent

The single resident user image owns one aligned flash extent large enough for
`swap_image_span(resident)`. The resource map excludes that extent, so fork,
exec spooling, `TFALLOC`, SwapRAM evacuation, and other process images cannot
consume it. `swapout()` consumes the extent without allocating. A flash
swap-in transfers the incoming image's released extent to the incoming process
instead of returning it to `swapmap`. A SwapRAM swap-in acquires an extent for
the incoming image before the scheduler evicts the resident, or the scheduler
restores the victim and waits. Growth and exec acquire a sufficiently large
replacement before publishing larger process geometry. Exit returns the
extent after the process can no longer need a departure route.

The ownership record needs an owner, base, and span. A global record is enough
because RP2040 has one untranslated user window; putting the record in every
`struct proc` would spend at least 300 bytes for 25 process slots and would put
live ownership in the `p_alive` union that exit overwrites. Allocation and
ownership transfer run in one `splhigh()` region. Flash I/O runs after the
extent has left the free map and may sleep without weakening ownership.

Invariant: every resident non-system image has one owned aligned flash extent
whose span is at least its current swap image span. A transition either retains
that extent, atomically replaces it with a sufficient extent, transfers a
flash image extent of sufficient size, or refuses before changing resident
geometry.

### Computed largest-run check

Every flash consumer first allocates its requested aligned run, computes the
largest aligned run remaining in `swapmap`, and rolls the allocation back when
that run is smaller than the resident image span. Allocation, verification,
cursor publication, and rollback must share one `splhigh()` region.
`sched()` must perform the same allocation-backed operation before admitting a
SwapRAM image, because a free-space precheck supplies no ownership.

The mechanism needs a shared allocator entry point used by
`swap_image_reserve()`, `exec_spool_reserve_flash()`, `swcioctl()` and
`swapram_evacuate()`. It also needs a stable resident-span record while
`sched()` has cleared `SLOAD`, while fork temporarily sets `u.u_procp` to the
child, and while swap I/O sleeps. Growth and exec update that record inside the
same interrupt exclusion boundary.

Invariant: after every flash grant, the free map retains an aligned run at
least as large as the recorded route requirement of the process that owns the
window.

### Route sized for the largest live image

Each flash grant preserves a free aligned run large enough for the largest
`swap_image_span()` among all non-zombie processes. The scan and grant run in
one `splhigh()` region. Fork publication, exec, growth, vfork transfer, exit,
and clean-text ownership changes must update the bound before publishing a
larger span and after retiring the last owner of a maximum span.

Invariant: the free map always contains one aligned run large enough for every
live image, so whichever image next owns the window has a flash route.

### Maximum-window reserve

A simpler largest-live variant permanently removes a 196-block aligned route:
192 KiB of user data and stack plus the 3 KiB u area, rounded to four 1 KiB
blocks. Every consumer allocates from the remaining 184 of the RP2040's 380
allocatable blocks.

Invariant: every image admitted by the fixed process-window limit fits the
permanent route without a process scan or a growth update.

## Falsification

The production allocator exposes 380 blocks: `FLASH_SWAP_BYTES` is 384 KiB
(`sys/arch/rp2040/dev/flash.h:99`), and boot places blocks 4 through 383 in the
map (`sys/kern/init_main.c:147-155`).
`malloc3_contiguous_next()` grants one aligned next-fit run and leaves the
cursor unchanged on refusal (`sys/kern/subr_rmap.c:282-374`). Total free space
does not establish a route: two aligned 100-block runs contain 200 blocks but
cannot serve a 152-block image.

Flash swap-in reads the image and then frees exactly its rounded span
(`sys/kern/vm_swap.c:195-203,140-142`). SwapRAM swap-in frees pool capacity and
frees no flash (`sys/kern/vm_swap.c:181-192`). The scheduler reserves and
writes the resident before it restores the incoming process
(`sys/kern/vm_sched.c:55-88`). A pool image can therefore become resident after
the old implementation consumes the last flash route.

Swap writes sleep for buffer completion, while exec spool, `TFALLOC`, and
evacuation allocate directly from `swapmap`. A check separated from allocation
is a time-of-check/time-of-use error; an intervening consumer can take the
observed run. Ownership or an allocate-and-verify transaction survives that
interleaving.

The computed-check option fails without a separate stable owner record.
`sched()` clears `SLOAD` before reservation (`sys/kern/vm_sched.c:141-151`),
and fork temporarily names the child in `u.u_procp` while the parent still owns
the window (`sys/kern/kern_fork.c:165-188`). Neither an `SLOAD` scan nor
`u.u_procp` identifies the route requirement in every sleeping interval. Adding
the stable record makes the mechanism converge on the owned-extent design but
still leaves a free run available for unrelated allocation mistakes.

The largest-live option rejects more work than the exchange invariant needs.
A large swapped image need not dictate the route while a smaller image owns the
window. The option also requires synchronized updates at heap growth
(`sys/kern/kern_mman.c:24-45`), syscall stack growth
(`sys/arch/rp2040/rp2040/syscall.c:188-201`), signal-frame growth
(`sys/arch/rp2040/rp2040/sig_machdep.c:77-91`), exec
(`sys/kern/exec_subr.c:429-438`), vfork transfer
(`sys/kern/kern_fork.c:190-209`), process-1 initialization
(`sys/kern/init_main.c:194-200`), and exit. Missing any update refutes the
claimed bound.

The maximum-window reserve spends 196 blocks even when init or the shell needs
only a small fraction. It leaves 184 blocks for every process image, exec spool
and temporary extent combined, so it refuses every request above 184 blocks
and any combination above that limit. Two maximum-size backing requirements
need 392 blocks and cannot coexist in 380 blocks under any mechanism; refusing
the second requirement is necessary rather than a policy artifact.

The owned-extent option survives fragmentation, sleeping I/O, pool return,
`TFALLOC`, and SwapRAM-off because the protected run is absent from the free
map. Its remaining falsifiers are an unguarded geometry increase, premature
release during exit or exec text transition, a transfer whose extent is smaller
than the incoming image's current span, or a code path that writes an image
without consuming the recorded owner extent. The calibrated gate must target
each of those seams.

RAM cost is one pointer and two `size_t` values globally, 12 bytes on RP2040.
Text cost and exact workload-refusal counts remain measurements for the linked
implementation and host fixture. Flash cost is the resident image span, not a
fixed maximum; the extent already becomes the resident's stored image on the
next eviction.

Process 1 requires an explicit resize after its bootstrap geometry replaces
the u-area-only fork image. PICO_UART defines `SWAP_IMAGE_ALIGN` without
`SWAPRAM`, so the same flash ownership invariant applies with the pool branches
compiled out. Existing `TFALLOC` shortage returns success with a zero output
size (`sys/dev/swap.c:296-302`); the board prediction must preserve that ABI
rather than claim `ENOMEM` for `talloc`.

## Synthesis

The implementation will use the owned departure extent. It takes the durable
ownership of the first option, the single atomic allocation region of the
computed-check option, and the larger-incoming-image concern exposed by the
largest-live option. The resident owns the route; a pool image acquires its own
route before admission, so the implementation does not reserve for an
unrelated maximum live image.

The computed largest-run rule is rejected as a primary invariant because a
free run remains available to any missed consumer and because identifying the
resident already needs persistent ownership state. The largest-live and fixed
maximum rules are rejected because they reserve capacity for images that do
not own the window. The choice reopens if source evidence establishes more
than one simultaneously resident user window, or if a production path can
require departure backing without being able to consume or transfer the owned
extent.

## Gate results

Pending implementation and calibration.

## Board prediction

Pending the final syscall and fixture behavior. The prediction will distinguish
fork's `ENOMEM` from `TFALLOC`'s successful zero-size shortage ABI, require a
normal scheduler retry rather than a panic, and exercise a SwapRAM-returning
image whose route cannot be acquired.

## Handoff

Implementation and calibrated-gate work remain active.
