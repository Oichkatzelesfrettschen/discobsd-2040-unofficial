# Kernel and userspace footprint work queue

This document corrects the capture kept beside it as
`kernel-footprint-handoff.md.BAK`. Three items were read again on
2026-11-18 at main `bff303aae` and are marked where they stand: the kernel
configuration that bounds growth, the unreferenced-function collection and
the `adjtime` divide. Every other sentence and figure stands as captured
and was not read again.

This note hands a measured work queue to an implementer who starts without
the session that produced it. Each item names the source location, the
measured or estimated payoff, the hazard that must be cleared first and the
acceptance command. The ranked technique survey it draws on is
`footprint-techniques.md`; the compiler and linker flag analysis is
`constrained-c.md` section 2. Alignment, the linked RAM map, flash recovery
evidence and the BSD comparisons are in `rp2040-optimization/README.md`; the
disposition of PR #435's mechanisms is `kernel-footprint-pr435-closeout.md`.

## Baseline

The measurements use GCC 16.2.0 (`arm-none-eabi-gcc`) at `-Os`. Text
columns are `arm-none-eabi-size unix.elf` text bytes. Headroom comes from
`tools/check_kernel_headroom.py`, which measures the flash image against the
FLASH region of `sys/arch/rp2040/conf/RP2040.ld` (130,816 bytes, `128K - 256`
after BOOT2). That image includes the `.data` and `.ramfunc` initializers, so
headroom is not the FLASH length minus text. Run `check-kernel-headroom` to
re-measure; it enforces a 1,024-byte margin.

| Kernel | Text at `origin/main` 694b91ef | Text after PR #422 (93ccc5da) | Flash image after #422 | Headroom after #422 |
|---|---|---|---|---|
| PICO | 95,519 | 95,120 | 95,120 | 35,696 |
| PICO_UART | 86,530 | 86,163 | 86,108 | 44,708 |
| PICOCALC | 113,087 | 112,704 | 112,824 | 17,992 |
| PICOCALC_PSRAM | not measured | 114,774 | 114,944 | 15,872 |

PICOCALC_PSRAM had the least headroom at this baseline. That configuration
has since been retired: `share/mk/architecture.mk` lists the six maintained
kernels (PICO, PICO_DIAG, PICO_UART, PICOCALC, PICOCALC_LEAN,
PICOCALC_LEAN_DIAG), PICOCALC carries the PSRAM driver and has the least
headroom among them, and `sys/arch/rp2040/doc/CAPACITY.md` holds the current
figures. PICOCALC therefore bounds any change that grows shared kernel code.

PICO data is 256 bytes and bss 39,832, which includes the 16,384-byte
`.swapram_pool` and the 8,192-byte `.scratch` (`usb_tx_ring`). The largest
other bss objects are `bufdata` 4,096, `proc` 2,600, `inode` 2,016, `mount`
1,036 and `cfree` 1,024.

The experiments on rows marked "scratch" ran on a `git archive` export of
eb5b3081, whose PICO baseline was 95,516. Deltas carry across; absolute
values do not.

## Scoped alignment sorting and ls formatting

The comparison base is `793d3432`. Both RP2040 distributions used ARM GCC
15.2.1 and GNU ld 2.45.1. Values below are candidate minus base in bytes.
Flash payload sums file-backed flash `PT_LOAD` bytes. Flash span includes
holes between segments. The end of resident kernel RAM moved by the same
amount as ordinary BSS in each configuration.

| Configuration | Flash payload | Flash span | Ordinary BSS |
| --- | ---: | ---: | ---: |
| PICO | -15 | 0 | -168 |
| PICO_DIAG | -14 | 0 | -64 |
| PICO_UART | -8 | -256 | -64 |
| PICOCALC | -17 | 0 | -40 |
| PICOCALC_LEAN | -18 | 0 | -48 |
| PICOCALC_LEAN_DIAG | -17 | 0 | -32 |

`sys/arch/rp2040/conf/kern.ldscript` sorts the remaining `.rodata*` and
`.bss*` inputs from largest alignment to smallest. Every input retains its
required alignment. The 8-byte output boundaries, named hot block and
linked executable symbol addresses are preserved. The SwapRAM pool retains
its capacity and follows the smaller BSS. PICO_UART crosses a 256-byte board
descriptor boundary. The other five builds finish in their original flash
span.

