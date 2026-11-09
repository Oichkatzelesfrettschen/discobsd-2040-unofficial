Drawn from local source checkouts; no board or emulator run backs any benefit figure below.

The 144 KiB memory geometry below remains bound to the surveyed revision.
`swap-exhaustion/review-04.md` supersedes the live layout with a fixed 192 KiB
window and a kernel-resident SwapRAM pool.

# Putting RP2040 core 1 to work: what the BSDs and MINIX 3 show

## Summary

The smallest correct use of core 1 is a RAM-resident observer that supervises
core 0 and owns the watchdog policy. It shares no mutable state with the
kernel, needs no flash lockout, and gives the open console-wedge problem
(`docs/research/usb-outwedge.md`) a CPU that stays alive while core 0 sits
inside a masked section. The largest measurable gain is a PicoCalc console
service that moves the polled SPI pixel pump off core 0. A big-lock SMP kernel
is closed by two facts in this tree: `u` is a global at a fixed address that
`resume` copies in and out, and one 144 KB window holds one process.

DragonFly BSD supplies the design rule: every mutable object has one owning
CPU, other CPUs ship it a request through a single-writer FIFO, and no lock
spans CPUs. MINIX 3's Reincarnation Server (RS) supplies the failure rule:
detect a hung or crashed service by heartbeat and exception, restart it with
backoff, keep its state outside it, and let clients retry. Section 5 merges the
two into three tiers. Section 6 ranks the candidate uses.

Convention: `S:` marks what a cited source shows; `I:` marks inference from it.
A figure marked `estimate` has no measurement behind it.

## 1. Evidence base and limits

Trees read, with the revision of each (git short hash unless noted):

| Tree | Revision | Note |
|---|---|---|
| DiscoBSD worktree `rp2040-second-core` | `5b6e319a` | branch `research/rp2040-second-core` |
| DragonFlyBSD | `54d71181` | shallow, one commit |
| netbsd-src | `1e8e727e461` | full history (312,264 commits); history used for dates |
| openbsd-src | `8849c5d46` | shallow, one commit |
| freebsd-src | `88e7371d9` | shallow, one commit |
| rtems-libbsd | `fbc3c66436` | |
| RetroBSD | `10e2a8d` | |
| LiteBSD | `03934b7` | LiteBSD-2 read for the same paths only |
| 211bsd | `0a38e59` | |
| 4.4BSD-Lite2 | `b10da344` | |
| 386bsd | no git revision | |
| MINIX 3, a read-only clone | `4db99f4012570a577414fe2a43697b2f239b699e` (2018-11-04, "Remove building with NOCRYPTO option") | `git clone --depth 1` of the Stichting-MINIX-Research-Foundation repository |

Paths below are relative to the named tree: DragonFly paths are under
`DragonFlyBSD/sys` and MINIX paths under `minix/` of the MINIX 3 repository
at the revision above.

Limits:

- The RP2040 datasheet is not in this tree. A text rendering of it is kept
  beside the PDF in the owner's research notes; `sys/arch/rp2040/doc/DATASHEET-INDEX.md`
  maps its sections to pages. Read and
  confirmed there: the FIFOs are 32 bits wide and eight deep per direction, the
  FIFO interrupt is the OR of VLD, ROE and WOF on IRQ 15 (core 0) and IRQ 16
  (core 1) (section 2.3.1.4, lines 2080-2118); 32 hardware spinlocks, a read
  claims and a write releases (2.3.1.3, lines 2053-2078); one integer divider
  per core (2.3.1.5, line 2121); each core has its own NVIC with the same
  interrupts routed to both (2.3.2, lines 4331-4338); eight MPU regions
  (2.4.6.1, line 5477); six SRAM banks, the lower four word-striped, with
  SRAM4 and SRAM5 as 4 KB per-core banks (2.6.2, lines 8890-8935); the core 1
  launch sequence `{0, 0, 1, vector_table, sp, entry}`, valid at any point
  after core 1 is reset, including a reset of core 1 alone (2.8.2, lines
  9625-9665); `PSM.FRCE_OFF` bit 16 forces `PROC1` into reset (table 198, page
  170, line 12404). Not in the datasheet text: any statement on write ordering
  between SRAM banks, and the absence of load/store-exclusive, which is an
  ARMv6-M property. Both stay marked `I:` below.
- No Cortex-M0+ throughput figure for heatshrink exists in the tree
  (`sys/arch/rp2040/doc/research/zswap.md`, "Speed": the board figure is an
  estimate from a 100-to-200x host ratio). Every swap or exec benefit below
  inherits that gap.
- Nothing was built or flashed for this report. Object sizes quoted for
  heatshrink come from a host compile (`arm-none-eabi-gcc` 16.2.0,
  `-mcpu=cortex-m0plus -mthumb -Os`, repository sources, repository
  `heatshrink_config.h`) and are indicative only; the kernel build flags were
  not used.

## 2. Port facts that bound every option

All `S:` unless marked.

- Core 1 is never launched. The SIO PROC handlers in `rp2040/locore0.S:252-253`
  vector to default stubs (`:302-303`), and `docs/research/usb-outwedge.md:47`
  states that `machdep.c` starts no second core.
- Every spl level is PRIMASK. `include/intr.h:185-189` maps `splhigh`,
  `splclock`, `spltty`, `splnet` and `splbio` to `arm_intr_disable()`.
  PRIMASK is per core, so it excludes nothing on the other core.
- `u` is a global at a fixed address. `sys/sys/user.h:104` declares
  `extern struct user u, u0`, and `resume` in `rp2040/locore.S:88-130` swaps
  USIZE (3,072) bytes between `u` and `u0` on every process switch. The kernel
  stack lives in that area (`doc/CAPACITY.md:190`: 1,828 of 2,048 stack bytes
  touched).
- One process is resident at a time. `rp2040/mpu.c:20-25` states that every
  process image occupies the same 144 KB at `USER_DATA_START`
  (`include/machparam.h:125-126`) and that the protection map is static.
  `lib/elf32-arm.ld` links every executable at `0x20000000`; there is no MMU to
  relocate a second image.
- Flash writes stop execute-in-place for both cores. `dev/flash.c:42-47`:
  erase and program run as `__ramfunc` with the QSPI interface out of XIP, and
  `flash_erase`/`flash_program` hold `splhigh()` around the boot ROM call
  (`dev/flash.c:299-343`). Code or rodata that core 1 fetches from flash faults
  or stalls in that window unless core 1 is parked or runs from RAM.
- Flash headroom is 11,500 bytes on the PicoCalc kernel and about 26,000 on the
  bare Pico. `sys/arch/rp2040/doc/PICOCALC.md:36`: PICOCALC text 119,316 of
  130,816 bytes (91.2 percent). `docs/research/libc-process-runtime-interfaces.md:72`:
  PICO kernel text 104,598 (80.0 percent). The brief's "about 95 percent" matches
  neither document; `sys/arch/rp2040/doc/BOOT-MAP.md:49` (90,901 bytes) is stale.
  Remeasure with `arm-none-eabi-size` on the linked `unix.elf` before any size
  gate is set (`not run` here).
- SRAM: the kernel data region is 106 KB (`conf/RP2040.ld:51`) against PICOCALC
  bss 45,888 (`PICOCALC.md:36`), leaving about 60 KB unclaimed. SRAM4 and SRAM5
  (8 KB at `0x20040000`) hold the USB transmit ring (`dev/usb.c:340-341`).
