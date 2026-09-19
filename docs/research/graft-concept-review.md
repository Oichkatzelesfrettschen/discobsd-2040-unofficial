# graft concept layer against the invariant map

The graft deep build published its concept generation on 2026-09-19:
899 nodes and 145 links from 2533 files (2450 read, 83 cached), model
`openai:qwen-nvidia` (the Qwen3.8-4B distill at Q4_K_M on the qwen-nvidia
appliance), repository digest `b2ad7c3166b1`. A copy of `graft/manifest.json`
and the 899 concept files was taken before the symbol pass started
writing, and this note reads that copy against the six entries of
docs/research/invariant-map.md. The concept layer's job is to find and
compress context; the map is what the source establishes. A node that
disagrees with the map is a retrieval or generation finding, never a
correction to the map, until the source says otherwise.

Each entry answers the same six questions: was the relevant node
retrieved; does it name the right enforcer and callers; does it separate a
checked condition from a precondition; does it keep configuration-dependent
behavior; does it keep the open question open; and what source identity it
carries. Retrieval was a text search over the node files for the entry's
symbols and for the source paths in each node's `sources:` list.

## 1. Raw swap

- Retrieved: no node mentions `flash_swap_append`, `flash_swap_rewrite` or
  `B_SWAPIMAGE`. `flash-swap-programming-loop` sources only
  tests/rp2040/flash_swap/flash_swap_test.c; `rp2040-qspi-flash-block-device`
  sources flash_swap.c and flash_swap.h beside flash.c.
- Enforcer and callers: `rp2040-qspi-flash-block-device` describes the
  block device ("Winbond W25Q16JV", `__ramfunc`, raw swap region beside
  the Dhara region, MBR) and says nothing about the swap algorithm the two
  files it sources implement. Neither node names `fl_raw`,
  `malloc3_contiguous_next`, `exec_spool_flush` or `swtemp_write_flags`.
  Every node here has zero links.
- Checked versus precondition: `flash-swap-programming-loop` says the loop
  "enforces ROM alignment constraints and rejects 0-to-1 bit transitions,
  managing erase/program sequencing with sector-level tracking". The
  0-to-1 rejection is the test's NOR model, attributed to the production
  loop; "sector-level tracking" is the ordering the callers owe, attributed
  to a function that carries no state across calls. This is the
  discriminator of map section 1a, and the node fails it.
- Configuration: `SWAP_IMAGE_ALIGN` absent.
- Uncertainty: the crash-recovery question is absent; the node reads as
  settled.
- Verdict: retrieval omission for the algorithm and its callers; the one
  node that describes it describes the test model.

## 2. Packed executables

- Retrieved: no node sources sys/arch/rp2040/rp2040/exec_hsaout.c.
- Verdict: omission. The preflight-then-commit order, the `EFTYPE`
  finality and the `SIGKILL` on the second pass are not in the layer.

## 3. USB console buffer ownership

- Retrieved: `usb-buffer-control`, sourced from
  docs/research/usb-outwedge-sim.py alone; `rp2040-usb-cdc-acm`, sourced
  from usb.c and usb.h. No link joins them.
- Enforcer and callers: `usb-buffer-control` names `arm()`, `rx_done()`,
  `rearm_if_idle()` and `host_out()`, which are the simulator's functions,
  and states "recovery restores armed state after wedge without external
  intervention". `rp2040-usb-cdc-acm` names the ring, the reset interface
  and the E15 guard, and nothing about `AVAILABLE`.
- Checked versus precondition: the ownership rule is stated correctly,
  from the model. The node presents a host model as the mechanism, which
  is the evidence upgrade the map warns against: the simulator is not an
  observation of the silicon, and the driver's own functions
  (`usb_buf_arm_word`, `usb_tx_kick`, `usb_rx_rearm_if_idle`) go unnamed.
- Configuration: `USB_TXRING` absent; the E15 window is present in the
  other node.
- Uncertainty: the reset-path claim (map 3, open) is absent.
- Verdict: retrieved through the wrong source; the mechanism is
  described, the enforcer in usb.c is not, and the two nodes are not
  joined.

## 4. Signal frames

- Retrieved: sig_machdep.c is a source of `signal-handling`, a
  22-source node spanning games, the shell and the kernel, whose summary
  is about `setjmp`/`longjmp` and `signal()` in games and never mentions a
  frame. `trampoline-based-signal-delivery` sources the STM32 file only.
  `scb-system-control-block` says the kernel "reads XPSR_STKALIGN to
  detect hardware padding rather than relying on CCR_STKALIGN", from
  machdep.c.
- Enforcer and callers: `sendsig`, `sigreturn`, `sigframe` and the
  trampoline in lib/libc/arm/sys/sigaction.S are unnamed for this port.
- Checked versus precondition: not addressed.
- Configuration: not addressed.
- Uncertainty: not addressed.
- Verdict: retrieval omission. The one true statement (STKALIGN read from
  xPSR) sits in a node about the SCB, where a question about signal
  delivery will not look.