Sorting BSS alone gave the same RAM savings and left every flash span
unchanged. Also sorting text saved 64 to 105 total flash payload bytes
across the six links, with the same RAM and flash-span deltas as the
adopted sort. The global `--sort-section=alignment` matched that wider
sort. Adding initialized data to the sort yielded no further reduction.
Keep instruction layout and the 8-byte boundaries until a separate
experiment establishes a payoff and checks the affected consumers.

The `ls` permission renderer translates three groups of permission and
special bits directly to nine characters. The two account caches retain
64 direct-mapped slots each and store nine display bytes plus a terminator.
A hit skips the relevant name lookup. An absent account leaves the
previous slot intact and uses the existing numeric fallback. The change
removes the dependency on the unrelated `utmp` login-name layout. The
renderer and cache functions gained C17 prototypes in this stage.

| Executable | Raw file | Packed file | Text | Data | BSS | Static RAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standalone `ls` | -212 | -73 | +16 | -228 | -560 | -772 |
| Shipped `/bin/box` | -204 | -46 | +24 | -228 | -552 | -756 |

Static RAM sums the a.out text, data and BSS fields. It excludes stack
and heap use. The RP2040 manifest resolves `/bin/ls` to packed
`/bin/box`, which occupies 28 filesystem blocks in both images. The full
root retains 218 free UFS blocks and the same 772 live Dhara sectors at
image creation. Build both revisions with
`logged-distribution`, measure the binaries with `hsaout -s` and check
each root with `fsutil --check --partition=1`.
`check-ls-format` exercises the changed functions under strict C17 with
12,288 permission cases and account-cache boundary cases. It rejects
mutations to the sticky-bit rule and the cache width.

The UFS layout probe moved the embedded superblock to the start of
`struct mount`. The C17 initial-member rule then supplies the containing
pointer at the recovery sites in `ufs_mount.c` and `ufs_syscalls2.c`.
The probe kept RAM usage constant and grew the six linked text sections
by 24 to 144 bytes. PICO and PICO_DIAG each grew by 256 bytes of flash
span. Moving `m_dev` beyond Thumb-1's 124-byte immediate word-load reach
added an addressing cost. Retain the current layout. Measure the owner
lookup separately from the member order when revisiting that boundary.

## C17 ls migration and packed allocation

Both `ls` translation units compile in strict C17 with fatal prototype
diagnostics. The prototype migration uses the installed `stat`, `lstat`
and `free` declarations. A single file-flag header checks the parser
at each caller. `xinstall` supplies an `unsigned` destination for the
parser's `unsigned` write.

The formatter appends inode, block and long-format columns directly to
the listing buffer. It removes three static intermediate buffers that
held 280 bytes. The measurements use ARM GCC 15.2.1 and GNU ld 2.45.1.
The migrated objects match byte for byte under strict C17 and GNU17
after stripping debug information.

| Build stage | Standalone raw bytes | Standalone packed bytes | Shipped box raw bytes | Shipped box packed bytes | Packed box blocks | Box static RAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `0279974e` | 15,192 | 13,405 | 31,476 | 26,678 | 28 | 36,640 |
| C17 prototypes | 15,184 | 13,392 | 31,468 | 26,681 | 28 | 36,632 |
| Direct formatting | 15,072 | 13,300 | 31,348 | 26,588 | 27 | 36,232 |

The 27-block boundary is 26,624 bytes of payload. The allocation
includes one indirect block. The installed full root moves from 761
to 760 used UFS blocks, with 219 free. `flashimg` records 771 live
Dhara sectors at image creation, down from 772. Replay with
`logged-distribution`,
`tools/bin/rp2040/hsaout -s bin/ls/ls distrib/obj/destdir.rp2040/bin/box`,
and `tools/bin/rp2040/fsutil --check --partition=1 distrib/rp2040/sdcard.img`.

## Compact syscall dispatch prototype

Build the six ordinary kernels first and wait for the build to finish.
With Capstone, pyelftools and Unicorn installed in the selected interpreter, run
`${PYTHON} tools/rp2040_compact_dispatch_probe.py /tmp/compact-dispatch`
with a fresh output directory. The probe relinks private images using
the configured kernel objects and flags. It recompiles the syscall
entry with a halfword decoder and substitutes the compact table at the
current hot-block location. The normal build uses the pointer table.