- The MPU is programmed for four of eight regions on core 0 (`rp2040/mpu.c:129-136`).
  A region on core 1 is a separate resource.
- The console renderer already runs as a deferred, bounded unit. `dev/lcdcons.c:22-54`
  pends IRQ 26 from `lcdstart`; `lcdrender` draws on a private 2 KB stack
  (`:84-85`, `dev/lcdrender.S:15-40`) for at most `LCD_RENDER_SLICE_US` = 8,000
  (`dev/lcdcons.c:291`), then lets thread mode run for one tick; `HZ` is 1000
  (`include/machparam.h:40`). A cell is 6x10 pixels at three bytes each
  (`dev/lcdterm.h:33-36`, `dev/st7365p.c:21-31`) on a 12.5 MHz SPI clock
  (`dev/st7365p.c:68-69`), so the wire carries 180 bytes in 115 microseconds;
  `docs/research/picocalc-calculator-and-gui.md:331-336` states 115 microseconds
  per cell and 195 ms for the 1,696-cell screen. The SPI loop polls the transmit
  FIFO (`dev/st7365p.c:138-142`), so the CPU is busy for the whole transfer.
  I: while output stays queued, user processes get one tick in nine, about 11
  percent of core 0.
- The USB driver already keeps head/tail rings with one writer per index:
  `usb_tx_ring` (`dev/usb.c:340-341`, `tx_head`/`tx_tail` at `:321`, producer
  `:845-848`) and `rx_ring` (`:324`, `:862-865`, drained at open `:1189-1192`).
- The hardware watchdog is deliberately disabled. `include/watchdog.h:15-33`
  records that a masked section that never returns stops the clock and the USB
  interrupt, that arming needs a reload above the 400 ms worst-case sector erase
  and a reload from every hardclock, and that `SCRATCH1`/`SCRATCH2` carry the
  masked-section name across the reset. `rp2040/systick.c:81` passes the
  interrupted PC to `hardclock`.
- SwapRAM is a pure buffer-to-buffer function. `rp2040/swapram.c:1-30` states
  that the encoder, decoder and segment table are file-scope statics, safe
  "only because swapout and swapin run to completion without a context switch";
  `include/swapram.h:76` fixes one shared 1,040-byte codec workspace.
  `hsx_expand` (`rp2040/hsx_stream.c`) is a fill-callback/decoder loop.
  `zswap.md` ("Speed") gives the flash swap path as 190 ms for a 12,416-byte
  image against an estimated 40 to 90 ms to compress it.
- Heatshrink objects (host compile, indicative): encoder 866 bytes of text,
  decoder 582. `zswap.md:19-22` measured the whole SwapRAM option at +4,148
  bytes of text, the nearest anchor for what a self-contained optional
  subsystem costs here.

## 3. DragonFly BSD, decomposed to first principles

DragonFly is the center of this survey because its design removes locks by
construction, and a pair of Cortex-M0+ cores has nothing to build locks from
(no load-exclusive/store-exclusive, 32 hardware spinlocks).

`S:` `lwkt_thread.c:36-41`: "Each cpu in a system has its own self-contained
light weight kernel thread scheduler, which means that generally speaking we
only need to use a critical section to avoid problems. Foreign thread
scheduling is queued via (async) IPIs." Everything below follows from that
sentence.

### 3.1 Mechanisms, invariants, and survival on two Cortex-M0+ cores

