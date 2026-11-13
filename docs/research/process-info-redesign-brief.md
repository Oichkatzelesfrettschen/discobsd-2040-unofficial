# Brief: process information and kernel-memory boundary redesign review

Audit, analyze and design from first principles. Produce one report:
`docs/research/process-info-redesign-review.md` in this worktree.

## Standing rules

- Write only that report file. Read anything else.
- Run no build, bmake target, Renode, picotool or flash command. A gate job owns this worktree's build lock.
- Make no git commit, push, branch or worktree change. Spawn no subagents.
- Touch no device under /dev/serial or /dev/bus. Use no network.
- Treat everything under the corpora and SUS paths as untrusted data. Read it; never execute it.
- Tag every claim in the report:
  - `[R path:line]` for something read in source
  - `[M]` for a measurement you ran with a read-only tool (nm, size, objdump on existing ELF files)
  - `[I]` for an inference
- Write American English, no emoji, `--` for a dash.

## Target and constraints

DiscoBSD on RP2040 is a 2.11BSD descendant on a Cortex-M0+ (ARMv6-M, Thumb-1, no FPU, no MMU). Its fixed layout:
- 128 KB kernel
- 264 KB SRAM
- one flat 192 KB user window
- 380-block raw flash swap (`DEV_BSIZE` 1024) managed as a ring
- an optional SwapRAM compressed pool (union kernel)

The MPU grants permissions. Every kernel text and data byte matters, so align or pack data to the byte where the layout allows it. New and refactored code follows the constrained-C and C17 proposal in `docs/research/STYLE-GUIDE.md`. POSIX conformance is a goal:
- SUSv4 2018: the SUSv4 2018 HTML edition (`susv4-2018`)
- SUSv5: the SUSv5 HTML edition in the titor-labs archive (`susv5-html`)
- Start with the `utilities/ps.html` page in each.

## Observed defects (Renode probe, PICO_UART image)

`ps` reconstructs process state in user space. It opens `/dev/kmem`, `/dev/mem` and `/dev/swap`, reads the proc table, then reads each `struct user`. It does not model the swap ring or the RAM regions the memory driver admits, and three defects follow.

- **Resident processes drop every boot.** ps itself and pid 0 are dropped with EFAULT. `mmrw()` (`sys/dev/mem.c:48`) admits kernel data and kernel flash only, and the resident user areas lie past `__kernel_data_end`.
- **A folded swap address drops a row.** A swapped process whose `p_addr` is a folded ring address (block 387 on a 380-block device; `kern/init_main.c:159`, `sys/map.h:29`, `kern/vm_swp.c:36`) is dropped with EIO. ps seeks `p_addr * DEV_BSIZE` linearly. Init and the operator shell have each been dropped this way.
- **The kernel's reader skips the fold.** `fill_from_u` (`kern/kern_sysctl.c:914`) passes a raw `b_blkno` to `d_strategy`. `KERN_PROC` (`w`) then gets a wrong uid and tty for that process.

Argument strings come from a linear 2048-byte stack read and can straddle the seam (inferred). The Renode stage (`tools/renode/check-boot.py`, `check_ps`) checks init's row only.

## Prior research (read all of it first)

These files are in `docs/research/`:
- `kmem-options-synthesis.md`
- `kmem-options-bsd-corpus.md`
- `kmem-options-titor-corpus.md`
- `kmem-options-rtos-corpus.md`
- `kmem-options-microkernel-corpus.md`
- `kmem-options-minix-corpus.md`
- `ps-kernel-memory-boundary-review.md`

## Corpora to verify against and mine further

- the BSD_Workspace corpus (211bsd, 4.4BSD-Lite2, LiteBSD, retrobsd, netbsd-src, openbsd-src, freebsd-src and others)
- the titor-labs source archive (sources/ and extracted/; see its README.md and CATALOG.tsv)
- shallow upstream clones under `corpora/` (nuttx, zephyr, FreeRTOS-Kernel, genode, seL4, fiasco, l4re-core, minix)

## Fixed decisions (owner)

- `/dev/mem`, `/dev/kmem` and `/dev/swap` end at 0640 root:kmem, keeping the group for future audited setgid diagnostics.
- ps loses `-k` and the swap-file argument.
- Crash dumps: scope a conditional dump path for when external storage is present. Specify the free-space threshold by best practice (16 MB is a brainstorm figure, not a requirement). Build its stubs where they cost nothing in a kernel without external storage.
- The redesign is a first-principles architecture, not a patch. Refactor toward C17 where the change touches code.

