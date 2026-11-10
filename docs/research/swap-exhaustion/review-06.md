# Exchange-headroom admission review

## Options

### Owned departure extent

The single resident user image owns one aligned flash extent large enough for
`swap_image_span(resident)`. The resource map excludes that extent, so fork,
exec spooling, `TFALLOC`, SwapRAM evacuation, and other process images cannot
consume it. `swapout()` consumes the extent without allocating. A flash
swap-in transfers the incoming image's released extent to the incoming process
instead of returning it to `swapmap`. A process admitted to SwapRAM acquires
an off-map departure extent in its pool record before fork publication. Pool
swap-in transfers that extent to resident ownership before eviction, and
evacuation consumes it as the flash image backing. Growth and exec acquire a
sufficiently large replacement before publishing larger process geometry.
Exit returns the extent after the process can no longer need a departure
route.

The resident ownership record needs an owner, base, and span. RP2040 has one
untranslated user window, so one global record covers the resident. Each
SwapRAM slot adds a base and span because a pool-backed swapped image must own
its future resident route before its parent can wait for it. Keeping those
fields in the pool table avoids the `p_alive` union that exit overwrites and
preserves zero `p_daddr`, `p_saddr`, and `p_addr` while an image is pool-backed.
Allocation and ownership transfer run without a sleeping gap. Flash I/O runs
after the extent has left the free map and may sleep without weakening
ownership.

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

For a bounded refusal count, take the 95 aligned single-consumer requests of
4 through 380 blocks against one initially contiguous 380-block map. Relative
to the old allocator, owned extents of 156, 160 and 164 blocks refuse 39, 40
and 41 requests respectively; the computed-check rule has the same counts on
that unfragmented workload family. A largest-live rule has the count determined
by its largest live span, so the same three spans give the same three counts;
the 196-block maximum-window rule refuses 49. Fragmented and multi-consumer
workloads depend on run placement and ordering, so these finite counts do not
claim a universal refusal rate.

The owned-extent option survives fragmentation, sleeping I/O, pool return,
`TFALLOC`, and SwapRAM-off because the protected run is absent from the free
map. Its remaining falsifiers are an unguarded geometry increase, premature
release during exit or exec text transition, a transfer whose extent is smaller
than the incoming image's current span, or a code path that writes an image
without consuming the recorded owner extent. The calibrated gate must target
each of those seams.

Route growth first consumes the exact `required - old_span` block suffix when
one free-map entry begins at `old_base + old_span`. The operation runs in the
same `splhigh()` region as replacement, changes no cursor, and retains the
route base. For example, a four-block route growing to eight blocks needs only
the adjacent four-block suffix; the earlier replacement-only implementation
required a separate eight-block run while still holding the old four blocks.
When the suffix is missing or short, replacement still allocates the complete
required span before returning the old route, so failure preserves ownership.

RAM cost is one pointer and two `size_t` values globally plus two four-byte
route fields in each of 25 SwapRAM process records, 212 bytes on RP2040.
With the exact PICO `-Os` compiler command retained in the logged build,
`vm_swap.o` has 1,660 text, 266 read-only and 12 BSS bytes versus 956, 138 and
zero at `HEAD`; `swapram.o` has 2,416 text, 790 read-only and 1,048 BSS bytes
versus 2,212, 704 and 848. The mechanism therefore adds 908 text bytes, 214
read-only flash bytes and 212 SRAM bytes across those objects. Capacity cost is
one route per resident or pool-backed image, not a fixed maximum; each extent
becomes stored-image backing on eviction or evacuation.

Process 1 requires an explicit resize after its bootstrap geometry replaces
the u-area-only fork image. PICO_UART defines `SWAP_IMAGE_ALIGN` without
`SWAPRAM`, so the same flash ownership invariant applies with the pool branches
compiled out. Existing `TFALLOC` shortage returns success with a zero output
size (`sys/dev/swap.c:296-302`); the board prediction must preserve that ABI
rather than claim `ENOMEM` for `talloc`.

## Synthesis

The implementation uses the owned departure extent. It takes the durable
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

