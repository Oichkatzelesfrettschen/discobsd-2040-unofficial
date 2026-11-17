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
