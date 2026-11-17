# Kernel size flags: sweeps and board timings

The inputs behind the `CSIZEFLAGS` entry of `../kernel-footprint-handoff.md`:
two flag sweeps over the kernel build and two timing runs on one bare
Raspberry Pi Pico. All builds use arm-none-eabi-gcc 16.2.0 on the tree of
pull request #670 (cfc76aa9, based on 6f000e92).

## Sweeps

| File | Scope | Columns |
|---|---|---|
| `sweep-every-source.txt` | one `COPTS` value per row, applied to every kernel C source | exit status, then text bytes of PICO, PICO_UART, PICOCALC and PICOCALC_LEAN |
| `sweep-outside-arch.txt` | one flag per row added to the three `CSIZEFLAGS` flags, applied to C sources outside `sys/arch` | exit status, then text plus data bytes of the same four kernels; the first row is the three flags alone |

The second sweep holds 123 additions. 18 shrink the kernel, 51 leave it
byte-for-byte the same size, 53 grow it and one (`-mslow-flash-data`) does
not compile. Its combination rows give differences from the first row; the
best, `-fno-tree-ter -fno-tree-dce --param=uninlined-function-insns=0
-fno-inline-small-functions -fno-ipa-modref`, saves a further 344 to 368
bytes and is the `ext` kernel below.

## Kernels timed

All are the PICO configuration. Data is 272 bytes and bss 42,936 in each.