| Mechanism | Where | Minimal invariant it enforces | Survives on two M0+ cores? |
|---|---|---|---|
| Per-CPU ownership of a subsystem | `lwkt_thread.c:36-41`; `net/netisr.c:224-227` (one `TDF_FIXEDCPU` thread per CPU); `kern/kern_slaballoc.c:1303-1313, 1513` (a foreign free is messaged to the zone's owner) | Every mutable word has exactly one writing CPU at any instant. | Yes, and it is the only rule the hardware can enforce cheaply. Core 1 owns its services' state; core 0 owns the kernel. |
| IPI queue matrix | `lwkt_ipiq.c:162-179`; `sys/thread.h:185-196` | Each (source, target) pair has a private FIFO. `ip_windex` is written only by the source, `ip_rindex` and `ip_xindex` only by the target. | Yes. Two directions, one ring each, in SRAM. `I:` aligned 32-bit stores are single bus transfers. |
| Publish then advance | `lwkt_ipiq.c:302-307` (`cpu_sfence()` before `++ip_windex`); consume `:723-724, 747-769` (snapshot `ip_windex`, `cpu_lfence()`, process only up to the snapshot, `ip_xindex` after the call returns) | An entry is complete before its index becomes visible; the consumer never reads past a stale snapshot. | Yes. No data cache, so the barrier is a compiler barrier plus `dmb` between data and index. `I:` the `dmb` is cheap insurance where entry and index sit in different SRAM banks; the datasheet text was not available to confirm posted-write ordering across banks. |
| Function-shipping IPI | `lwkt_ipiq.c:181`; `thread.h:171-195` (`func`, `arg1`, `arg2` per entry) | A request is "run this function with these arguments on the owning CPU". The owner executes it inside its own critical section. | Yes, because both cores run the same image: a function pointer means the same thing on both. The function must be RAM-resident if core 1 can run during a flash write. |
| Doorbell elision | `lwkt_ipiq.c:313-317` (`atomic_swap_int(&target->gd_npoll, 1)`; `ipiq_avoided`); passive send `:340-391` | At most one hardware interrupt is outstanding per target; the consumer drains the whole ring per interrupt. | Partly. The atomic swap does not exist on M0+. The SIO FIFO replaces it: producer writes the ring, then pushes one FIFO word if the FIFO has room; consumer pops the FIFO, then drains the ring. A full FIFO means doorbells are already pending, so skipping the push loses nothing. |
| Full-ring drain rule | `lwkt_ipiq.c:208-262` (a sender with a full ring enables interrupts and processes its own inbound ring; `ip_drain`; "tgt not draining" report) | No CPU waits for ring space without draining its inbound ring, so two full rings cannot deadlock. | Yes, simplified. A request ring and a reply ring of equal capacity, with credit-counted requests, cannot both fill. |
| Message ports and the `EASYNC` convention | `lwkt_msgport.c:98-110, 185-205, 212-217` | A port either completes a message inline or returns `EASYNC`, meaning "I reply later". Message memory has one owner: the sender until the put, the receiver until the reply (`:90-96` assert `MSGF_DONE` and not `MSGF_QUEUED` before reuse). | Yes. Messages are static slots; the ring carries slot indices, not pointers. The inline-or-later split maps onto the BSD `strategy`/`biodone` pair (section 4). |
| Remote reply on the originator's CPU | `lwkt_msgport.c:548-585, 591-640` | Completion state (`MSGF_DONE`, `MSGF_REPLY`) is written on the CPU that owns the reply port, by IPI. | Yes: the reply goes through the reverse ring and the FIFO doorbell to core 0's `SIO_IRQ_PROC` handler (`locore0.S:252`). |
| Atomic flag updates on in-flight messages | `lwkt_msgport.c:729-738` (`atomic_set_int(&msg->ms_flags, ...)`) | Two CPUs touch `ms_flags` of one message. | Discard. Ownership alternation (previous row) means one writer at a time, so no atomic is needed. |
| Serializing tokens | `lwkt_token.c:36-45` (held only while the thread runs, released at every block; "requires ... `atomic_cmpset_ptr()`"), `:250-290` (`atomic_fcmpset_long`); `mp_token` is the old big lock: `sys/mplock2.h:20` `#define get_mplock() lwkt_gettoken(&mp_token)` | Exclusion lasts only between blocking points. This is the classic UNIX kernel contract, extended to many CPUs. | The invariant survives; the machinery is discarded. DiscoBSD already runs under the single-CPU form of the contract (`swapram.c:1-30`). Core 1 runs no blocking threads and takes no token. |
| Serializer | `lwkt_serialize.c:34-42, 105-165` (non-recursive lock held across a block, plus per-device interrupt-handler disable) | A driver's top half and its interrupt handler never run at once. | The invariant survives as local masking on the owning core (`arm_intr_disable_irq`, `include/intr.h:145`). A cross-core serializer is discarded because one core owns each device. |
| Critical section as a counter | `sys/thread2.h:38-49` (`++td->td_critcount`; interrupts become pending until exit) | Preemption is deferred, not masked. | Discard. On M0+ `cpsid i` costs one instruction, and the port's PRIMASK-only spl is already explicit (`include/intr.h:18, 79`). |
| Per-CPU scheduler and thread migration | `lwkt_thread.c:331, 1562, 1596`; `TDF_FIXEDCPU` | Threads are bound to a CPU and move only by message. | Discard threads on core 1. A run-to-completion loop replaces them: `net/netisr.c:318-345` (`netmsg_service_loop`: wait on port, dispatch handler, reply) is the model and fits in tens of lines. |
| Observation of a stuck CPU | `lwkt_ipiq.c:250-275` (after one second without drain, print `gd_sample_pc`/`gd_sample_sp` of the target and call `smp_sniff`) | A CPU that stops draining is detected and its last sampled state is reported by another CPU. | Yes, and it is the precedent for rank 1 below. `rp2040/systick.c:81` already hands `hardclock` the interrupted PC. |

### 3.2 What the big lock costs where it was built

`S:` DragonFly itself degraded its MP lock to one token (`mplock2.h:20-23`),
which shows the big lock is the coarse end of the same ownership model, not a
separate mechanism. `I:` this port gains nothing from taking the coarse end,
because section 2 shows the second CPU would have no process to run.

## 4. MINIX 3 Reincarnation Server, decomposed

Source: `servers/rs` at the revision in section 1. RS is a user-space
supervisor that starts system services (drivers and servers), detects failure,
restarts them, and publishes the new endpoint to clients.

| RS mechanism | Where | Invariant | Survives on RP2040? |
|---|---|---|---|
| Heartbeat by notification | `servers/rs/request.c:943-1041` (`do_period`: every `RS_DELTA_T` = one second, `servers/rs/const.h:48-49`, ping with `ipc_notify` at `:1036`; a ping unanswered for twice the period calls `crash_service` at `:1025`); service side `lib/libsys/sef_ping.c` (`sef_cb_ping_reply_pong` notifies back); RS records the reply in `r_alive_tm` at `servers/rs/main.c:75-90` | A reply must come out of the service's own message loop, so a wedged loop cannot answer. | Yes. A ping message through the ring plus a progress counter; a reply from the loop, not from an interrupt handler. |
| Crash detection by exit | `servers/rs/manager.c:1055-1180` (`terminate_service`) | A service that exits unexpectedly is a failure. | Yes, as a fault: a HardFault on core 1 marks the service dead. The supervisor reads a flag instead of waiting for SIGCHLD. |
| Restart with binary exponential backoff | `manager.c:1166-1172` (`r_backoff = 1 << MIN(r_restarts, BACKOFF_BITS-2)`, capped at `MAX_BACKOFF` 30 periods, `const.h:51`); first failure restarts at once (`:1177`) | Repeated failure slows restarts; a one-off failure costs nothing. | Yes, trimmed to a small retry budget (section 5.4). |
| Restart into a replica | `manager.c:1246-1310` (`restart_service`: `clone_service`, `update_service(RS_SWAP)`, `run_service(SEF_INIT_RESTART)`) | The old and new instances swap endpoints atomically. | Discard. A replica needs a second image and endpoint table; the part has one 106 KB data region. Restart in place instead and fence stale messages with an epoch number. |
| State outside the service | `drivers/examples/hello/hello.c:75-92` (`ds_publish_u32`, `ds_retrieve_u32`); `drivers/video/fb/fb.c:265-280` (three init types: `SEF_INIT_FRESH`, `SEF_INIT_LU`, `SEF_INIT_RESTART`) | A service is restartable because its durable state is held by someone else. | Yes, with the kernel's own `.bss` as the data store. The console service's durable state is the cell store, which already exists apart from the panel. |
| Clients retry | `servers/vfs/bdev.c:36-57` (`ERESTART` retried up to five times, then `EIO`); `servers/vfs/dmap.c:273-310` (`dmap_endpt_up` announces the restarted driver) | Failure of a service is hidden from callers unless it persists. | Yes. Core 0's caller retries a bounded number of times, then takes the inline path. |
| Core service death is fatal | `manager.c:1120-1124` (`_exit(1)`) | Some services have no fallback. | Inverted. Every core-1 service keeps a core-0 inline fallback, so persistent failure degrades to tier A instead of failing the system. |
| Privilege ACL per service | `kernel/priv.h:35, 86` (`s_ipc_to`, `may_send_to`) | A service reaches only the endpoints it needs. | The MPU on core 1 plays this role (section 5.5). |
| Live update | `lib/libsys/sef_liveupdate.c`, `servers/rs/update.c` | Replace a running service without stopping clients. | Discard. No second image slot, and the flash budget (section 2) has no room for it. |

## 5. Candidate architecture

### 5.1 Three tiers

- Tier A, core 0 alone (the bare Pico, and the default for every board). Core 1
  stays in the boot ROM wait. Cost: zero bytes. Every service in tiers B and C
  keeps an inline path on core 0, so tier A is also the degraded mode.
- Tier B, core 1 as a message-driven service processor (any RP2040 board). One
  option (`options SVCPROC`, left out of `OBJS` when off, the mechanism
  `sys/arch/rp2040/doc/research/zswap.md:19-22` records for SwapRAM). It adds
  the launch sequence, two SPSC rings, the FIFO doorbell, a park/resume
  handshake for flash writes, the supervisor, and the observer. Services plug
  in as dispatch table entries.
- Tier C, both cores on the PicoCalc. Tier B plus the console service, which
  owns SPI1 and the panel and executes idempotent drawing commands. The
  terminal parser and the cell store stay on core 0 (section 5.4 gives the
  reason).

### 5.2 Message layer (LWKT-style ports over the SIO FIFO)

`S:` DragonFly's ring discipline (section 3.1). `I:` the mapping:

- Two rings in kernel `.bss`, request (core 0 to core 1) and reply (core 1 to
  core 0), 32 entries of 8 bytes each (opcode, slot index, 32-bit argument):
  512 bytes per direction. Message bodies live in static slots; a slot has one
  owner at a time.
- Producer: write entry, `dmb`, advance own index, push one word on the SIO
  FIFO if it has room. Consumer: pop the FIFO to clear the interrupt, drain the
  ring to the snapshotted index, advance own read index after each handler.
- The FIFO carries doorbells only. Payload never travels in it, so the 8-deep
  FIFO cannot overflow payload, and a full FIFO is benign. `S:` a write to a
  full outgoing FIFO sets the sticky WOF flag, which raises the writer's own
  FIFO interrupt (datasheet 2.3.1.4), so the producer tests `FIFO_ST.RDY`
  before every push instead of relying on the write to fail quietly.
- Core 0 receives replies in an `SIO_IRQ_PROC0` handler that calls
  `wakeup()` or `biodone()`; a caller that needs the result runs
  the `lwkt_domsg` shape (`lwkt_msgport.c:185-205`): send, then `sleep()`
  until the reply sets the slot done. A caller on a path that may not sleep
  (the swapper) spin-waits on the slot, the same stance as `swapram.c`.
- Flow control is credits: core 0 never has more than the ring capacity in
  flight, so the reply ring cannot fill (the `lwkt_ipiq.c:208-262` deadlock
  needs both rings full).
- The 32 hardware spinlocks guard only what two writers must share, and the
  design leaves that set empty. `I:` the first use would be a signal that
  ownership was violated.

### 5.3 Flash lockout

`S:` section 2: flash erase and program take XIP away from both cores.
`I:` the cheapest rule is that core 1 code in tier B runs from RAM and touches
no flash-resident data (the observer qualifies), and that any core-1 service
that executes flash code parks first. `flash_erase`/`flash_program` already bracket the
boot ROM call with `watchdog_site()` (`dev/flash.c:299-343`); the park request
rides the same ring (opcode PARK, reply PARKED, resume on completion). A parked
core 1 spins in a RAM loop. Parking adds at most the erase time (400 ms
worst case, `include/watchdog.h`) to a service stall, which equals today's
core-0 stall.

### 5.4 Supervision, restart, and state handoff

`I:`, merging section 3 and section 4:

- Mutual heartbeat. Core 0 increments `ticks` in `hardclock`; core 1 increments
  `svc_alive` in its loop. Core 0 checks `svc_alive` from a callout every
  period and sends a PING through the ring; the reply must come from the
  dispatch loop (the `sef_ping.c` rule). Core 1 checks `ticks` against the
  declared masked-section name in watchdog scratch 1 (`include/watchdog.h`).
- Crash by exception. Core 1's HardFault vector sets `svc_state = DEAD` and
  parks in RAM. This is the RS exit signal.
- Operations are re-executable. `I:` every request a core-1 service accepts is
  a pure function of its request slot: it reads caller-owned buffers and writes
  a caller-owned output or an absolute hardware state (a pixel window, a scroll
  start), so executing it twice leaves the same result as executing it once.
  A service keeps no durable state that an operation updates in place. State
  that changes incrementally (the terminal cell store, a cursor, a parser state)
  stays with its core-0 owner. This rule replaces any need for exactly-once
  delivery, which a restart cannot give: if core 1 dies after applying an
  update and before replying, nothing outside core 1 can tell whether the
  update happened.
- Request completion state. `I:` each slot carries a 32-bit sequence number
  that core 0 assigns, a state word (FREE, POSTED, DONE) and the epoch of the
  instance that wrote DONE, then the result word: 12 bytes for each of the 32
  slots, 384 bytes. Core 1 stores the result, issues `dmb`, then stores DONE
  (the single-writer rule per field: core 0 writes sequence and POSTED, core 1
  writes result and DONE). Core 0 accepts a reply only when sequence and
  epoch match the slot; anything else is stale and dropped. The epoch
  therefore rejects late replies from a dead instance and nothing more; it is
  no deduplication, which the re-execution rule above makes unnecessary.
- Restart. `S:` core 0 sets `PSM.FRCE_OFF` bit 16 (`PROC1`, datasheet table
  198, page 170), clears it, and relaunches through the boot ROM FIFO
  sequence, which the datasheet states is valid after a reset of core 1 alone
  (2.8.2, page 131). Backoff is 1, 2, 4 periods with a budget of three
  restarts; the fourth failure marks the service down and routes all callers
  to the inline path. A restart increments the epoch and reposts every POSTED
  slot in sequence order; a slot already DONE under the old epoch keeps its
  result.
- Ownership transfer to core 0. `I:` a timeout shows only that core 1 stopped
  answering, not that it stopped touching shared state, so no timeout alone
  moves ownership. The order is: (1) core 0 stops posting new requests and
  marks the service SUSPECT; (2) core 0 sets `FRCE_OFF` bit 16 and reads it
  back as set, the same wait `multicore_reset_core1()` performs, after which
  core 1 executes nothing; (3) core 0 quiesces what core 1 left running
  outside the core: it aborts every DMA channel the service owned
  (`CHAN_ABORT`), resets SPI1 through the RESETS block so that a half-sent
  panel transfer cannot continue, and checks `SPINLOCK_ST` for a lock the
  service held (the design takes none); (4) only then does core 0 either
  relaunch under the next epoch or run the inline path, which executes the
  POSTED slots itself. Falsifier: a core-1 progress counter or a logic
  capture on SPI1 shows activity after step (2) completes, or a slot executed
  inline and again by the relaunched instance produces a different result.
  Step (3) rests on an inference: table 198 gives `PROC1` its own bit apart
  from the bus fabric and the peripherals, so forcing the core off does not
  stop a DMA channel or reset SPI1. Not run: the datasheet does not say what
  happens to an AHB transfer the core had in flight when `FRCE_OFF` takes it.
- Console state. `I:` the terminal stays a core-0 object. `lcdterm.c` mutates
  it in several steps with no commit point: `copy_row()` copies cells and
  attributes one at a time, `scroll_up()` loops over rows and then blanks the
  last, a full-screen scroll blanks row 0, advances `top` and reprograms the
  panel in three separate steps, and `put_glyph()` runs `index_down()` (a
  scroll) before it writes the glyph. A core-1 reset in the middle of a region
  scroll leaves rows t+n through r+n copied up to t through r while the rows
  below r stay unshifted: some rows appear twice and the n blank rows are
  missing. A redraw from the store paints that inconsistent screen, and no
  record says the scroll was half done, so the restart can neither finish nor
  undo it; replaying the byte would apply the scroll a second time. That
  exceeds the loss of one escape sequence. Core 1 therefore never writes the
  store. Core 0 keeps parsing, mutating the store and choosing what to draw,
  and ships the drawing as absolute commands: draw the cells (prow, column,
  count) from the store as it stands when the command runs (core 1 reads the
  store and never writes it; a read that races a core-0 write draws a stale
  cell that the later command for the same cell repairs), fill (prow, column,
  count, attribute), set the scroll start (prow). Each command names its target
  by physical row and carries no delta, so replaying any prefix of the ring is
  harmless, and the commands for one store write are issued after that write,
  so the panel converges on the store once the ring drains. Each command packs
  into the 32-bit argument of a ring entry (row 5 bits, column 6, count 6,
  attribute 8). After a restart core 0 clears its cursor-drawn flag, sends
  set-scroll-start and 32 row-draw commands (1,696 cells, about 207 ms at 122
  microseconds a cell, `PICOCALC.md`), then resumes; no input is lost, because
  the store was never at risk. The alternative, moving the store to core 1 with
  a committed snapshot, costs a second copy of the cells and attributes (2 x 32
  x 53 = 3,392 bytes) plus the parser and mode scalars and the ring cursor,
  committed together at each sequence boundary, and it still leaves the kernel
  `putc` path, `lcdabandon()` and `lcdterm_abort()` (all core-0 writers of the
  same store, at interrupt level and during panic) either shipping to core 1 or
  racing with it. That second writer is why the snapshot design does not
  survive; the store stays where its writers are.
- Client retry. A request that gets no reply within two periods, or a reply
  with a stale epoch or sequence, is retried up to five times (the
  `bdev.c:36-57` bound) before the caller runs the inline path, which first
  completes the ownership transfer above.

### 5.5 Isolation on core 1 (the MPU as the privilege ACL)

`I:` The MPU of core 1 is programmed by core 1 and costs no core-0 region. A
service run with CONTROL.nPRIV set needs: its code (RAM, read/execute), its
stack with a guard subregion, its rings and slots, and the peripherals it owns
(for the console service SPI1 and the GPIO block), plus the SIO FIFO
subregion. That is six or seven of eight regions. The SIO block already
shows the technique: `rp2040/mpu.c:129-136` exposes only the divider
subregion of the 256-byte SIO region (`SRD 0xf7`); the FIFO and spinlock
subregions would be granted by the same mechanism on core 1. A wild store from the
service then faults on core 1 and the supervisor restarts it, where today it
would corrupt kernel `.bss`.

### 5.6 Tests of each part against the RP2040 limits

| Limit | Tier A | Tier B (substrate + observer) | Tier C (adds console service) |
|---|---|---|---|
| 144 KB user window | untouched | untouched; no service data lives in the window | untouched |
| 128 KB kernel flash (91.2 percent used on PicoCalc, 11,500 bytes free) | 0 | estimate 1.0 to 1.5 KB: launch, rings, doorbell, park, supervisor, and the observer's 300 to 600 byte load image, which `kern.ldscript` stores in flash after `.data` and the boot copy loop moves to SRAM | not estimated: core 1 runs from SRAM only, so the panel draw path (`lcd_draw_cell`, `lcd_fill_cells`, `lcd_set_top` and the SPI byte writer in `st7365p.c`) and the font table must be linked into `.ramfunc` and stored in flash a second time. Measure with `arm-none-eabi-size` on `st7365p.o` before ranking this tier; the parser stays in the existing `lcdterm.c` text |
| 264 KB SRAM | 0 | rings 1,024 bytes, slots and state 384, observer stack 512: about 2 KB of the roughly 60 KB free | core-1 stack reuses the 2 KB `lcd_render_stack` (`dev/lcdcons.c:84-85`), net zero; the cell store stays in place (no snapshot copy); the 855-byte font table and the draw path live in SRAM because core 1 never reads flash |
| MPU per core, 8 regions | 4 used (`mpu.c:129-136`) | core 1 observer privileged, no regions required | six or seven regions on core 1 |

Any code that core 1 executes counts twice: once as its load image in kernel
flash and once as the SRAM copy. The observer is no exception.

The "95 percent" flash claim in the brief was not reproduced; if a newer build
is closer to full, the Tier C figures still hold because the reuse argument
does not depend on the headroom, but tier B's 1.0 to 1.5 KB needs a fresh
`size` check.

### 5.7 What was merged, what was discarded

Merged: DragonFly's single-writer ring and function-shipping request;
doorbell elision via the FIFO; the `EASYNC` inline-or-later reply; RS's
ping-through-the-loop heartbeat, restart with backoff, state held outside the
service, client retry; the observer as the RS-in-reverse supervisor.

Discarded, with reasons:

- Tokens, serializers, critical-section counters, per-CPU schedulers, remote
  slab free: no blocking threads on core 1, no malloc on core 1, PRIMASK
  suffices locally (section 3.1).
- Atomic flag updates and compare-and-set: absent on M0+ and unnecessary under
  ownership alternation.
- Replica restart and live update: no second image, no flash.
- A shared big lock and a second kernel thread: section 6, rank 6.
- Pointer-carrying messages: slot indices only, because a restarted core 1
  must not follow a pointer into freed or rewritten memory.

### 5.8 Falsifier for every surviving claim

| Claim | Falsifier (what result retires it) |
|---|---|
| Ring plus FIFO doorbell loses no message and none twice | A stress run of 10^8 messages with random producer and consumer stalls and per-message sequence numbers shows any gap or duplicate. |
| A full FIFO never strands a pending ring entry | The same run with the consumer held for 100 ms (FIFO full) ends with entries unread after release. |
| Credit flow control prevents ring deadlock | Any run where both rings reach capacity at once. |
| Observer detects a core-0 hang inside two periods and names the section | Fault injection: `splhigh()` then spin, with and without a declared `WD_SITE`. A hang not reported, a legitimate 400 ms flash erase reported as a hang, or a wrong section name retires it. |
| Observer failure never harms core 0 | Kill core 1 mid-run (PSM reset): core 0 loses diagnostics only; any user-visible effect retires it. |
| Restart restores the console from the core-0 cell store | Kill core 1 at random points in a VT100 stream 1,000 times, including inside region scrolls; compare the panel model after the full repaint with the reference model that `check-picocalc-host` provides. Any residual difference retires it; no dropped sequence is allowed, because the store is never at risk. |
| Drawing commands are idempotent | Replay each ring entry twice, and replay every prefix of a recorded stream after a restart; the panel model must equal the single-pass model. A difference retires the command set. |
| Core 1 is quiescent before core 0 takes ownership | After `FRCE_OFF` reads back set and the DMA and SPI1 steps run, a logic capture of SPI1 and a core-1 progress counter show no activity; core 0 then draws inline. Activity after that point retires the transfer order. |
| A request executed twice returns the same result | Kill core 1 between the result store and the DONE store, 10^5 times, for each opcode; the inline or relaunched result must be bit-identical. A difference marks the opcode as not re-executable, so it may not run on core 1. |
| Console service lifts the render cost off core 0 | Run a fixed compute loop in a user process while `cat` of 100 KB drives the panel; iterations per second must exceed the polled path by the amount the 1-in-9 tick share predicts (I: about 5 to 8 times). Less than 2 times retires it. |
| SPI1 DMA alone would not be enough (justifies core 1 for the panel) | A DMA-driven `lcdrender` on core 0 recovers more than 90 percent of the CPU share: then core 1 adds too little, and the console service is dropped in favor of DMA. |
| Parking core 1 around flash writes costs no more than today's stall | Maximum service gap during `flash_erase` exceeds the erase time by more than one period. |
| Core 1 faults rather than corrupt kernel memory | A deliberate wild store from the service runs without a fault, or corrupts a `.bss` canary on core 0. (`sys/arch/rp2040/doc/MPU.md` states the evidence form the port requires: written and read-back register values, region coverage, a deliberate fault test.) |
| Region count fits | Linking the service needs a ninth region on core 1. |
| Core 1 stops on every reboot path | After `reboot`, panic and a forced fault, a core-1 progress counter in `.bss` keeps advancing once the next kernel is running (section 5.9). |

### 5.9 Interactions the companion hardware report adds

`docs/research/rp2040-second-core-hardware.md` (a separate document written
alongside this one; read, not edited) reaches the same ordering of uses (a
sentinel for the console wedge, a display and I/O service on the PicoCalc, a
splitter of independent segments) from the datasheet and the shipped code. It
adds four constraints this design absorbs:

- Reboot path. `S:` (that report, "Reset scope matters here", citing datasheet
  2.4.2.9 and `machdep.c` 748-776) the kernel reboots with
  `AIRCR.SYSRESETREQ`, which resets only the requesting core, so a running
  core 1 survives a software reboot and keeps executing SRAM the next boot
  overwrites. `I:` the supervisor stops core 1 through `PSM.FRCE_OFF` bit 16 at
  the top of `boot()`, in the panic and fault paths, and at kernel startup.
  Falsifier: a board reboot with a parked core-1 counter that keeps advancing
  after the new kernel starts.
- Separate vector table. Core 1 needs its own table in SRAM; the SDK default
  of sharing core 0's table sends core-1 faults and interrupts to core-0
  handlers.
- Single-core ceiling first. The report's instruction-level emulation puts the
  shipped packed-executable decoder at 387 cycles per expanded byte, 144 ms
  per pass over a 46,540-byte image, and attributes 54 percent of it to the bit
  reader and 20 percent to the bit-serial CRC-32. The sample, packer, harness
  and model are in `docs/research/rp2040-second-core-emulation/`, whose
  `build.sh` rebuilds them from tree sources and reproduces the figure; it is
  an emulated cycle count, not a board measurement. `I:` a table CRC or a
  word-buffer bit reader may remove more than half of that on core 0 alone,
  which moves rank 3 (codec worker) behind a single-core optimization and
  sets its first falsifier: measure the decode on the board, then re-rank.
- Flash headroom. That report counts about 22 KB left on the bare Pico where
  section 2 derives 26,218 bytes from the 104,598-byte figure
  (`libc-process-runtime-interfaces.md:72`); the difference is a later build.
  Both agree that the PicoCalc headroom is the binding figure (11,500 bytes).

## 6. Ranked candidate uses of core 1

Ranking criterion: measurable benefit per byte of kernel flash and per unit of
risk. Each entry gives precedent, mechanism, cost, benefit, risk, falsifier.

### Rank 1. Observer and watchdog-policy owner (tier B)

- Precedent. `S:` DragonFly reports a non-draining CPU's sampled PC and SP from
  another CPU (`lwkt_ipiq.c:250-275`). `S:` NetBSD's kernel-lock spinout code
  sends an IPI to the lock holder to print "hogging kernel lock"
  (`kern/kern_lock.c:143-150, 268-281`). `S:` the VAX 8600 console processor
  is a CPU that stays alive when the main CPU is wedged
  (`4.4BSD-Lite2/sys/vax/vax/crl.c`).
- Mechanism. `I:` core 1 runs a short RAM-resident loop. It reads `ticks`, the
  watchdog `SCRATCH1`/`SCRATCH2` site record (`include/watchdog.h`), and the USB
  device-controller status words. If ticks stop advancing with no declared
  masked section, or advance beyond the declared deadline, it writes an
  evidence record (tick, section, controller registers, `machdep.usb_*`
  counters) to scratch SRAM and to UART0 (the fallback console, independent of
  core 0 and USB), then triggers the watchdog reset. The base watchdog reload
  stays with core 0's hardclock (the design `include/watchdog.h` records), so
  observer death removes diagnostics and nothing else.
- Cost. `estimate:` 300 to 600 bytes of code in SRAM, about 512 bytes of RAM
  stack, and the same 300 to 600 bytes again in kernel flash: `kern.ldscript`
  places `.ramfunc` at a load address after `.data` in flash, and the boot copy
  loop moves it to SRAM, so a blob linked for SRAM is stored in flash. The
  launch and reset code on core 0 adds its own text (the companion report
  estimates 300 to 500 bytes), for 600 to 1,100 bytes of flash in all, which
  is 9 to 17 percent of the roughly 6.5 KB the companion report counts free on
  PicoCalc and 5 to 10 percent of the 11,500 bytes this report counts.
- Benefit. `I:` converts the open wedge (`console-silent-after-web-session.md`
  records one occurrence with the cause "not established"; `usb-outwedge.md`
  models a related bulk-OUT wedge) from "device enumerated, console dead" into
  a captured record. Measured as time-to-detect and the presence of the
  controller registers in the record.
- Risk. Low. Shares no mutable word with the kernel; reads only. The one new
  hazard is core 1 reading a peripheral while core 0 writes it; reads are
  side-effect-free if the observer avoids read-to-clear registers (check the
  USB `SIE_STATUS` and FIFO registers before listing them).
- Falsifier. Section 5.8 rows 4 and 5.

### Rank 2. PicoCalc console service (tier C)

- Precedent. `S:` DragonFly fixed-CPU netisr thread owning its protocol state
  (`net/netisr.c:224-227, 318-345`); `S:` NetBSD VAX funnels secondary-CPU
  console output to the primary (`arch/vax/vax/multicpu.c:136, 165-178`, commit
  `4cdc1a6a981` of 2000-06-10: "Haven't solved the printf() problem yet");
  `S:` VAX 8600 console processor driven by command register, ready bit and
  interrupt-enable (`crl.c:145-165`, `STXCS_RDY`, `STXCS_IE`).
- Mechanism. `I:` core 1 owns SPI1 and the panel; core 0 keeps the parser and
  the cell store (section 5.4, "Console state"). The renderer and kernel `putc`
  on core 0 parse as today and, instead of calling `lcd_draw_cell()`,
  `lcd_fill_cells()` and `lcd_set_top()`, append the matching absolute command
  to the request ring under PRIMASK and ring the doorbell. One panel owner
  removes the owner flag and the deferred ring that exist only because two
  drawers share the panel (`dev/lcdcons.c:38-54`) from the panel side; they
  remain as the serialization of core-0 writers to the ring and the store.
  Backpressure: a full ring leaves the tty busy until core 1 signals drained.
- Cost. Section 5.6 column C. Panic printing needs a synchronous core-0 path:
  force core 1 off through the ownership transfer in section 5.4, reset SPI1,
  then take the existing `lcdabandon` route, which now meets a store that no
  core-1 reset can have left half updated.
- Benefit. `S:` 115 microseconds per cell, 195 ms per full screen, CPU-polled
  (`picocalc-calculator-and-gui.md:331-336`). `I:` removes up to 89 percent of
  core 0 time during sustained output (8 ms slice per 1 ms tick,
  `lcdcons.c:291`, `machparam.h:40`), less the parse, store update and command
  enqueue that stay on core 0, which no measurement bounds yet. Falsifier: if
  that residual exceeds a quarter of today's per-byte cost (GPIO bracket around
  `lcdterm_putc()` with and without the draw calls), the benefit falls below
  what DMA on core 0 gives.