The linker checks each selected target's alignment and range. It
supplies an odd base address so the decoder restores the Thumb bit
with the same addition that recovers the code address. The probe
matches each of the 163 handlers and argument counts against the
syscall master in both links. A Cortex-M0 instruction run starts
after the successful `setjmp` return and stops at the indirect call
target. A wrong-target mutation fails this check in each build.

| Kernel | Pointer-table flash span | Halfword-table flash span |
| --- | ---: | ---: |
| PICO | 98,624 | 98,368 |
| PICO_DIAG | 101,184 | 100,928 |
| PICO_UART | 90,424 | 90,168 |
| PICOCALC | 126,608 | 126,352 |
| PICOCALC_LEAN | 120,464 | 120,208 |
| PICOCALC_LEAN_DIAG | 123,024 | 122,768 |

The table occupies 326 bytes instead of 652. Each flash span decreases
by 256 bytes. Initialized data and BSS widths match in all six links.
The measured dispatch path grows from seven to ten instructions with
one extra flash literal load. Timing depends on XIP cache state and
bus behavior. Measure target cycles before enabling compact dispatch
in a shipping build.

## Shared base for interval-timer state

`kern_clock.c` and `kern_time.c` access the monotonic seconds and tick
together with the subsecond expiry bound. Three fields occupy one
eight-byte BSS record on ARM32. The initialized deadline seconds keeps
the `LONG_MAX` sentinel in its own word. The timer keeps its strict
past-deadline comparison, carries ticks at `hz`, and stores the expiry
bound under the existing clock interrupt exclusion.

Matched RP2040 links against `0a211d15` use GCC 15.2.1 and GNU ld
2.45.1. Negative values are candidate minus base, in bytes.

| Kernel | Flash payload | Flash span | End of resident kernel RAM |
| --- | ---: | ---: | ---: |
| PICO | -40 | 0 | 0 |
| PICO_DIAG | -40 | 0 | 0 |
| PICO_UART | -48 | 0 | 0 |
| PICOCALC | -48 | 0 | 0 |
| PICOCALC_LEAN | -56 | -256 | 0 |
| PICOCALC_LEAN_DIAG | -56 | 0 | 0 |

The payload is the sum of file-backed flash load segments. The span
runs from boot2 to the end of the last such segment. The lean link
crosses one 256-byte boundary at the board descriptor. A four-field
record moved eight bytes into initialized data and increased the
PicoCalc RAM extent by 24 bytes. A seconds-and-ticks pair alone
reduced payload by 32 bytes per kernel and added eight BSS bytes in
PICOCALC. The three-field record gives the smaller linked footprint
where the two constraints compete.

The C17 inventory also retires the two identifier-list definitions
guarded by `NOT_CURRENTLY_IN_USE` in `kern_time.c`. The exact PICO
compile flags with that guard defined reject the parent and accept
the typed functions. `check-storage-correctness` exercises the active
timer and wall-clock paths and their mutation controls. Silicon
interrupt timing remains unmeasured.

## Queue, ordered by payoff over risk

### Hot `struct user` fields within Thumb-1 load reach (measured: -2,040 to -2,056 bytes)

A Thumb-1 `ldr rX, [rY, #imm]` reaches 124 bytes. `sys/sys/user.h` places
`u_comm[MAXCOMLEN + 1]` (64 bytes) and the three 40-byte `label_t` saves
(`u_qsave`, `u_rsave`, `u_ssave`) beside `u_psargs`, immediately before
`u_stack`, so `u_arg` starts at offset 8 and the last credential word,
`u_groups[NGROUPS - 1]`, sits at 120. `sizeof(struct user)` stays 1,024
and `u_stack` stays last, because the kernel stack grows down from
`u + USIZE` toward it.

- Static assertions in `user.h` hold `u_procp` at offset 0 (locore.S
  reads it as `[r2, #0]`) and, on Thumb-1 targets only, `u_frame`,
  `u_arg[5]`, `u_rval`, `u_error` and every credential word at or below
  124. Header mutants that push `u_groups[NGROUPS - 1]` or `u_error` out
  of reach, or put `u_frame` first, fail the Cortex-M0+ compile; the
  Cortex-M4 compile rejects only the last.
- Layout readers inventoried: locore.S reads only `u_procp`; swap, fork,
  exec, core dumps and `proc_uarea()` move the u area as whole `USIZE`
  or `DEV_BSIZE` (1,024) blocks; ptrace and the host tests under
  `tests/kernel` address fields by name or computed offset.
