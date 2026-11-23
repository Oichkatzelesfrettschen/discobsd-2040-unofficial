# RP2040 memory ledger: re-verification and build-out

This note re-measures the memory of every RP2040 kernel at main
`6719f9d7a`, checks a November 17 research report (written against
`0a211d15eb65`, 950 commits earlier) claim by claim, and records what the
check changed in the tree. Two kernel changes follow from it: PR #890 keeps
ptrace-written text across swap, and PR #893 moves the Lean kernels' swap
exchange sector into idle SRAM5. The remaining findings are designs with
their costs and falsifiers.

Toolchain for every number: GCC 16.2.0 (`arm-none-eabi`), GNU ld 2.47.20260726,
bmake 20260912, Python 3.14.9, GNU cflow 1.8. Evidence classes stay separate:
*linked* (a measurement of a built ELF), *source* (a reading of the tree
with its file and line), *host* (a host test run), *retained* (a figure
quoted from another document). Neither Renode nor the board ran for this
note.

## Findings

1. **The PicoCalc board kernel holds the least kernel RAM.** PICOCALC_SD,
   the kernel board sessions and card images use, leaves 7,488 bytes
   between `__kernel_ram_used_end` and the u areas (6,432 in
   PICOCALC_SD_DIAG); every other kernel leaves at least 13,680. Its flash
   gap is wide (33,400 of the 160 KiB SD layout). The report's six-kernel
   table omits both SD kernels, and its PICOCALC flash gap (2,424 bytes) is
   now 6,776. *Linked.*
2. **A ptrace write into text vanished at the next swap.** swapout omits
   `[p_daddr, p_daddr + p_tsize)` and swapin reads it back from the held
   inode, while `procxmt` wrote breakpoints there through `copyout` with no
   record. PR #890 marks the image `P_TEXTDIRTY` and `swaptext()` then
   keeps it whole. The new host cases fail 4 ptrace checks and 15 fork-swap
   checks on the unmodified tree. *Source, host.*
3. **SRAM5 sits idle in the Lean kernels, and only there.** PICOCALC_LEAN
   and PICOCALC_LEAN_DIAG lack `DEV_LCDGFX`, so the tile atlas that fills
   SRAM5 elsewhere is absent; their Config still named the atlas. PR #893
   places `swap_exchange_sector` at 0x20041000: +4,096 bytes of RAM gap on
   both, flash spans unchanged. Every byte the exchange reads from the
   buffer comes from a whole-sector read earlier in the same step, and a
   failed read panics first, so the never-cleared bank is safe. *Linked,
   source, host.*
4. **One MPU region can carry the whole window; the slot it frees would
   stay idle beside three others.** A 256 KiB region at 0x20000000 with SRD 0xc0 grants
   exactly `[0x20000000, 0x20030000)` (ARM DDI 0419E: subregions apply to
   regions of 256 bytes and more, an access in a disabled subregion falls
   through to the other regions and the background map,
   and the highest-numbered matching region wins, so the region-7 break
   guard keeps working). Regions 4, 5 and 6 already sit idle
   (`mpu.c` programs 0-3 and 7), so the merge buys a fourth idle slot and
   costs edits to `mputest`, `check_mpu_startup.py`,
   `check_user_window_contract.py`, the Renode banner match and MPU.md.
   The report's premise of a scarce slot is refuted. *Source.*
5. **Every swap-out tries the compressed pool before the device, PSRAM
   included.** `swap_exchange_switch` and `swap_image_reserve` call
   `swapram_reserve` first; the PSRAM strategy routine moves raw bytes,
   and the codec runs only inside the pool tier.
   This order is the recorded open decision M2 (`memory-devices.md`,
   `picocalc-device-first-memory.md`, `swap-tier-designs.md`), with a
   pool bypass while `swapdev` names the PSRAM as the recommended option.
   The report restates it; the measurement that decides it needs the board
   (Renode lacks a PSRAM model). *Source, retained.*
6. **`RLIMIT_DATA` and `RLIMIT_STACK` bound nothing.** `brk`
   (`kern_mman.c`), the stack check at syscall entry (`syscall.c`
   `user_entry`) and signal-frame placement (`sig_machdep.c`) test only the
   window and the break; `init_main.c` sets every limit to `RLIM_INFINITY`
   and only FSIZE, CPU and CORE are read. These are the three sites
   2.11BSD patch 439 changes (`upstream-patch-survey.md`). Enforcement
   changes behavior only after `setrlimit`. *Source.*

## Kernel memory ledger

`measure_kernel_memory.py` writes `kernel-memory-ledger.tsv` from the
linked ELF files; its flash gaps equal `tools/check_kernel_headroom.py`'s
headroom for every kernel checked (PICOCALC 6,776, PICOCALC_LEAN 11,128,
PICOCALC_SD 33,400), an independent calibration of the generator.