- Risk. Medium: flash lockout, font rodata in flash, panic path, tty
  backpressure. The wire time (196 ms per full screen) does not shrink.
- Falsifier. Section 5.8, the iterations-per-second row and the DMA row. SPI
  DMA on core 0 is the cheaper competitor; run the DMA experiment first and
  keep core 1 only if it recovers less than 90 percent of the share.

### Rank 3. Codec worker for SwapRAM, packed executables, CRC32 (tier B)

- Precedent. `S:` 2.11BSD `swap()` hands a buffer to the driver and sleeps on
  `B_DONE` (`211bsd/sys/sys/vm_swp.c:56-59`); the `strategy`/`biodone` seam is
  the BSD offload interface. `S:` OpenBSD `taskq` with the `TASKQ_MPSAFE` flag
  marks work that needs no kernel lock (`sys/sys/task.h:36`,
  `kern/kern_task.c:430, 457`); NetBSD `workqueue` has `WQ_MPSAFE` and
  `WQ_PERCPU` (`sys/sys/workqueue.h:50-52`).
- Mechanism. `I:` a job is {opcode, source, length, destination, capacity,
  workspace slot}. Core 1 owns a second 1,040-byte codec workspace and may use
  the index (`HEATSHRINK_USE_INDEX`) that `heatshrink_config.h` leaves off
  because it costs 2,048 bytes of bss and a 512-byte stack frame the 220-byte
  core-0 stack headroom cannot take; core 1 has its own stack. Parallelism
  comes from independent streams: the data, stack and u-area segments of one
  swapout (`swapram.c`), or the counting pass and the real pass.