- Text with arm-none-eabi-gcc 14.2.1, before and after (data, bss and free
  RAM unchanged): PICO 102,451 to 100,403; PICO_UART 91,913 to 89,857;
  PICOCALC 122,795 to 120,747; PICOCALC_PSRAM 125,315 to 123,275. Pool
  words that address the u area fell from 380 to 252 in PICO. The 36 words
  still beyond reach spread over 15 fields, the most being `u_qsave` (7,
  addresses passed to `setjmp()`) and `u_ofile` (6).
- `struct mount` (1,036 bytes, containing a 1,024-byte `struct fs`) is the
  next candidate for the same analysis.

### Unreferenced kernel functions (scratch: up to -2,463 bytes)

The flags have landed: PR #488 (`395804de`) compiles the kernel with
`-ffunction-sections -fdata-sections` and links it with `--gc-sections`
(`sys/arch/rp2040/conf/Makefile.rp2040`), so the functions below leave every
linked kernel that does not reference them. What remains open is deleting
the unreferenced source, under the intersection rule of the first bullet;
PICOCALC_PSRAM is no longer one of the configurations to intersect. The
scratch measurement that preceded the flags follows as captured.

A `-ffunction-sections -fdata-sections` compile with a `--gc-sections` link
of PICO dropped 67 sections. Among them: `malloc3`, `malloc3_contiguous`,
`physio`, `rawrw`, `disksort`, `dk_alloc`, `dhara_map_trim`,
`dhara_map_clear`, `dhara_map_copy_page`, `dhara_map_copy_sector`,
`dhara_journal_clear`, `dhara_strerror`, `sysctl_long`, `sysctl_rdlong`,
`sysctl_struct`, `tprintf`, `puts`, `insque`, `remque`, `badkaddr`,
`vn_open`, `vn_close`, `swapram_out`, `swapram_admit`,
`swapram_pool_largest`, `uwritec`, `uiofmove`, `q_to_b`, `ndqb`,
`ndflush`, `ttrstrt`, `nullmodem`, `ttylclose`, `ttymodem`,
`swap_image_cancel`, `exec_maxhdrsz` and `dk_n`.

- A function unreferenced on PICO may be referenced on PICO_UART, PICOCALC,
  PICOCALC_PSRAM, STM32, or a host test. Link each configuration with
  `--print-gc-sections` and take the intersection before deleting.
- Vendored Dhara and heatshrink sources under `sys/arch/rp2040/dhara` and
  `sys/arch/rp2040/heatshrink` stay unedited. Their dead functions leave
  through the linker flags, not through source deletion.
- The collector also dropped a 32-byte `.text` input section of
  `locore0.o`. Identify it before trusting a collected link.
- Landing the flags themselves is the alternative to deletion. Function
  tables (`cdevsw`, `execsw`, `syscall_handlers`) root every entry they
  name, so the flags cannot remove a reachable syscall.

### 64-bit divide in `adjtime` (estimate: about -480 bytes)

Done: commit `988101245` (2026-11-10) computes `adjtime` in 32 bits. At
main `bff303aae`, `arm-none-eabi-nm unix.elf` on each of the six kernels
lists none of `__aeabi_ldivmod`, `__aeabi_uldivmod`, `__divdi3`,
`__udivdi3`, `__muldi3`, `__aeabi_lmul`, `__gnu_ldivmod_helper` or
`__clzdi2` (GCC 16.2.0, a local `logged-build`). The analysis that led to
the change follows as captured.

`adjtime` is the only PICO caller of `__aeabi_ldivmod` and `__aeabi_lmul`.
Those calls pull `__divdi3` (304 bytes), `__aeabi_ldivmod`,
`__gnu_ldivmod_helper`, `__muldi3`, `__clzdi2` and the 64-bit shifts out of
libgcc. The input is not bounded: `adjtime()` (`sys/kern/kern_time.c`, from
line 118) accepts full-width `long` `tv_sec` and `tv_usec`. The `0x7fff` tick
threshold selects stepping the clock over slewing it and admits nothing;
the step path accepts any adjustment whose resulting `time.tv_sec` fits in
`long` and rejects the rest with `EINVAL`. The rewrite therefore keeps
large steps and the overflow rejection, using checked 32-bit arithmetic
(seconds and ticks carried separately) rather than truncating to the slew
range. Extend `tests/kernel/time_io_test.c` with old-against-new comparisons
at the slew/step boundary, at large positive and negative steps, and at the
`LONG_MIN`/`LONG_MAX` rejection edges. Confirm with `arm-none-eabi-nm unix.elf | grep divdi3` that the helper is
gone.