| Name | Flags beyond `-Os` | Text | Against `base` |
|---|---|---|---|
| `base` | none (`CSIZEFLAGS=`) | 98,335 | |
| `mi` | the three `CSIZEFLAGS` flags outside `sys/arch` (pull request #670) | 97,715 | -620 |
| `all` | the three flags on every C source | 97,579 | -756 |
| `ext` | `mi` plus the five flags above, outside `sys/arch` | 97,355 | -980 |
| `sel` | `mi` plus `-O2` on the nine swap-path sources | 100,395 | +2,060 |

The swap-path sources are `heatshrink_encoder.c`, `hsx_decoder.c`,
`hsx_stream.c`, `swapram.c`, `swapram_pool.c`, `flash_swap.c`,
`vm_swap.c`, `vm_swp.c` and `swap.c`.

## Board runs

One bare Pico (RP2040 B2, 125 MHz, W25Q16 flash), 2026-11-17, 03:00Z to
03:17Z and 03:17Z to 03:35Z. Each run saved the whole flash, loaded a root
image holding the UnixBench and CoreMark programs (built earlier with GCC
14.2.1; SHA-256
a28199bf641c108866413a814a27ad7c1ac31670fcf7f8d15ed2df1ff538231f), flashed
one kernel per pass, ran the commands each file shows and restored the saved
flash, which `picotool verify` confirmed. `board/run1` and `board/run2`
hold one console transcript per pass, named kernel-pass, with escape
sequences, prompts and trailing blanks removed. Pass order was base, mi,
all, sel, sel, all, mi, base in run 1 and ext, mi, base, base, mi, ext,
ext, mi in run 2.

A UnixBench rate is the `loops` line divided by the 10-second run; CoreMark
is its `Iterations/Sec` line. Means, per second, higher is better:

| Test | base | mi | all | sel | | base (run 2) | mi (run 2) | ext |
|---|---|---|---|---|---|---|---|---|
| ub-syscall | 38,658.9 | 38,974.2 | 38,975.6 | 38,972.3 | | 38,659.8 | 38,974.1 | 38,974.3 |
| ub-pipe | 24,148.4 | 24,378.1 | 22,288.4 | 24,631.0 | | 24,026.5 | 24,376.7 | 22,078.0 |
| ub-context1 | 3.50 | 3.50 | 2.70 | 5.30 | | 3.50 | 3.47 | 3.50 |
| ub-spawn | 3.35 | 3.30 | 2.60 | 5.15 | | 3.30 | 3.30 | 3.33 |
| ub-execl | 51.40 | 50.50 | 46.15 | 43.60 | | 49.90 | 50.03 | 49.43 |
| cm-O2u | 248.26 | 247.24 | 245.36 | 249.97 | | 247.41 | 248.30 | 248.82 |

Readings, each scoped to this board, this compiler and the PICO
configuration:

- `mi` against `base`: system calls +0.8%, where passes of one kernel
  agree to 0.01%. Pipe +1.0% in run 1, where the two `base` passes span
  1.0%, and +1.5% in run 2, where they span 0.03%. Context switch, spawn
  and execl sit within their spread. No test slows.
- `all`: context switch -23%, spawn -22%, pipe -8%, execl -10%. The loss
  comes from the `sys/arch` sources, which hold the swap codec; the
  exclusion that keeps drivers byte-identical also keeps this time.
- `ext`: pipe -8% against `base` in three passes of three, for 360 bytes.
  Its stack frames also grow in 12 to 14 functions, among them `copen`,
  `open`, `prf` (8 bytes each) and `q_to_b` (32). Not adopted.
- `sel`: context switch +51%, spawn +54%, pipe +2%, execl -15% in both
  passes, for 2,680 bytes over `mi`. It trades text for time and is not
  part of #670.
- CoreMark runs in user mode from SRAM; its 0.5% spread across kernels is
  the same size as its spread across passes of one kernel.

## Kernel stack chains

`stack_chain.py` bounds the kernel stack of one linked kernel from the
call graph GCC writes with `-fcallgraph-info=su`, and `stack-chain.sh`
runs it over four of the kernels above. The stack is the 2,048 bytes of
the u area above `struct user`; a chain that passes them writes into
`struct user`. The builds are PICO at f48e19c9 with arm-none-eabi-gcc
16.2.0, a later tree than the timed kernels, so text differs from the
table above.

| | base | mi | all | ext |
|---|---|---|---|---|
| Text | 98,795 | 98,175 | 98,047 | 97,799 |
| Deepest system call chain | 2,400 | 2,376 | 2,372 | 2,376 |
| Deepest boot chain (`main`) | 1,728 | 1,704 | 1,700 | 1,712 |
| Deepest user fault chain | 1,992 | 1,968 | 1,964 | 1,960 |
| Clock interrupt, with its exception frame | 292 | 292 | 292 | 292 |
| Terminal interrupt, with its exception frame | 444 | 444 | 444 | 444 |
| Probe interrupt (NMI), its exception frame | 36 | 36 | 36 | 36 |
| Sum | 3,172 | 3,148 | 3,144 | 3,148 |
| Against 2,048 | -1,124 | -1,100 | -1,096 | -1,100 |
| `panic`, from its own entry | 1,280 | 1,272 | 1,268 | 1,272 |

`stack/<variant>.txt` holds each root's path and `stack/syscalls.tsv`
the deepest chain under each of the 108 system call handlers, and
`stack/frames.tsv` each linked function's frame. Text is the same with
and without the call-graph options (98,175 for `mi`).

### What the flags change

The flag result is the difference between columns, which the model's
choices cancel out of:

- `mi` lowers the deepest chain by 24 bytes and the sum by 24. Under the
  108 handlers it lowers 60 chains, by 40 bytes at most and 10 on average,
  leaves 47 and raises one, `getfsstat`, by 8. Among linked functions 41
  frames shrink and 2 grow (`closei` and `getfsstat`, 8 bytes each), 320
  bytes net.
- `all` lowers 79 handler chains, by 44 at most, and raises `getfsstat`
  by 4; against `base` 63 frames shrink and 4 grow, 476 bytes net.
- `ext` matches `mi` on the deepest chain and raises three handler
  chains against `base` (`getfsstat`, `settimeofday`, `sigstack`, 8 bytes
  each). Against `mi` 15 frames grow, among them `copen`, `open` and
  `prf` by 8 each, and 12 shrink, 20 bytes net; ten functions exist under
  one name in only one of the two builds and are not compared (`q_to_b`,
  whole in `ext` and split into `q_to_b.part.0` elsewhere, is one). Its
  further 376 bytes of text buy no stack.

Three of these figures repeat ones `kernel-footprint-handoff.md` took by
hand from the `.su` files of an earlier tree, which checks the parser
against an independent reading: `closei` and `getfsstat` grow by 8 under
`mi`, `newproc` and `swtch` shrink by 16, and `copen`, `open` and `prf`
grow by 8 under `ext`. The deepest clock chain (256) and the deepest
terminal chain (USB, 408) are the same in every variant; the UART chains
fall from 400 to 392 under `all`.

### What the bound says about the stack

Every variant's sum passes the budget by more than 1,000 bytes, and the
deepest system call chain passes it alone, before any interrupt:

    PendSV_Handler 88, syscall 32, execv 8, execve 400, exec_check 40,
    exec_script_check 120, exec_check 40, exec_hsaout_check 120,
    exec_clear 24, closef .. device_driver_call 144, cnclose .. ttywait 48,
    sleep 8, tsleep 40, issignal 16, procxmt 24, exit 32,
    closef .. irele 40, itrunc 144, indirtrunc 144 (three levels of 48),
    free .. bwrite 80, flstrategy 56, dhara_map_write .. bcopy 728

It is an `execve` of a script whose close-on-exec terminal waits for its
output to drain, where a tracing parent's kill ends the process inside
that wait and `exit` then closes the last reference to an unlinked file
with indirect blocks while Dhara collects garbage. Each step is a branch
the source has; whether one run can take all of them is not shown. Three
handlers pass 1,928 bytes, the budget less the 120 bytes of
`PendSV_Handler` and `syscall`, on their own: `execv`, `execve` and
`smount`. 32 more reach 1,536 bytes or more, where the entry and one
terminal interrupt (444) carry them over.

Two parts recur. The flash write path from `flstrategy` down costs 784
bytes and ends every deep chain, 352 of them in the `raw_gc` and
`trace_path` frames of the vendored Dhara. `execve` holds 400 bytes and
`namei_at` 368.

The bound is a static result under a model. The retained board watermark
(`sys/arch/rp2040/doc/CAPACITY.md`) is a different kind of evidence: 1,828
of 2,048 bytes touched in one stdio workload. A watermark above the bound
for a path the model covers would refute the model; a watermark below it
does not show the bound is loose, because that workload drives none of
the chain above. The deciding measurement is a PICO_DIAG board run that
drives the chain (a traced `execve` of a script, killed during the
terminal drain, with a large unlinked file open and the root near its
garbage-collection threshold) and reads `kern.capacity` afterwards.

### The model

`stack-model.json` declares what the call graph cannot say. The tool
refuses a build the model does not cover: an indirect call site that
matches no rule, a rule or guard that matches nothing, a call cycle, a
function with a dynamic frame, and a linked function whose address an
object file stores while no rule, root or exemption names it. That last
check is what catches a callback registered in a file the model's author
never read. `stack_chain.py --self-test` runs the analysis over synthetic
graphs with hand-computed depths and requires 13 malformed inputs to be
refused.

- **Roots.** `PendSV_Handler` (system calls), `main`, `HardFault_Handler`
  (a user fault that posts a signal) and `SVC_Handler` are the base
  chains; `systick` at the clock level and the USB and UART handlers at
  the terminal level and the core 1 probe's `NMI_Handler` are added on
  top, each with a 36-byte exception frame, whether or not the deepest
  point masks them. `panic` and `boot` are reported alone.
- **Indirect calls.** 76 site rules. 49 read their targets from the linked image
  (`cdevsw`, `bdevsw`, `Fops`, `execsw`, `syscall_handlers`,
  `conf_service_init`, `flswap_ops`) through DWARF member offsets; 9 name
  functions assigned at run time (callouts, `t_oproc`, driver probes,
  sysctl subtrees, the core ring); 16 name a call left out, listed below;
  2 count frames only.
- **Cycles.** Five bounded recursions (`wakeup`, `psignal`, `mfree` two
  deep, `indirtrunc` three, `errno_message_expand` five) and three
  functions whose second entry returns before any call (`ttypend`, `exit`,
  `exec_script_check`), each with its reason in the file.
- **Guards.** 40 calls whose arguments rule out part of the callee:
  23 callers that sleep only at `PZERO` or below and so never test a
  signal (the tool checks the priority in the source text of every such
  call, in every variant), the kernel's own 16 `rdwri` callers, whose
  inode is a regular file, a directory or a symbolic link and never a
  character device (the nine that only read are also kept from the
  truncation and synchronous-write arms, and the tool checks `UIO_READ`
  at each of their calls), and a pipe's release, which has no indirect
  block.

### What the bound leaves out

- **Boot ROM routines.** The flash erase, program and XIP calls and
  `rom_func_lookup` run ROM code whose stack use is unmeasured. They sit
  under the flash write path, beside the deepest chain.
- **`panic` on top of a chain.** A panic adds up to 1,272 bytes to whatever
  raised it; the sum is not bounded here.
- **The swap pseudo-device through the buffer cache.** `bwrite`, `bread`
  and `breada` reach `swstrategy` only for I/O on the swap device's block
  node, which nests the buffer cache under itself up to `NBUF` deep.
- **`ucall`.** It calls a caller-supplied kernel address at securelevel 0.
- **Terminal modem ioctls.** `ttyblock` and `ttyunblock` count the
  driver's and `ttioctl`'s frames and none of their other calls.
- **Terminal drivers by name.** 16 `cdevsw` sites whose device is a
  terminal keep only table entries named `usb`, `uart`, `lcd`, `pt`,
  `null` or `no`. A terminal driver under another prefix would be dropped
  there without a refusal.
- **Core 1.** Its functions run on `core1_stack`.
- **Other configurations.** PICO only. PICO_UART, the PicoCalc kernels and
  PICO_DIAG link different drivers; the model drops targets a kernel does
  not link, and it has been run on none of them.