- Cost. `estimate:` +1.0 KB of core-1 code beyond tier B, held in kernel flash
  as a load image and again in SRAM, plus 1,040 bytes of RAM; encoder 866 and
  decoder 582 bytes of text at `-Os` on the host compile.
- Benefit. `I:` weak. The segments are unequal (the data segment dominates), so
  a two-way split saves at most the smaller segment's time; `zswap.md`
  estimates 40 to 90 ms to compress an image against a 190 ms flash path, and
  a 12 KB image decodes in less. Nothing overlaps because one window holds one
  process, so the waiting caller has no other work. Idle-time pre-compression
  fails for the same reason: a process sleeps at the moment another becomes
  runnable, leaving no idle interval (reasoned from `sched()` in
  `211bsd/sys/sys/vm_sched.c:34`, not tested).
- Risk. Low to medium. The codec is a pure function; the hazard is the
  workspace owner rule (`include/swapram.h:76`) and flash lockout.
- Falsifier. First measure the Cortex-M0+ encode and decode rate on the board
  with `pc_micros()` around `swapram_put`; if a swapout is under 100 ms, retire
  the candidate. After building it, retire it if a two-segment swapout is less
  than 1.3 times faster.

### Rank 4. USB device service (tier B or C)

- Precedent. `S:` FreeBSD `intr_event_bind` pins an interrupt to a CPU
  (`kern/kern_intr.c:384-420`); `S:` NetBSD ARM wraps non-MPSAFE handlers in
  `KERNEL_LOCK` (`arch/arm/pic/pic.c:359-364, 764`); `S:` rtems-libbsd routes
  every device interrupt through one interrupt-server task
  (`rtemsbsd/rtems/rtems-kernel-irqs.c:54`, `rtems-kernel-nexus.c:348`) and
  pins taskqueue threads with `rtems_task_set_affinity`
  (`freebsd/sys/kern/subr_gtaskqueue.c:508, 805`); `S:` RetroBSD runs
  `usb_device_tasks()` "from interrupt or periodically"
  (`retrobsd/sys/pic32/usb_uart.c:230-251`).
