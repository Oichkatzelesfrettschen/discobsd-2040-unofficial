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
runs it over PICO under the four flag variants above and over the five
other tracked configurations under the tree's default flags. The stack is
the 2,048 bytes of the u area above `struct user`; a chain that passes
them writes into `struct user`. The tool holds that budget to each image:
the span from `u` to `_estack` less the DWARF size of `struct user`. The
builds are at 793d3432 with arm-none-eabi-gcc 16.2.0, a later tree than
the timed kernels, so text differs from the table above.

| PICO | base | mi | all | ext |
|---|---|---|---|---|
| Text | 98,795 | 98,175 | 98,047 | 97,799 |
| Deepest system call chain | 2,416 | 2,392 | 2,384 | 2,392 |
| Deepest boot chain (`main`) | 1,744 | 1,720 | 1,712 | 1,728 |
| Deepest user fault chain | 2,008 | 1,984 | 1,976 | 1,976 |
| Clock interrupt, with its exception frame | 292 | 292 | 292 | 292 |
| Terminal interrupt, with its exception frame | 444 | 444 | 444 | 444 |
| Probe interrupt (NMI), its exception frame | 36 | 36 | 36 | 36 |
| Sum | 3,188 | 3,164 | 3,156 | 3,164 |
| Against 2,048 | -1,140 | -1,116 | -1,108 | -1,116 |
| Sum with buffers of the swap pseudo-device | 3,676 | 3,596 | 3,588 | 3,628 |
| Sum with a panic | 6,084 | 6,012 | 5,900 | 6,052 |
| `panic`, from its own entry | 1,296 | 1,288 | 1,280 | 1,288 |

`stack/<variant>.txt` holds each root's path, `stack/syscalls.tsv` the
deepest chain under each of the 108 system call handlers, and
`stack/frames.tsv` each linked function's frame. Text is the same with
and without the call-graph options (98,175 for `mi`). The last two sums
are overlays, described under "Calls counted as overlays".

### What the flags change

The flag result is the difference between columns, which the model's
choices cancel out of:

- `mi` lowers the deepest chain by 24 bytes and the sum by 24. Under the
  108 handlers it lowers 60 chains, by 40 bytes at most and 10 on average,
  leaves 47 and raises one, `getfsstat`, by 8. Among linked functions 41
  frames shrink and 2 grow (`closei` and `getfsstat`, 8 bytes each), 320
  bytes net.
- `all` lowers 79 handler chains, by 48 at most, and raises `getfsstat`
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
    free .. bwrite 80, flstrategy 56, dhara_map_write .. flash_erase 656,
    the boot ROM's flash_range_erase 88

It is an `execve` of a script whose close-on-exec terminal waits for its
output to drain, where a tracing parent's kill ends the process inside
that wait and `exit` then closes the last reference to an unlinked file
with indirect blocks while Dhara collects garbage and erases a block.
Each step is a branch the source has; whether one run can take all of
them is not shown. Three handlers pass 1,928 bytes, the budget less the
120 bytes of `PendSV_Handler` and `syscall`, on their own: `execv`,
`execve` and `smount`. 33 more reach 1,536 bytes or more, where the entry
and one terminal interrupt (444) carry them over.

Two parts recur. The flash write path from `flstrategy` down costs 800
bytes and ends every deep chain: 176 of them are the `raw_gc` frame of
the vendored Dhara and 88 the boot ROM's erase. `execve` holds 400 bytes
and `namei_at` 368.

The bound is a static result under a model. The retained board watermark
(`sys/arch/rp2040/doc/CAPACITY.md`) is a different kind of evidence: 1,828
of 2,048 bytes touched in one stdio workload. A watermark above the bound
for a path the model covers would refute the model; a watermark below it
does not show the bound is loose, because that workload drives none of
the chain above. The deciding measurement is a PICO_DIAG board run that
drives the chain (a traced `execve` of a script, killed during the
terminal drain, with a large unlinked file open and the root near its
garbage-collection threshold) and reads `kern.capacity` afterwards.
PICO_DIAG's own bound is in the next table.