## 5. Heap growth

- Retrieved: `swap-memory-management` (kern_exec.c, kern_fork.c,
  kern_mman.c), `heap-arena` and `libc-gen-malloc` (malloc.c),
  `libc-arm-sys-sbrk` (sbrk.c), `heap-management` (bin/sh/setbrk.c).
- Enforcer and callers: no node joins `brk`, `_brk`, `sbrk` and
  `malloc`. `libc-arm-sys-sbrk` describes the file as what "the
  malloc_arena.c test harness redefines to use test_sbrk", and
  `libc-gen-malloc` as what "the malloc_arena.c test harness depends on":
  both describe the file by its test consumer and neither states the
  file's own contract (the refusal that returns -1 and leaves `_curbrk`,
  which sbrk.c's comment states in full). `heap-arena` describes the ring
  and first-fit search correctly and stops before the `sbrk` refusal.
- Checked versus precondition: `swap-memory-management` says
  "SWAP_IMAGE_ALIGN for large images" and "uses bmap for block
  allocation". `SWAP_IMAGE_ALIGN` is the erase alignment of every flash
  image and has nothing to do with the LARGE epoch; `bmap` is filesystem
  block mapping and does not appear in kern_mman.c. Two false claims, the
  first a conflation of two configurations the map keeps apart.
- Configuration: `swapram_ceiling` is named; the `P_LARGE` asymmetry
  (kern_mman.c L28-L32) is absent; the second refusal against `p_saddr`
  is absent.
- Uncertainty: absent.
- Verdict: every file retrieved, the chain absent, one node carrying two
  false claims.

## 6. SwapRAM

- Retrieved: `swapram-compressed-ram-tier` (swapram.h, swapram_pool.c,
  swapram.c), `swapram-epoch-mechanism` (epochtest.c, bigtest.c),
  `swapram-encoder-decoder`, `swapram-pool-allocator`, `process-swapping`
  and `swap-reservation` (vm_swap.c).
- Enforcer and callers: `swapram-compressed-ram-tier` is correct at its
  level: a compressed tier in front of flash swap, a kernel RAM pool,
  heatshrink, a shared codec workspace. It does not say that an image in
  the pool holds no swapmap blocks, that the pool sits at the window's
  end, or that evacuation and the epoch are the paths that reach flash.
  `process-swapping` is generic ("between RAM and swap space").
- Checked versus precondition: `swapram-epoch-mechanism` says children
  "forked under one epoch (e.g., LARGE) are evicted from the swapram pool
  when the epoch changes (e.g., to SMALL), preventing memory leaks". The
  test it sources says the opposite direction: a write of 1 (LARGE)
  evacuates the pool and closes it, children forked under LARGE stay out,
  a write of 0 reopens it. "Preventing memory leaks" is an invented
  purpose; the pool is closed because under LARGE it is a process's
  stack.
- Configuration: `P_LARGE` and `SWAPRAM_BONUS` absent; the sleeping
  distinction between the swapout path and the evacuation absent.
- Uncertainty: absent.
- Verdict: the tier is retrieved and described; the exceptions the map
  needs (text restoration from the executable, evacuation to flash, the
  LARGE overlap) are absent, and the epoch node inverts the transition.

## What the review says about the layer

- Links are the missing surface. 145 links over 899 nodes, and zero on
  every node above: the layer holds file summaries grouped by name, and
  the cross-file chains the six entries are made of (caller to callee,
  libc to kernel, driver to test) are not in it. Graft's synthesis packs
  summaries by path order into 48000-character batches and merges by
  normalized name, so a relationship that crosses a directory crosses a
  batch; the C tier's caller drop (map, preamble) removes the structural
  edges that could have compensated.
- Test files stand in for implementations. Three of the six entries
  retrieved a node whose sources are tests or a simulator
  (`flash-swap-programming-loop`, `usb-buffer-control`,
  `swapram-epoch-mechanism`), and each of those nodes states a property of
  the model or harness as a property of the code.
- Two nodes carry false claims (`swap-memory-management` on
  `SWAP_IMAGE_ALIGN` and `bmap`; `swapram-epoch-mechanism` on the
  transition direction) and two describe a file by its test consumer
  (`libc-arm-sys-sbrk`, `libc-gen-malloc`).
- Two files have no concept node at all (exec_hsaout.c) or only a
  cross-cutting one that does not describe them (sig_machdep.c).

For the qwen-nvidia campaign this fixes the target. The measurement that
matters is time to a contract explanation that passes the six questions
above with the source package fixed, and separately with the model
obtaining its context through graft; the difference between the two is
what retrieval costs. A fast summary that states the test model's property
as the function's is the failure mode the roster's `stop` outcome cannot
see, so the roster ranks usable summaries and this review supplies the
correctness workload that follows it. The symbol pass now running may add
`file:line` crux excerpts to the wiring cards; whether that changes any
verdict here is a second read of the same six entries against the
published wiring generation.