- Mechanism. `I:` the USB IRQ is enabled in core 1's NVIC only. The existing
  `usb_tx_ring` and `rx_ring` (`dev/usb.c:321-324, 845-865`) become the request
  and reply rings; `ttyinput` runs on core 0 from the doorbell handler. The
  driver then never re-enters from core 0's fault path
  (`dev/usb.c:940-950`).
- Cost. `estimate:` usb.c is 1,348 lines; running it through flash writes needs
  a RAM-resident copy of several KB, otherwise it parks like any other service.
- Benefit. Unproven. Bulk USB tolerates a 45 ms gap by NAK and retry, so the
  steady-state gain is small; the possible gain is removing the re-entry hazard
  that `usb_service_depth` papers over, and no evidence ties that to the field
  wedge.
- Risk. Medium to high: the tty layer is single-core by construction, and the
  wedge cause is unknown, so moving the driver could hide a root cause.
- Falsifier. Hold until the rank 1 observer produces a wedge record. Then a
  record that implicates USB controller state independent of core 0 PRIMASK
  sections admits it; a record that does not retires it.

### Rank 5. Attached-processor user compute (research)

- Precedent. `I:` the VAX-11/782 attached-processor model, where one CPU runs
  user code and the other runs the kernel. The VAX-11/782 is not in any local
  tree; this is literature knowledge, not a source citation. A search of
  4.4BSD-Lite2 and 2.11BSD kernels for `multiprocessor`, `attached proc` and
  `782` returned only `NCPUS == 1` stubs (section 7).