### `sched` and `exit` noreturn (small)

`sched()` (`sys/kern/vm_sched.c` line 45, declared `sys/sys/proc.h` line
189) loops forever. Kernel `exit()` (`sys/kern/kern_exit.c` line 55,
declared `proc.h` line 214) ends in `swtch()`, which never returns to a
zombie but returns to every other caller, so `exit` needs
`__builtin_unreachable()` after its `swtch()` before it can be `__dead`.
`rexit()` (line 157) then becomes `__dead` too. `idle()`
(`sys/arch/rp2040/rp2040/machdep.c` line 697) returns and is excluded.

### Lean PICO and a PICO_DIAG configuration (-1,304 bytes text, -96 bss)

The owner chose this split on 2026-11-10. `sys/arch/rp2040/compile/PICO/Config`
enables `SYSTRACE` (544 bytes of text) and `CAPACITY_STATS` (760 bytes of
text, 96 bytes of bss). `HALTREBOOT` costs 52 bytes and stays.

- Remove both options from PICO and add `compile/PICO_DIAG/Config` carrying
  them. The generated Makefiles are tracked, so generate and track
  PICO_DIAG's Makefile the way PICO and PICO_UART are.
- Repoint every reader of `kern.capacity` or `kern.systrace` at PICO_DIAG:
  `sys/arch/rp2040/doc/CAPACITY.md`, `sys/arch/rp2040/doc/TESTING.md`, the
  board capacity acceptance in `docs/research/board-capacity-acceptance.md`,
  and any tool under `tools/`. Run `grep -rn 'kern.capacity\|systrace'`
  first.
- Add the PICO_DIAG kernel to the root Makefile, the board-build tier of
  `logged-check` and CI.
- Confirm that `sysctl` and `adminbox` report an absent node on a lean
  kernel as an error rather than printing stale memory.
- `kern/subr_capacity.c` is selected by `optional capacity_stats` in
  `sys/arch/rp2040/conf/files.rp2040`. A measurement that removes the
  `-DCAPACITY_STATS` parameter by hand must also remove `subr_capacity.o`
  from `OBJS`, or the build fails on the stub macros in `sys/sys/capacity.h`.

### Resolved: `-O2` compile of the capacity source

`capacity_note()` in `sys/kern/subr_capacity.c` rejects `kind >=
CAPACITY_KIND_COUNT` before it forms an array element pointer, and the
`invalid-kind` mutant of `tests/kernel/capacity_mutate.py` removes that check
and must fail. With the check in place `subr_capacity.c` compiles at `-O2` under
the PICOCALC flags with `-Werror` (measured at `d81b1788`: 520 text bytes
against 480 at `-Os`). Whole-kernel `-O2` linking is not measured here; the
branch of PR #435 recorded PICOCALC exceeding FLASH at `-O2`
(`kernel-footprint-pr435-closeout.md`). `-Oz` produces the same text as `-Os` on
this target (scratch: 95,516 both), so `-Os` remains the size baseline.

### Never-returning libc entry points (measured: -1,566 packed bytes, one root block)

`abort`, `exit` and `_Exit` in `include/stdlib.h`, `_exit` in
`include/unistd.h`, `longjmp`, `_longjmp` and `siglongjmp` in
`include/setjmp.h`, and `err`, `errx`, `verr` and `verrx` in `include/err.h`
are declared `__dead`. A world build against `main` at 4e36f422 shrank 65 of
288 programs by 2,224 raw bytes and 1,566 packed bytes (`hsaout -s`), one
packed root block in total; 62 programs grew by up to 10 packed bytes each,
which `footprint-techniques.md` item 2 predicts, and `usr/sbin/update` grew
by 4 raw bytes. The largest raw savings were `forth` 228, `ed` 188, `basic`
180, `bc` 168 and `utilbox` 152 bytes.

- `sys/sys/cdefs.h` expands `__dead` to nothing when `__SMALLER_C__` is
  defined, before its C11 test: the Smaller C suite preprocesses through
  `arm-none-eabi-gcc -E`, which claims C11, and `smlrc` rejects
  `_Noreturn`.
- Each header falls back to `_Noreturn` when the `<sys/cdefs.h>` it reaches
  lacks `__dead`, as a host `<sys/cdefs.h>` does in the hosted header
  contracts.