| Kernel | Flash span | Flash gap | Kernel RAM used | RAM gap | Scratch | USB ring | Atlas |
|---|---:|---:|---:|---:|---:|---:|---:|
| PICO | 98,376 | 32,440 | 36,000 | 23,392 | 8,192 | 8,192 | 0 |
| PICO_DIAG | 100,936 | 29,880 | 36,112 | 23,280 | 8,192 | 8,192 | 0 |
| PICO_UART | 90,688 | 40,128 | 19,568 | 39,824 | 0 | 0 | 0 |
| PICOCALC | 124,040 | 6,776 | 45,712 | 13,680 | 8,192 | 4,096 | 4,096 |
| PICOCALC_LEAN | 119,688 | 11,128 | 45,208 | 14,184 | 4,096 | 4,096 | 0 |
| PICOCALC_LEAN_DIAG | 122,248 | 8,568 | 45,312 | 14,080 | 4,096 | 4,096 | 0 |
| PICOCALC_SD | 130,184 | 33,400 | 51,904 | 7,488 | 8,192 | 4,096 | 4,096 |
| PICOCALC_SD_DIAG | 155,592 | 7,992 | 52,960 | 6,432 | 8,192 | 4,096 | 4,096 |

Flash span runs from 0x10000100 to the end of the last loaded byte.
Kernel RAM used runs from 0x20030000 to `__kernel_ram_used_end` and
includes the 16 KiB SwapRAM pool. RAM gap runs to `__kernel_data_end`
(0x2003e800) and includes the 512-byte Renode dump reserve. With PR #893
the two Lean rows read 18,280 and 18,176 bytes of RAM gap and 8,192 bytes
of scratch; PR #890 adds 72 to 88 bytes of text per kernel and no RAM.

The largest ordinary objects of PICOCALC_SD, from `nm -S --size-sort`: the
SwapRAM pool 16,384; `swap_exchange_sector` 4,096; `bufdata` 4,096 (four
1 KiB buffers); the mount table 3,672; `lt` and `lt_frozen` 3,540 each (the
live and the frozen virtual console, `LCD_VT`); `proc` 3,200; `inode`
2,208; the render stack 2,048; `cfree` 1,024; the glyph bank 896.

## Call graphs

`cflow_maps.sh COMPILE_DIR OUTPUT_DIR` writes depth-4 trees for twelve
roots (scheduler, swap, SwapRAM, exec, ptrace, MPU) over the sources the
dependency files of one built configuration name, preprocessed with that
configuration's `PARAM` definitions. `call-graphs/` holds the PICO and PICOCALC_SD trees at
`6719f9d7a`, with each configuration's source list and definitions. The
two differ in `sched` by the `SDSWAP_ENABLED` arms (`swap_lost_fallback`,
`swap_lost_sweep`, `swap_lost_retire`) and in `swapram_in` by
`sr_evacuated`.

cflow resolves direct calls only. The edges it leaves out, read from the
source:

- `swap()` and `swap_with_buf_result()` (`vm_swp.c`) reach the device
  through `bdevsw[major(swapdev)].d_strategy`: `flstrategy` (NOR, `fl1`),
  `psramstrategy` or `sdstrategy` (`conf.c`). `swapdev` is chosen once
  per boot: `psramprobe()` replaces `fl1` when the PSRAM identifies
  itself, otherwise `sdswapattach()` takes the first type b8 partition,
  otherwise `fl1` stays.
- `execve` reaches the a.out, packed a.out and ELF loaders through
  `execsw`; each calls `exec_text_release()` before it installs the new
  text owner.
- MPU registers change after boot only in `mpu_guard_set()`, reached from
  `mpu_guard_update()` on each return to user (`syscall.c`, `fault.c`) and
  `mpu_guard_off()` from `systick.c`; a context switch leaves them unchanged.

Swap destination order at swap-out on PICOCALC_SD: the SwapRAM pool
(`swapram_reserve`, admitted when the codec is free, the encode fits and
the pool has a run); otherwise, on a non-removable device, the in-place
exchange over the incoming image's extent; otherwise a fresh extent on
`swapdev`. A removable SD swap device always takes the fresh extent.

## HardenedBSD mechanisms against the RP2040 MPU

Read from the local HardenedBSD clone at `521ab3484` and FreeBSD beside
it. The port has one fixed 192 KiB window holding OMAGIC text, data, bss
and stack, eight PMSAv6 regions, and no `mmap` or `mprotect` system call.

| Mechanism | Source | RP2040 mapping | Cost and consequence |
|---|---|---|---|
| PAGEEXEC: strip execute from stack and data | `hbsd_pax_noexec.c` `pax_pageexec`, `pax_noexec_nx` | an XN overlay from the first subregion past the text to the window top | one idle region, rewritten when `p_tsize` changes; the XN start rounds up to a subregion, so up to one subregion of data stays executable. `sendsig` returns through the libc `sigtramp` in text, so signal return survives |
| Read-only text | `pax_noexec_nw` | a user-read-only overlay over a text prefix | see the design below |
| MPROTECT and `PROT_MAX` | `pax_mprotect`, `vm_map.c` `max_protection` check | permissions never change after exec | holds without code |
| SEGVGUARD | `hbsd_pax_segvguard.c` | a small table keyed by text inode, counted at SIGSEGV delivery, checked at exec | kernel RAM and text the owner weighs against both tight budgets |
| ASLR | `hbsd_pax_aslr.c` | code and data stay at 0x20000000; only the stack top could move | each byte of jitter costs window; entropy stays a few bits |