That run was made on a bare Pico on 2026-11-22 with PICO_DIAG built from
the tree of pull request 932 and the `exec` stage of
`tests/rp2040/stack_chain`. The first attempt, on the kernel before that
pull request, measured nothing: the kill left the swapper asleep on the
executable's inode lock and scheduling stopped, which is the defect the
pull request removes by closing the close-on-exec descriptors after
`execve` releases the inode. On the repaired kernel the stack watermark
read 1,392 of 2,048 bytes after login, 1,552 after three journal churn
rounds and an exit holding an unlinked file behind three indirection
levels, and 1,688 after the `exec` stage (child stopped by the trace
signal, killed, dead by signal; three runs alike), with 360 bytes untouched
and the saturation flag clear. Two conditions keep that reading from deciding the 2,424-byte bound. The
table predates
the repair, and the repair takes the terminal close out from under
`execve`'s frames, so the modeled chain that ends in that close is no
longer in the kernel that was measured; `stack_chain.py` on the repaired
ELF gives the bound to compare. And the run did not confirm that a Dhara
garbage collection ran under the chain, which is the 800-byte tail of the
model.

### The other configurations

`stack/configs.tsv` holds one row per configuration and stack, and
`stack/<CONFIG>.txt` the paths. All figures are under the default flags.

| | PICO | PICO_UART | PICO_DIAG | PICOCALC | PICOCALC_LEAN | PICOCALC_LEAN_DIAG |
|---|---|---|---|---|---|---|
| Text | 98,175 | 90,105 | 100,709 | 126,051 | 119,985 | 122,519 |
| Deepest system call chain | 2,392 | 2,408 | 2,424 | 2,424 | 2,392 | 2,424 |
| Deepest boot chain | 1,720 | 1,728 | 1,728 | 1,752 | 1,744 | 1,752 |
| Deepest user fault chain | 1,984 | 1,992 | 1,992 | 2,008 | 2,000 | 2,008 |
| Clock interrupt | 292 | 292 | 292 | 572 | 572 | 572 |
| Terminal interrupt | 444 | 436 | 444 | 444 | 444 | 444 |
| Sum on the u area | 3,164 | 3,172 | 3,196 | 3,548 | 3,480 | 3,512 |
| With buffers of the swap pseudo-device | 3,596 | 3,628 | 3,652 | 4,004 | 3,912 | 3,968 |
| With a panic | 6,012 | 6,028 | 6,052 | 7,004 | 6,928 | 6,968 |
| Sum on the render stack | - | - | - | 1,512 | 1,476 | 1,476 |
| Render stack, with a panic | - | - | - | 4,992 | 4,948 | 4,956 |

No configuration fits its u area under the bound, and the deepest chain
is the same one in each: the terminal that drains is `uart` on the Pico
kernels and `lcd` on the PicoCalc ones. The capacity option costs 16 bytes on
that chain (8 each in `sleep` and `getnewbuf`) and the trace option 16
more in `syscall`. The PicoCalc clock level is 280 bytes deeper
because the keyboard poll runs as a callout and feeds `ttyinput`:

    systick 16, hardclock 40, softclock 32, kbd_poll 56, lcdcons_input 32,
    ttyinput .. updatepri 360

The PicoCalc renderer runs on `lcd_render_stack`, 2,048 bytes that the
tool holds to the array's size in the image. Its bound is `lcdrender`'s
own chain (424, eight of them pushed by `SPARE_IRQ_0_Handler`) with the
clock, terminal, audio and probe levels on top: 1,512 bytes, 536 free.
That is the one stack the bound clears, and only without a panic. On the
u area the renderer costs its 36-byte exception frame. The PicoCalc sums
count the audio interrupt (no frame and no call in the graph, so its
36-byte exception frame alone) and the
render entry as levels of their own.

### Calls counted as overlays

