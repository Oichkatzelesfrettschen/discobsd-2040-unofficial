# Kernel and libc footprint work queue

This note hands a measured work queue to an implementer who starts without
the session that produced it. Each item names the source location, the
measured or estimated payoff, the hazard that must be cleared first and the
acceptance command. The ranked technique survey it draws on is
`footprint-techniques.md`; the compiler and linker flag analysis is
`constrained-c.md` section 2.

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

PICOCALC_PSRAM has the least headroom, so it bounds any change that grows
shared kernel code.

PICO data is 256 bytes and bss 39,832, which includes the 16,384-byte
`.swapram_pool` and the 8,192-byte `.scratch` (`usb_tx_ring`). The largest
other bss objects are `bufdata` 4,096, `proc` 2,600, `inode` 2,016, `mount`
1,036 and `cfree` 1,024.

The experiments on rows marked "scratch" ran on a `git archive` export of
eb5b3081, whose PICO baseline was 95,516. Deltas carry across; absolute
values do not.

## Queue, ordered by payoff over risk

### Hot `struct user` fields within Thumb-1 load reach (scratch: -2,008 bytes)

`sys/sys/user.h` places `u_comm[MAXCOMLEN + 1]` (64 bytes) and three 40-byte
`label_t` saves (`u_qsave`, `u_rsave`, `u_ssave`) at offsets 8 to 191. A
Thumb-1 `ldr rX, [rY, #imm]` reaches 124 bytes, so `u_arg` (offset 192),
`u_rval` (216) and `u_error` (220) each need a 4-byte literal. The PICO
disassembly holds 164 literals for `&u`, 60 for `&u.u_arg`, 46 for
`&u.u_error` and 20 for `&u.u_rval`.

- The scratch experiment moved the four cold fields after `u_stack[1]`.
  That placement measures the payoff but is wrong for landing, because
  `u_stack` must remain last: the kernel stack grows down from
  `u + USIZE` toward it. Place the cold fields immediately before
  `u_stack` instead.
- Inventory every reader of the layout before editing: ptrace
  (`sys/kern/sys_process.c`), core dumps, sysctl export, any locore or
  assembly offset constant, swap images of the u-area, `subr_capacity.c`
  (`UAREA_STACK_OFFSET`), and host tests under `tests/kernel`.
- Add `_Static_assert(offsetof(struct user, F) <= 124, ...)` for each hot
  field (`u_procp`, `u_frame`, `u_arg`, `u_rval`, `u_error`, `u_uid` and
  the credentials), so a later field insertion that breaks reach fails the
  build. Calibrate the assertion by moving one field past 124 once.
- `struct mount` (1,036 bytes, containing a 1,024-byte `struct fs`) is the
  next candidate for the same analysis.

### Unreferenced kernel functions (scratch: up to -2,463 bytes)

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

### `-O2` build blocked by an unadmitted capacity index

`COPTS=-O2` fails with `-Werror=array-bounds` at
`sys/kern/subr_capacity.c` line 79: `capacity_state.metric[kind]` with
`kind` 5 of a 5-element array. `capacity_note()` indexes the array without
checking `kind < CAPACITY_KIND_COUNT`, and `capacity_live()`'s switch carries
a `CAPACITY_KIND_COUNT` arm that keeps the value live in the compiler's
range analysis. All 17 callers pass valid kinds, so no current path reaches
it. Add the admission in `capacity_note()`, then build all four RP2040
configurations at `-O2` to find any further diagnostics. This unblocks
every speed-for-size comparison; `-Oz` produces the same text as `-Os` on
this target (scratch: 95,516 both), so `-Os` remains the size baseline.

### Never-returning libc entry points (payoff unmeasured)

`include/stdlib.h` (`abort` line 60, `exit` line 67), `include/unistd.h`
(`_exit` line 79), `include/setjmp.h` (`_longjmp` line 14, `longjmp` line 20)
and `include/err.h` (`err`, `errx`, `verr`, `verrx`, lines 19 to 24) declare
functions that never return as plain `void`. Userland source holds 1,101
`exit`, 53 `_exit`, 25 `abort`, 50 `err` and 24 `errx` call sites. Those are
source counts; the code-generation payoff is unmeasured and smaller than the
counts suggest.

- Userland compiles hosted (`share/mk/sys.mk` passes neither
  `-ffreestanding` nor `-fno-builtin`), and GCC already treats `exit`,
  `_exit` and `abort` as non-returning library builtins there. The
  candidates that remain are the `err` family and `longjmp`/`_longjmp` in
  userland, and every one of these calls inside libc, which
  `lib/libc/Makefile` compiles with `-ffreestanding`.
- A single-file probe cannot decide the payoff. Six variants (plain and
  `_Noreturn` declarations, each hosted, `-ffreestanding` and
  `-fno-builtin`) produced identical function sizes, because the probe's call
  sites were tail-call shaped and the known-good `_Noreturn` control moved
  nothing. The kernel measurement for `panic` and `boot` (-399 bytes on
  PICO, built `-ffreestanding -fno-builtin`) came from a whole build. The
  decisive measurement is a world build compared in packed blocks.

- Mark each declaration `__dead` (`sys/sys/cdefs.h` maps it to `_Noreturn`
  under C11, the GNU attribute otherwise, and nothing for other compilers).
- Each libc definition must end in a call the compiler sees as
  non-returning, or `-Werror` reports `'noreturn' function does return`.
  `_exit` is the assembly stub `lib/libc/arm/sys/_exit.S` (`svc #SYS_exit`),
  so only its declaration changes.
- The native Smaller C compiler reads these headers. Confirm that `__dead`
  expands to nothing there, or that Smaller C accepts `_Noreturn`
  (`c17-semantic-migration-plan.md`, "Native Smaller C rule").
- Measure by packed root blocks (`tools/bin/rp2040/hsaout -s`) across the
  world build, not by raw text: `footprint-techniques.md` item 2 records
  that packed size is not monotone in code size.

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

### Designs not yet prototyped

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
- **Halfword dispatch tables.** `syscall_handlers` (652 bytes) stores
  4-byte pointers into text under 128 KB. `cdevsw` (484 bytes) mixes
  function pointers with the RAM data pointer `d_ttys` (`struct cdevsw` in
  `sys/sys/conf.h`, populated with tables such as `cnttys`), which cannot take
  a code-offset encoding. A prototype compresses only the function-pointer
  members and keeps data pointers full width. C cannot express halfword code
  offsets as constant initializers, so a generator would emit them in
  assembly.
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
  overrides the `COPTS?=` default.
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

## Not measured

No item in this queue has been booted on a board or in Renode. Packed-block
effects on the root filesystem are unmeasured for every userland item.