## Design: the clean text is the protected text

The swapper trusts that resident text equals the executable file; finding
2 closes the kernel's own breach of that trust, and a process storing into
its own text still breaches it silently, because the window is user RWX.
A user-read-only overlay makes the trust enforceable, and choosing its
extent as the clean span makes the swap rule exact:

- One idle region of size 2^n >= `p_tsize` at 0x20000000, subregion size
  s = 2^(n-3), enables the k = floor(`p_tsize` / s) lowest subregions with
  AP privileged-RW, user-RO. It never covers data, because k * s <=
  `p_tsize`.
- `swaptext()` returns k * s instead of `p_tsize`; the bytes
  `[k * s, p_tsize)` swap as data. Since `p_tsize` > 4s, that tail is
  shorter than s and shorter than a quarter of the text. A second region
  with subregions of s / 8 over the tail cuts it below s / 8. For the
  34,884-byte shell text: one region protects 32,768 bytes (2,116 swap as
  data); two protect 34,816 (68 bytes).
- Kernel writes run privileged and pass the overlay: exec's loader, swapin
  and ptrace keep working, and ptrace still marks the image dirty (PR #890).
- A process that writes its own text takes SIGSEGV instead of a silent
  revert. That is the behavior change to qualify: a Renode run of the
  shipped programs under the overlay (`check-renode-union`) and a board run
  of `mputest` with a new text-store probe decide it; any shipped program
  that stores into its text falsifies the design for that program.
- Cost: one or two of the three idle regions, two register writes per
  region at exec and at swapin of a different image (the break-guard update
  pattern), and kernel text for the computation. The owner weighs the text
  against PICOCALC's 6,776-byte flash gap.

## Re-verification of the report

All 50 cited project files exist at `6719f9d7a`; 33 changed after
`0a211d15eb65`. Of 63 citations, 61 show the cited mechanism, 13 of them
at a moved line. Two changed: the exchange-buffer citation describes a placement that existed
only in the report's disposable experiment (now PR #893), and the MPU
citation describes a one-region map the tree never shipped. The cited
`devname()` defect is fixed on main: the name lives in a static path
buffer that survives `closedir()`. The donor claims hold in the local
clones (2.11BSD `x_count`/`x_ccount` and `choverlay()`; DragonFly
`vm_swapcache.c` burst accrual and the object-cache magazines; pfSense's
generated adaptive state limits, bounded tmpfs and the `pf_state_expires`
scaling), several at moved lines.

## Roadmap

Each item names the measurement that decides it.

1. **Swap route on PSRAM (M2).** Replay the same process mix on the board
   with the pool in front and with the pool bypassed while `swapdev` is the
   PSRAM; record switch stalls (p50, p99), pool admissions, and the largest
   free run. Decides whether the bypass lands. Owner decision recorded as
   open.
2. **`RLIMIT_DATA` and `RLIMIT_STACK`.** Add the checks at `brk`,
   `user_entry` and `sendsig`; host tests with `setrlimit` below the
   current break and stack, and mutants that drop each check. Behavior with
   infinite limits stays identical.
3. **Read-only text overlay.** Implement the design above behind a kernel
   option; qualify on Renode with the shipped programs, then on the board.
4. **PICOCALC_SD RAM.** Candidates by size: the frozen console screen
   (3,540 bytes) could live on the PSRAM block device while its console is
   in the background. A 32 KiB pool needs 16,384 more bytes: it fits the
   Lean kernels only after PR #893 (1,896 bytes of gap left), and no other
   PicoCalc kernel. Measure `kern.capacity` peaks on the board before choosing.
5. **Single window region.** Land only when a fourth idle region is
   needed; the edit list is in finding 4.

## Reproduction

```sh
PYTHON=${PYTHON:-python3}
export PYTHON
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 logged-kernel
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 picocalc-sd-kernel picocalc-sd-diag-kernel
"$PYTHON" -I docs/research/rp2040-memory-ledger/measure_kernel_memory.py \
    sys/arch/rp2040/compile/*/unix.elf \
    >docs/research/rp2040-memory-ledger/kernel-memory-ledger.tsv
for c in PICO PICOCALC_SD; do
	sh docs/research/rp2040-memory-ledger/cflow_maps.sh \
	    sys/arch/rp2040/compile/$c docs/research/rp2040-memory-ledger/call-graphs
done
```

Every source finding above names its file and was re-read at
`6719f9d7a` before it entered this note.