Two kinds of call are left out of the figures above and counted in a
second bound each, so that the plain figures stay those of ordinary file
system traffic. An overlay's sum is reported on the `stack` lines of each
report and its chains beside the plain ones.

- **Buffers of the swap pseudo-device.** `bwrite`, `bread` and `breada`
  reach `swstrategy` when a buffer belongs to the block node of a
  temporary swap extent. `swstrategy` calls `swap`, which takes a buffer
  of its own through `getnewbuf`, which may write another delayed buffer
  of the same device: the cache nests under itself. Each entry holds one
  buffer busy, so `NBUF` (4, held to the size of `buf` in the image)
  bounds the entries. A round costs 104 bytes and the overlay adds 432 to
  the deepest PICO chain (2,824) and to the sum (3,596). The bound counts
  four entries followed by a write; the fourth entry finds no buffer left
  to write, so the figure is one round looser than the tightest argument.
- **A panic.** `panic` is counted where it is called: 4,088 bytes on the
  deepest PICO chain and 6,012 for the sum. The first entry syncs the file
  system from wherever it was raised, which is the 1,288-byte chain of the
  table's last row; one chain takes that fresh entry and every handler
  above it is taken as a second entry. The second entry asks `boot` for
  `RB_NOSYNC` because `panicstr` is set. The graph does not bound the
  nesting beyond that. Each console's output routine polls its device and
  hands received bytes to `ttyinput`, which reaches `wakeup` (`usbputc`
  through `usb_service`, `uartputc` through `uartintr`, `lcdputc` through
  `reply_flush`), and `wakeup` panics on a sleep queue entry that is
  neither asleep nor stopped. Since #838 `wakeup` returns at once while
  `panicstr` is set, as `tsleep` does, so a panic's own print does not
  re-enter `panic` through it; the graph still draws that edge, because
  the tool does not read the test, and the overlay keeps counting two
  entries and leaving a third out. The two overlays are
  not combined: a panic whose sync writes buffers of the swap
  pseudo-device is in neither figure.

### The boot ROM and the second stage

`bootrom_frames.py` measures the ROM routines the kernel calls from
`b2.elf` of the raspberrypi/pico-bootrom-rp2040 release B2 (version byte
3, image SHA-256 3a3c4c61..21cc49aa, ROM contents SHA-256
9c19b46f..bc2efd81). It finds each routine through the function table the
kernel itself walks and follows every path of its disassembly with the
bytes pushed so far. `stack/bootrom.txt` holds the result, and
`--check` holds the model's 15 ROM site rules to it:

| Routine | Code | Bytes |
|---|---|---|
| `flash_range_erase` | RE | 88 |
| `flash_range_program` | RP | 88 |
| `flash_exit_xip` | EX | 64 |
| `connect_internal_flash` | IF | 8 |
| `flash_flush_cache` | FC | 8 |
| `reset_usb_boot` | UB | 16 |
| the table lookup | - | 0 |
| the tree's second stage, `boot2_w25q080.S` | - | 24 |

The erase is 16 bytes deeper than the compiled leaf it replaces at the
end of the flash write path, which is the whole of the ROM's effect on
the deepest chain. The figures hold for that image. The measured board
reports ROM version 3; comparing a readout of its ROM with the contents
hash above is the check that it is this image, and it has not been run.
Under the PicoCalc UF2 loader the second stage in flash is the loader's,
which is not measured.

### The model

`stack-model.json` declares what the call graph cannot say. The tool
refuses a build the model does not cover: an indirect call site that
matches no rule, a rule or guard that matches nothing, a call cycle, a
function with a dynamic frame, a nesting that goes past its bound or
never reaches it, a budget or buffer count the image contradicts, a
configuration or overlay the model does not name, and a linked function
whose address an object file stores while no rule, root or exemption
names it. That last check is what catches a callback registered in a file
the model's author never read. `stack_chain.py --self-test` runs the
analysis over synthetic graphs with hand-computed depths and requires 24
malformed inputs to be refused; `bootrom_frames.py --self-test` does the
same for the ROM walk with six. Run with the model of commit 1c087591,
which has none of the entries added since, the tool reproduces that
commit's `mi` report byte for byte.