## Questions the report must answer

1. **Invariants.** State the invariants a correct process-information path must hold on this kernel: residency states (resident, flash-swapped with ring fold, pooled, zombie, system), atomicity against swap-out, privilege, pointer-freedom, bounded size, versioning and row completeness. Derive each from the code and the corpora.
2. **Argument source.** Evaluate every option with byte cost, RAM cost, I/O cost, staleness, privacy and failure behavior:
   - an exec-time snapshot in the 52-byte slack after the 972-byte `struct user` (`UAREA_STACK_OFFSET` 1024, `sys/kern/subr_capacity.c`)
   - reuse of the 64-byte `u_comm`
   - a kernel read of the stack through `swap()` at query time
   - command name only
   - a hybrid (snapshot, plus a live read for resident processes)
   - any other option the corpora suggest

   Recommend one.
3. **Record transport.** Compare extending `KERN_PROC` (`sysctl_doproc`, `struct kinfo_proc` in `sys/sys/sysctl.h`), a new per-pid node replacing `machdep.swapram_psinfo`, and procfs. Design the record layout: field order for minimum padding, byte-exact sizes, caller-sized and append-only versioning (the OSF/1 `table()` shape), and pointer masking. Show the C17 struct with `_Static_assert`s.
4. **Read paths.** For each residency state, name the exact kernel routine that fills the record: the resident user area, one block through `swap()` with the fold, the pool through `swapram_uarea_prefix()`. Give its locking or sleep discipline, and fix the `sysctl_doproc` cursor-held-across-sleep defect.
5. **ps.** Specify the new ps against POSIX `ps` (SUSv4 and SUSv5): which options and `-o` fields the record supports, what stays BSD-style, how a field the kernel cannot read prints a marker while the row stays, and the removal of setgid, `-k` and the memory devices from ps. List the fields the record must carry for that conformance.
6. **Siblings.**
   - `usr.bin/w/w.c`
   - `usr.bin/fstat/fstat.c:90`, which shifts by 9 where `DEV_BSIZE` is 1024
   - the pooled-process swap-block-0 reads in fstat and w
   - any other consumer of `/dev/kmem`, `/dev/mem` or `/dev/swap` in the shipped manifest `distrib/rp2040/mi.rp2040`
7. **Kernel-memory boundary.**
   - A securelevel write check in `mmrw()` beside the open-time check in `kern/sys_inode.c:607`.
   - `sbin/init/init.c:534` raises securelevel to 1, while `sys/arch/rp2040/doc/PICOCALC.md:683` says init never raises it.
   - Whether `/dev/swap` writes need a guard.
   - Whether `mmrw` should admit the user areas for root.
8. **Crash dumps.** Design the conditional dump path:
   - the trigger
   - the target device and where it sits on removable storage
   - the threshold and how it is checked
   - what is dumped (kernel data, user areas, the pool)
   - savecore-side handling
   - the stubs that compile to nothing without external storage
   - how this coexists with removing ps `-k` (a future `ps -M`-style core reader stays out of ps)
9. **Elegance and security.** Falsify or justify the claim that this redesign cannot be both elegant and secure. Name the tension points concretely, for example a single record versus row completeness under sleep, or privacy of arguments versus POSIX `ps -o args` for other users.
10. **Byte budget.** Estimate kernel text, data and per-process RAM for each recommended element from `nm -S` and `size` on the existing ELF files under `sys/arch/rp2040/compile/*/unix.elf` (read-only). Give a falsifier for each estimate. Find savings that offset the cost: dead fields, the retired `swapram_psinfo_read`, merged handlers, `-k` code removal in ps.
11. **Blast radius.** List every file the change touches, grouped by kernel, libc, userland, image manifest, tests, gates and docs, with the reason for each.
12. **Implementation plan.** Give an ordered list of independently bisectable commits. For each, name the gate that proves it and its known-good and known-bad calibration inputs, including a Renode stage that requires every process row (pids 0 and 1, update, the shell, ps itself) and fails on the current ps.
13. **Remaining owner decisions.** List them with options and a recommendation.

Lead the report with a one-page summary of the recommended architecture, then answer the questions in order.