- A test stand-in for one of these functions must itself not return;
  `tests/umount_contracts` declares its `longjmp`-ending exit replacement
  `__dead` for that reason.

### Three size-growing passes off in machine-independent code (measured: -604 to -620 bytes)

`CSIZEFLAGS` in `sys/arch/rp2040/conf/Makefile.rp2040` holds
`-fno-move-loop-invariants -fno-tree-loop-im -fno-tree-dominator-opts`, and
`CSIZEFLAGS_MI` passes them to each C source outside `sys/arch`. Text with
arm-none-eabi-gcc 16.2.0 at 6f000e92, before and after (data, bss and
`__kernel_ram_used_end` unchanged in all six):

| Kernel | Before | After | Change |
|---|---|---|---|
| PICO | 98,335 | 97,715 | -620 |
| PICO_DIAG | 100,877 | 100,257 | -620 |
| PICO_UART | 90,289 | 89,677 | -612 |
| PICOCALC | 123,399 | 122,795 | -604 |
| PICOCALC_LEAN | 117,405 | 116,801 | -604 |
| PICOCALC_LEAN_DIAG | 119,939 | 119,335 | -604 |

- The selection came from a sweep of 31 single `-fno-` flags over `-Os`
  and 12 combinations, read as PICO, PICO_UART, PICOCALC and PICOCALC_LEAN
  text. `-Oz` produced the same text as `-Os`. The largest single losses
  were `-fno-crossjumping` (+1,776 to +2,528), `-fno-toplevel-reorder`
  (+1,116 to +1,720) and `-fno-gcse` (+752 to +816). Adding
  `-fno-inline-small-functions -fno-partial-inlining -fno-if-conversion
  -fno-ipa-sra` saved a further 56 to 120 bytes and was left out as too
  small to carry four more flags.
- Applied to every source the three flags save 748 to 964 bytes, and they
  then change instructions in the `.ramfunc` flash routines and in
  `sd_cmd`, `spi_wait_idle`, `i2c_restart`, `kbd_step` and `usb_service`,
  whose loops poll hardware. The `sys/arch` exclusion gives up 136 to 360
  bytes to leave those unmeasured timings alone. Object identity shows it
  holds: between a `CSIZEFLAGS=` build and the default build of PICO,
  PICO_UART and PICOCALC, 44 of 270 objects differ in their text, read-only
  data, data or `.ramfunc` bytes, and the generated Makefile rules place all
  44 sources under `$S/kern`. The 90 objects built from `$S/arch` are
  identical.
- Stack frames from `-fstack-usage` in the affected sources: `closei` grows
  by 8 bytes in PICO and PICOCALC and `getfsstat` by 8 in PICO, `newproc` and `swtch` shrink by 16, and
  about 60 others shrink. The deepest call chain, walked over the PICO
  call graph at 793d3432 with the boot ROM's measured depths
  (`kernel-size-flags/README.md`, "Kernel stack chains"), falls 24 bytes
  with the adopted flags, from 2,416 to 2,392; 60 of 108 system call
  handlers get a shallower chain and one, `getfsstat`, a deeper one by 8.
  The same walk puts the deepest system call chain above the 2,048-byte
  kernel stack in every variant and in each of the six tracked
  configurations (2,392 to 2,424 bytes), a static bound under a declared
  model that no board run has confirmed or refuted.
- Hosted arm-none-eabi-gcc 13.2.1: the flash image `check-kernel-headroom`
  reports, which moves in 256-byte steps, falls 512 bytes in five kernels
  and 256 in PICO_UART between main's push run and the pull-request run.
  GCC 14 is not measured.
- Board timing, bare Pico, PICO configuration, two runs
  (`kernel-size-flags/README.md` holds the tables and transcripts): against
  plain `-Os` the adopted flags move system calls +0.8% and pipe +1.0% to
  +1.5% and leave context switch, spawn and execl within their spread. The
  three flags on every source cost context switch 23%, spawn 22% and pipe
  8%, so the `sys/arch` exclusion preserves time as well as driver bytes.
- A second sweep of 123 further flags outside `sys/arch` found 18 that
  shrink the kernel; the best five together save 344 to 368 bytes more,
  cost 8% of pipe throughput on the board and grow 12 to 14 stack frames,
  and were not adopted.
- Observed during the sweep, mechanism not examined: the first
  `bmake kernel` after an edit to `Makefile.rp2040` compiled with the
  previous template's flags and the second with the new ones. A size
  comparison across a template edit used the second build.