`bmake MACHINE=rp2040 check-swap-exchange-headroom` links the production fork,
scheduler, image-swap, SwapRAM pool and resource-map definitions into the host
fixture. The baseline passed 53,636 assertions. Five behavioral mutants exited
1 at their named oracle: `count-free-not-largest-run`,
`fork-consumer-skip-rule`, `release-headroom-early`, `pool-swap-in` and
`scheduler-pool-bypass`.
The same gate compiles the production image-swap source with aligned swap and
SwapRAM disabled, matching the PICO_UART ownership surface. This compile oracle
rejected the unguarded pool-route call found during adversarial review.
The generated PICO_UART ARMv6-M command also accepts the current source under
`-fsyntax-only` with its complete warning-as-error flags.

The same gate checks ten production consumer anchors and rejects one removal
mutant for each: fork, exec spool, `TFALLOC`, SwapRAM evacuation, heap growth,
syscall stack growth, signal-frame growth, exec geometry, process-1 geometry
and exit release. These are source-bound inclusion checks; the linked fixture
is the behavioral evidence for allocation, ownership transfer, resize,
fragmentation, scheduler refusal and release.

The preexisting fork/scheduler fixture changed because flash swap-in now
transfers its extent to the resident instead of returning 156 blocks to the
free map. The previous free-map assertion contradicted the selected invariant,
so the fixture now requires zero newly free blocks at transfer and 156 blocks
only after terminal exchange release. The scheduler success fixture likewise
requires every outgoing resident to use its owned flash route; SwapRAM status
belongs to the incoming image. A pool-backed child now owns its flash route at
admission; the scheduler transfers that route rather than allocating after the
parent can wait. Before the pool-bypass correction, every scheduler eviction
used the resident route as flash backing and performed three media writes even
when the image fit the compressed pool. The corrected transaction admits the
resident with its existing route, records that route in the pool slot, and
performs zero media writes; pool refusal retains the flash fallback. The
`scheduler-pool-bypass` mutant forces that fallback and fails the tier oracle.
`check-fork-swap-refusal` passed 53,636 checks in each compact,
wide and diagnostic shape, 53,642 in the full-map shape and
435 in the legacy unaligned shape, then rejected all 28 existing mutants.

`bmake MACHINE=rp2040 check-kernel` and
`bmake MACHINE=rp2040 check-swapram-evac` exited 0. Native sanitizer probes
recorded sandbox `Bad system call` skips; they establish no sanitizer
execution. The evacuation gate completed its allocator, codec, round-trip,
shortage, admission and service controls.

## Board prediction

The measured origin/main `eb5b3081` workload used a `forktest` program with a
172 KiB bss and an approximately 184-block image. It printed no FORKTEST line
within 120 seconds while other processes held swap; a console interrupt
returned the shell, and the kernel printed no panic. With owned departure
extents, the same geometry when only the resident's 184-block extent remains
will instead observe `fork()` return `-1` promptly with `errno == ENOMEM`;
the parent remains runnable and the kernel prints no panic. The linked host
fixture constructs that exact 184-block geometry, leaves only the resident's
owned extent, enables SwapRAM admission, and requires the fork call to return
with `ENOMEM`, an empty pool, zero process publications and zero media writes.
A second 184-block case supplies two fragmented 92-block runs, proving that a
total-free calculation cannot admit the child.
A `TFALLOC` request that would consume the same capacity returns success with
an output size of zero, preserving the existing temporary-swap ABI. Every
admitted SwapRAM image already owns an aligned departure extent. Pool swap-in
therefore transfers the extent without consulting the remaining free map; a
missing route is an invariant failure and the scheduler refusal path keeps the
resident loaded. The deciding capture is the syscall result and errno, the
absence of a panic, the empty pool after refusal, and the free-run map before
and after admission.

## Handoff

Resident and SwapRAM-slot owned extents are implemented.
The exact 184-block fork case refuses with SwapRAM enabled and fragmented.
The 53,636-assertion gate rejects five behavioral and ten removal mutants.
The host and exact ARM PICO_UART SwapRAM-disabled compile oracles pass.
`check-kernel`, `check-swap-exchange-headroom` and `check-swapram-evac` pass.
Ruff check, Ruff format check and `git diff --check` pass.
`logged-build` is blocked by shared invocation
`rp2040-check-local-20261110T021709Z-117470-122cb521`.
Its recorded process group 117476 is absent, while sequence and build receipts
remain nonterminal `running`; the recovery tool requires a terminal receipt.
The required `logged-build` attempt fails closed with status 125 before mutation.
Current-product `check-kernel-headroom` remains unrun.
The private PICO object comparison records 908 text, 214 rodata and 212 BSS.
Four commit requests cover every headroom change, including shared registrations.
The orchestrator will stage only the headroom hunks of shared files.