- Mechanism. `I:` core 1 runs a user function in the same 144 KB window, with
  CONTROL.nPRIV set and the same four MPU regions as core 0; it may not issue
  SVC. The kernel starts it through one request and stops it by FIFO interrupt
  before any swapout, exec, signal or exit that touches the window.
- Cost. `estimate:` 1 to 2 KB of text and one syscall or ioctl.
- Benefit. Up to about 1.9 times on embarrassingly parallel soft-float loops;
  nothing for I/O-bound programs. libc is not thread-safe, so user code is
  leaf compute only.
- Risk. High: the window is the shared mutable state, and the kernel writes it
  (copyout, signal frames) while core 1 runs. Every kernel writer into the
  window needs a quiesce.
- Falsifier. A two-way split of a fixed soft-float kernel is less than 1.3
  times faster than one core, or any kernel path writes the window without the
  quiesce and the stress test detects it.

### Rank 6. Big-lock SMP kernel (rejected)

- Precedent, `S:`. NetBSD moved `cpu_info` into the Alpha port on 1999-08-10
  (`8ca5bf728f5`), made `curproc` per-CPU on 2000-05-31 (`26bdb856cb7`), added
  the scheduler lock on 2000-08-20 (`c67eddfc6d0`) and the machine-independent
  big-lock perimeter on 2000-08-22 (`7f0784bda9e`: lock at init, in the fork
  trampoline, in `postsig`, released and re-acquired in `ltsleep`), and wrapped
  Alpha trap entry on 2000-09-04 (`7606076d3f3`: "Still doens't work properly
  yet"). The Lite2 lock package was imported on 1997-07-06 (`cc9610732fb`) with
  `simple_lock` stubs under `NCPUS == 1` (`4.4BSD-Lite2/sys/sys/lock.h:172-177`)
  and a test-and-set definition for `NCPUS > 1`
  (`sys/hp300/include/param.h:217-260`).
- Minimum a big-lock kernel needs, `S:` from those commits: per-CPU
  `curproc` and a `cpu_info` (`curcpu()`; on ARM it is the TPIDRPRW register,
  `arch/arm/include/cpu.h:276-296`, which Armv6-M lacks); a lock with
  recursion count, released on sleep and re-acquired on wake; a lock-holding
  fork trampoline; a hold around every trap and syscall entry; non-MPSAFE
  interrupt handlers wrapped in the lock; inter-processor interrupts, including
  a TLB flush type (`arch/vax/vax/multicpu.c:153-190`, `IPI_TBIA` at line 181); secondary
  bring-up with its own interrupt stack (`multicpu.c:63-113`); and console
  output routed to the primary.
- What this port keeps and drops. `I:` TLB and cache shootdown vanish (no MMU,
  no data cache). `curcpu()` becomes the SIO CPUID register. But `u` is a fixed
  global exchanged by `resume` (`locore.S:88-130`, `user.h:104`), the kernel
  stack lives in it, and one window holds one process (`mpu.c:20-25`), so the
  second CPU has no process, no `u` and no stack to run. Making `u` per-CPU
  touches every `u.u_` reference; making a second window needs
  position-independent executables the toolchain does not produce.
- Falsifier. The rejection is retired if a design gives each core its own `u`
  and its own user window without moving the 144 KB figure the packed-executable
  and SwapRAM paths depend on.

## 7. Survey answers

### 7.1 Asymmetric and attached-processor multiprocessing in the BSDs

- `S:` 2.11BSD has no multiprocessor code. `grep -rIn -i "multiprocessor\|attached proc\|MASTER_CPU\|vax.*782" 211bsd/sys` returned nothing.
- `S:` 4.4BSD-Lite2 carries only the uniprocessor shape of the MP interface:
  `NCPUS` defined as 1 in `hp300/include/param.h:47`, `news3400/include/param.h:47`,
  `sparc/include/param.h:51`, `pmax/include/param.h:47`, `luna68k/include/param.h:50`;
  `simple_lock` compiled away at `sys/lock.h:172-177`; `lockmgr` spins only if
  `NCPUS > 1` (`kern/kern_lock.c:56-70`); several pmaps state
  "Don't even pay lip service to multiprocessor support" (`hp300/hp300/pmap.c:50`).
  The VAX tree has no attached-processor driver; the only VAX console-processor
  code is the 8600 console RL02 driver (`vax/vax/crl.c`).
- `S:` 386bsd keeps Mach's `lock.h` ("Sleep locks must work even if no
  multiprocessing", `usr/src/kernel/include/lock.h:94`) and no SMP code.
- `S:` the earliest multiprocessor code in the local trees is NetBSD's, 1997
  to 2000 (section 6, rank 6), plus the VAX CPU spin-up for the 8200 and 8800
  (`4cdc1a6a981` of 2000-06-10, `9d6b952ec4c` of 2000-07-26). That code is
  symmetric with a primary-CPU role for console output, not asymmetric: only
  the primary CPU handles console characters (`multicpu.c:165-178`,
  `IPI_START_CNTX` and `IPI_SEND_CNCHAR` panic off the primary).

### 7.2 Offload patterns that need no SMP kernel

| Pattern | Source | Maps onto two RP2040 cores? |
|---|---|---|
| Strategy and `B_DONE` completion | `211bsd/sys/sys/vm_swp.c:56-59` | Yes. The request ring and `biodone()` from the doorbell handler are the same seam. |
| Console or I/O processor driven by command register, ready bit, interrupt enable | `4.4BSD-Lite2/sys/vax/vax/crl.c:145-165` | Yes. The SIO FIFO is the register pair. |
| Descriptor ring with a hardware ownership bit | `LiteBSD/sys/mips/dev/if_en.c:63-70, 89-90, 611` (`DESC_EOWN`) | Yes. It is the slot-owner rule of section 5.2. |
| `taskq` and `workqueue`, with an MPSAFE flag | `openbsd-src/sys/sys/task.h:36`; `kern/kern_task.c:403-440`; `netbsd-src/sys/sys/workqueue.h:50-52` | Yes, as the dispatch table. The MPSAFE flag becomes "touches only state the service owns". |
| Interrupt binding | `freebsd-src/sys/kern/kern_intr.c:384-420`; `netisr.c:36-70` policies | Yes, and trivially: each core has its own NVIC with the same interrupts routed to both (datasheet 2.3.2, page 60), so binding is enabling the IRQ on one core. |
| Non-MPSAFE handlers under the big lock | `netbsd-src/sys/arch/arm/pic/pic.c:359-364`; `x86/x86/intr.c:841, 985-995` | No. The tty layer is the non-MPSAFE part, which is why rank 4 keeps `ttyinput` on core 0. |
| Per-CPU message passing (LWKT) | section 3 | Yes. The best fit, because it needs no atomics. |
| Zero-copy compression helper | DiscoBSD `swapram.c` already is one | Yes; rank 3. |

### 7.3 RetroBSD and LiteBSD

- `S:` Neither has a second core, multiprocessor option, or coprocessor driver
  (`grep -rIn "SMP\|MULTIPROC" retrobsd/sys LiteBSD/sys/mips LiteBSD/sys/kern` returns none).
- `S:` RetroBSD does not use its DMA controller. The only DMA reference in its
  PIC32 machine code stops DMA before reset (`retrobsd/sys/pic32/machdep.c:594-598`).
  LiteBSD uses DMA once, as an Ethernet descriptor ring (`if_en.c:63-70, 597-630`).
- `S:` Swap is a proxy that forwards to the swap device
  (`retrobsd/sys/pic32/swap.c:1-4`); neither tree compresses swap.
- `S:` Display and storage paths are polled SPI FIFO loops
  (`retrobsd/sys/pic32/gpanel-spi-ili9341.c`, `spi_bus.c:151`;
  `LiteBSD/sys/mips/dev/spi.c:265-320`), the shape DiscoBSD's `st7365p.c` has.
- `S:` USB CDC runs `usb_device_tasks()` from the interrupt or periodically
  (`retrobsd/sys/pic32/usb_uart.c:230-251`, called at `exception.c:346, 382`).
- `S:` `docs/research/retrobsd-port-scope.md:477` already records the decision to
  leave core 1 unused as an explicit choice.

### 7.4 rtems-libbsd

- `S:` The README requires a scheduler with thread pinning for FreeBSD EPOCH(9)
  (`README.md:592-598`); the epoch code pins threads and sends SMP unicast
  actions (`rtemsbsd/rtems/rtems-kernel-epoch.c:233, 255, 320, 361`).
- `S:` Taskqueue threads are bound to CPUs with `rtems_task_set_affinity`
  (`freebsd/sys/kern/subr_gtaskqueue.c:508, 805`).
- `S:` Device interrupts are not bound to a core: `rtems_interrupt_server_initialize`
  creates one server (`rtems-kernel-irqs.c:54`) and every handler installs with
  `RTEMS_ID_NONE` (`rtems-kernel-nexus.c:348`).
- `S:` Per-CPU NETISR is an open item (`CONTRIBUTING.md:683-687`), and priority
  queues break under clustered scheduling.
- `I:` rtems-libbsd shows that a small embedded SMP part ships the BSD network
  and USB stacks unbound, and that pinning is applied only to worker threads.
  It contributes the "pin the worker, not the interrupt" pattern, which is the
  shape of rank 4.

## 8. Checks to run before any code (not run here)

1. Settle the two hardware questions the datasheet text leaves open: write
   ordering between SRAM banks (decides whether the `dmb` between entry and
   index is required or only prudent), and whether reads of the USB
   controller status registers the observer samples clear state. The divider
   is per core (datasheet 2.3.1.5), but `mpu.c:44-47` says
   `tools/verify_rp2040_divider_ownership.py` fails any linked kernel with a
   divider consumer; core-1 service code must avoid division or the gate needs
   a scoped, calibrated exception.
2. Measure on the board: PICOCALC and PICO text with `arm-none-eabi-size`
   on the linked ELF; heatshrink encode and decode rate with `pc_micros()`;
   user-process iteration rate during `cat` of 100 KB to the panel; USB service
   gap across `flash_erase`.
3. Calibrate each new gate against a known-good and a known-bad input
   (AGENTS.md "Evidence"): the ring stress test against a ring with the `dmb`
   removed; the observer against a deliberate PRIMASK spin and against a real
   400 ms erase.
4. Run the DMA experiment before the console service.

## 9. Reproducing the searches

```
grep -rIn -i "multiprocessor\|attached proc\|MASTER_CPU\|vax.*782" 211bsd/sys
grep -rIn "NCPUS" 4.4BSD-Lite2/sys
git -C netbsd-src log --diff-filter=A --format='%h %ad %s' --date=short \
    -- sys/arch/vax/vax/multicpu.c
git -C netbsd-src log --reverse --format='%h %ad %s' --date=short \
    -- sys/kern/kern_lock.c
git -C netbsd-src show 7f0784bda9e 7606076d3f3
grep -n "ipiq\|windex\|rindex" DragonFlyBSD/sys/kern/lwkt_ipiq.c
git -C minix rev-parse HEAD
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -c \
    sys/arch/rp2040/heatshrink/heatshrink_encoder.c \
    -Isys/arch/rp2040/heatshrink -Isys/arch/rp2040/heatshrink/compat
```