### Kernel LTO (unmeasured; four blockers)

An `-flto` link of PICO fails four ways:

1. `arm_fault` and `fault_regs` are referenced only from naked inline
   assembly in `sys/arch/rp2040/rp2040/fault.c`, so LTO discards them. Mark
   them `__attribute__((used))`.
2. `memcpy` is localized, and the calls GCC synthesizes for structure copies
   then fail to resolve.
3. `usb_tx_ring` fails the linker assertion that places it in the
   SRAM4/SRAM5 scratch banks.
4. A single partition overflows the Thumb-1 literal-pool reach ("invalid
   offset, value too big"). `-flto-partition=1to1` avoids that and exposes
   the first three.

Measure the payoff after the `struct user` and dead-function work, because
the three overlap.

### Further design candidates

Prototype each in a scratch tree and land only a measured win.

- **Anchored kernel scalars.** 891 literal words in PICO text point at 169
  distinct RAM addresses. Gathering hot scalar globals into one structure
  lets a function load one base literal and reach the rest by offset.
  Estimate: 1 to 1.5 KB, medium confidence.
- **errno returned from syscall handlers.** 267 sites assign `u.u_error`.
  Returning the value, as 4.4BSD does, pairs with typed argument
  structures in place of `u.u_arg` punning. Measure after the `struct user`
  reorder, which absorbs part of this.
- **Boot ROM memory routines.** `bcopy` (110 bytes), `bzero` (106) and
  `__clzsi2` (60) duplicate functions the boot ROM exports. The kernel calls
  `rom_func_lookup` once (`machdep.c` line 742). The overlapping-copy
  semantics of `bcopy` must be preserved.
- **Halfword device dispatch.** The
  [syscall prototype](#compact-syscall-dispatch-prototype) covers
  `syscall_handlers`. The separate `cdevsw` table mixes function pointers
  with the RAM data pointer `d_ttys` in `sys/sys/conf.h`. A device-dispatch
  trial can encode the function pointers as code offsets and retain
  `d_ttys` at pointer width.
- **Packed message pools.** `errno_message_pool` (1,902 bytes) and
  `syscall_name_pool` (1,193 bytes) are mostly lowercase text. Five-bit
  packing with a small decoder would cost CPU only on error paths.
  Estimate: about 1.1 KB.

## Method notes

- A scratch export needs `sys`, `share/mk` and `tools` from `git archive`,
  plus a copy of the built `tools/bin` host tools. The kernel Makefile has
  to be invoked with `all`, because the default target stops after
  `.params`.
- Flag variants go through `COPTS='...'` on the `bmake` command line, which
  overrides the `COPTS?=` default. Run `bmake clean` before every variant:
  the objects do not depend on `COPTS`, so a changed flag reuses stale
  objects and the size comparison reports the previous variant. Every
  measurement in this note ran from a clean compile directory.
- A collected link was made by hand:
  `arm-none-eabi-ld -T ../../conf/RP2040.ld -T ../../conf/kern.ldscript -nostdlib -z noexecstack boot2.o locore0.o locore.o <other objects> --gc-sections <libgcc>`.
  The production recipe also passes `--fatal-warnings` and a map file.
- `logged-check` output contains `FAIL` lines from negative calibration
  gates, which pass by failing. Read the final `[logged-gate] completed ...
  exit=` line for the verdict.
- After editing any C source or header, refresh
  `tools/c17/source-denominator.json` with
  `$PYTHON tools/c17/source_denominator.py --refresh-classification`
  before running `logged-check`.
- A host stand-in for a kernel function declared `__dead` must not return.
  `tests/kernel/hostkern.h` provides `hk_escape()` and `hk_catch()` for
  that (PR #422).
- Every `gh` call in this repository needs
  `--repo Oichkatzelesfrettschen/discobsd-2040-unofficial`, because the
  default resolves to the upstream repository. Request a Copilot review with
  `gh pr edit N --repo ... --add-reviewer @copilot`; the REST
  `requested_reviewers` endpoint accepts the bot login silently and records
  nothing.
- A force-push is refused by the session permission policy. Corrections to
  a pushed branch land as further commits.

## Open measurements

The scoped alignment sort and `ls` changes have no physical-board
timing, peak-memory or flash endurance measurements. Establish those
measurements before using static footprint as evidence for those effects.