- **Configurations.** The model lists the six configurations. An entry
  with its own list applies to those alone (24 site rules, three guards,
  three roots, one exemption and the render stack), and such an entry is held to the
  same rule as any other in the kernels it names.
- **Roots.** `PendSV_Handler` (system calls), `main`, `HardFault_Handler`
  (a user fault that posts a signal) and `SVC_Handler` are the base
  chains; `systick` at the clock level, the USB and UART handlers at the
  terminal level, the PicoCalc audio handler and the core 1 probe's
  `NMI_Handler` are added on top, each with a 36-byte exception frame,
  whether or not the deepest point masks them. `panic` and `boot` are
  reported alone.
- **Indirect calls.** 97 site rules. 66 read their targets from the linked
  image (`cdevsw`, `bdevsw`, `Fops`, `execsw`, `syscall_handlers`,
  `conf_service_init`, `flswap_ops`, `sd_ops`) through DWARF member
  offsets; 13 name functions assigned at run time (callouts, `t_oproc`, driver probes, sysctl subtrees, the core ring, the dump writer, the PicoCalc console's select hooks); 15
  carry a measured depth for a ROM routine or the second stage; 2 count
  frames only; 1, `ucall`, names a call left out.
- **Cycles.** Five bounded recursions (`wakeup`, `psignal`, `mfree` two
  deep, `indirtrunc` three, `errno_message_expand` five), two functions whose second entry returns before any call (`ttypend`, `exec_script_check`; `exit` left the list at the refresh below) and the two overlay nestings, each with its reason
  in the file.
- **Guards.** 43 calls whose arguments rule out part of the callee:
  26 callers that sleep only at `PZERO` or below and so never test a
  signal (the tool checks the priority in the source text of every such
  call, in every build), the kernel's own 16 `rdwri` callers, whose
  inode is a regular file, a directory or a symbolic link and never a
  device (the nine that only read are also kept from the truncation and
  synchronous-write arms, and the tool checks `UIO_READ` at each of their
  calls), and a pipe's release, which has no indirect block.

### Refresh at 4eb0f4eb3

`stack-4eb0f4eb3/` holds the same outputs for main at 4eb0f4eb3, built
with arm-none-eabi-gcc 16.2.0; the sections above keep the 793d3432
figures and the flag comparison built on them. Two model entries changed
for this run. `exit` left the reentry list: since 2e8cad18b a PT_KILL
child ends through SIGKILL rather than `exit()` under `procxmt`, so the
chain that re-entered it is gone, and the tool refuses a reentry entry it
never meets. `picokbd_evq_select` gained site rules for its three indirect
calls (`o->raise`, `o->restore`, `o->tty`) on the three PicoCalc
configurations, where `lcdselect()` passes the one `struct picokbd_sel`,
`lcd_sel`; the tool stops at an indirect call outside every rule.

| PICO | base | mi | all | ext |
|---|---|---|---|---|
| Text | 98,881 | 98,257 | 98,145 | 97,897 |
| Deepest system call chain | 2,328 | 2,312 | 2,304 | 2,304 |
| Sum | 3,100 | 3,084 | 3,076 | 3,076 |
| Sum with buffers of the swap pseudo-device | 3,588 | 3,516 | 3,508 | 3,540 |
| Sum with a panic | 6,020 | 5,956 | 5,844 | 5,988 |

| | PICO | PICO_UART | PICO_DIAG | PICOCALC | PICOCALC_LEAN | PICOCALC_LEAN_DIAG |
|---|---|---|---|---|---|---|
| Text | 98,257 | 90,597 | 100,800 | 127,728 | 119,730 | 122,265 |
| Deepest system call chain | 2,312 | 2,320 | 2,336 | 2,352 | 2,328 | 2,352 |
| Deepest boot chain | 1,720 | 1,728 | 1,728 | 1,736 | 1,728 | 1,736 |
| Deepest user fault chain | 2,008 | 2,016 | 2,016 | 2,032 | 2,024 | 2,032 |
| Clock interrupt | 292 | 292 | 292 | 572 | 572 | 572 |
| Terminal interrupt | 444 | 436 | 444 | 444 | 444 | 444 |
| Sum on the u area | 3,084 | 3,084 | 3,108 | 3,476 | 3,416 | 3,440 |
| With buffers of the swap pseudo-device | 3,516 | 3,540 | 3,564 | 3,932 | 3,848 | 3,896 |
| With a panic | 5,956 | 5,964 | 5,988 | 6,908 | 6,840 | 6,872 |
| Sum on the render stack | - | - | - | 1,512 | 1,476 | 1,476 |
| Render stack, with a panic | - | - | - | 4,968 | 4,924 | 4,932 |

The deepest chain is 80 bytes shallower on PICO and no longer passes
through `procxmt` and `exit`. It is now the lookup of
`exec_script_check`, which drops the last reference to an unlinked inode
while Dhara collects garbage:

    PendSV_Handler 88, syscall 32, execv 8, execve 400, exec_check 40,
    exec_script_check 120, namei 8, namei_at 368, iget 32, iput .. irele 24,
    itrunc 144, indirtrunc 168, free .. bwrite 80, flstrategy 56,
    dhara_map_write .. flash_erase 656, the boot ROM's flash_range_erase 88

Every configuration still passes 2,048 bytes on that chain alone. Issue
#839 tracks the bound and #840 the panic on the render stack. #838, the
path from a panic's print back into `panic` through `wakeup`, is closed
by the `panicstr` test in `wakeup`. A trial that skipped the first
entry's sync on the render stack measured 3,744 bytes of 2,048 on PICOCALC
and 3,708 on the two Lean kernels (4,968 and 4,924 to 4,932 with the sync),
so #840 needs the panic moved off that stack, not the sync removed.

### What the bound leaves out

- **Panics nested more than two deep.** The graph has no bound for them;
  the path through `wakeup` above is closed in the source (#838), not in
  the graph.
- **The two overlays together.** A panic's sync over buffers of the swap
  pseudo-device is counted by neither.
- **The board's ROM and the PicoCalc loader's second stage.** The ROM
  figures are those of the B2 image, and the second-stage figure that of
  the tree's own.
- **`ucall`.** It calls a caller-supplied kernel address at securelevel 0.
- **Terminal modem ioctls.** `ttyblock` and `ttyunblock` count the
  driver's and `ttioctl`'s frames and none of their other calls.
- **Terminal drivers by name.** 16 `cdevsw` sites whose device is a
  terminal keep only table entries named `usb`, `uart`, `lcd`, `pt`,
  `null` or `no`. The other prefixes in the six kernels' `cdevsw` tables are
  `cn`, `sy`, `fd`, `mm`, `sw`, `sel`, `sd`, `gpio` and `pcm`, none of
  them a terminal driver; one added later under a new prefix would be
  dropped there without a refusal.
- **Device open and close by table.** `device_driver_call` is taken to
  reach every open and close routine of both device tables from each of
  its callers, which is wider than any one caller's device.
- **Core 1.** Its functions run on `core1_stack`.
- **A fault inside the renderer.** The render stack's bound has no fault
  root; `HardFault_Handler` is counted on the u area only. A panic raised
  there is counted, and #840 tracks it.
- **PICOCALC_PSRAM.** The configuration left the tree with the split into
  PICOCALC, PICOCALC_LEAN and PICOCALC_LEAN_DIAG (6f522c3ec).
- **PICOCALC_SD and PICOCALC_SD_DIAG.** The board-session and card-image
  kernel and its diagnostic build have tracked compile directories outside
  the six configurations `stack-chain.sh` builds, so this record lacks a
  bound for them. Adding them takes their names in the script and the
  model and site rules for their SD services (`sdrootattach`,
  `sdswapattach`) and the SD-root and swap paths.
