# RP2040 RAM ownership and memory-map verdict

## Claim boundary

No single memory map is optimal for every objective. A larger process window,
a larger compressed-swap pool, more kernel growth room, lower flash traffic,
lower latency and lower idle power compete for the same physical resources.
The shipping map is Pareto-credible under the current priority order:
correctness and recovery, a fixed 192 KiB process window, a 16 KiB SwapRAM
pool where configured, a 4 KiB exchange reserve and positive linked growth room
in every shipped configuration.

The measurements below come from the six kernels built at `d81b1788` with GCC
16.2.0 and GNU ld (`bmake MACHINE=rp2040 logged-build`), read with
`arm-none-eabi-nm -S` against `RP2040.ld` and `kern.ldscript`. They describe linked
artifacts, not runtime heap, stack, traffic, endurance or power peaks. Board
records named in each section supply the workload evidence.

## Physical decomposition

```text
0x20000000  +-------------------------------+
            | resident user image           | 192 KiB
0x20030000  +-------------------------------+
            | kernel data, BSS, .ramfunc,   | 58 KiB
            | optional SwapRAM pool         |
0x2003e800  +-------------------------------+
            | process-0 u area              | 3 KiB
0x2003f400  +-------------------------------+
            | current u area + kernel stack | 3 KiB
0x20040000  +-------------------------------+
            | SRAM4 + SRAM5 scratch banks   | 8 KiB
0x20042000  +-------------------------------+
```

SRAM0--3 provide the first 256 KiB through the striped address range. The
bank-specific range at `0x21000000..0x2103ffff` aliases the same cells and adds
no capacity (`../hardware-review-reconciliation.md`, datasheet 2.6.2). SRAM4
and SRAM5 are two separate 4 KiB banks. The USB DPSRAM at `0x50100000` is a
separate 4 KiB peripheral memory with its own execution, reset and ownership
rules. The PicoCalc's PSRAM is a PIO block device and has no C address range.

The 192 KiB user range takes two MPU regions (128 KiB at `0x20000000`, 64 KiB
at `0x20020000`); the boot ROM and the SIO divider take two more, so four of
eight regions are programmed (`sys/arch/rp2040/doc/MPU.md`). Every process uses
the same physical user addresses; the MPU grants permissions and translates
nothing.

Each 3,072-byte u area (`USIZE`) holds a 1,024-byte `struct user`
(`UAREA_STACK_OFFSET`) and a 2,048-byte kernel stack. A context switch
exchanges the whole area. A retained board workload touched 1,828 of the
2,048 stack bytes and left 220 (`../board-capacity-acceptance.md`); that
watermark does not cover every fault, signal, interrupt and SwapRAM path.

## Linked kernel RAM per configuration

Physical static bytes are `_ebss - __kernel_data_start` (`.data`, `.ramfunc`,
BSS, alignment). The SwapRAM pool (`swapram_pool_mem`) extends the used
interval to `__kernel_ram_used_end`. Slack runs to `u0` (`0x2003e800`); the last
column also subtracts the 4,096 bytes the linker reserves for an in-place
flash-swap exchange.

| Configuration | Physical static | SwapRAM pool | Used kernel RAM | Slack to `u0` | Slack after exchange reserve |
| --- | ---: | ---: | ---: | ---: | ---: |
| PICO | 19,896 | 16,384 | 36,280 | 23,112 | 19,016 |
| PICO_DIAG | 19,896 | 16,384 | 36,280 | 23,112 | 19,016 |
| PICO_UART | 19,744 | 0 | 19,744 | 39,648 | 35,552 |
| PICOCALC | 32,968 | 16,384 | 49,352 | 10,040 | 5,944 |
| PICOCALC_LEAN | 28,368 | 16,384 | 44,752 | 14,640 | 10,544 |
| PICOCALC_LEAN_DIAG | 28,464 | 16,384 | 44,848 | 14,544 | 10,448 |

PICO and PICO_DIAG differ in flash text only (their ordinary BSS extents
agree because alignment absorbs the removed diagnostic state). PICOCALC is the
limiting row. The 8 KiB USB transmit ring (`usb_tx_ring`, SRAM4 and SRAM5) is
outside the ordinary kernel RAM interval. These figures replace the four-row
table of the PR #435 notes, whose configurations and sizes predate PICO_DIAG
(`fdd25393`), PICOCALC_LEAN and the u-area argument prefix.

## USB DPSRAM ownership

In every USB configuration `kern.ldscript` places:

| DPSRAM interval | Bytes | Owner |
| --- | ---: | --- |
| `0x50100000..0x5010023f` | 576 | USB setup, endpoint controls and endpoint buffers |
| `0x50100240..0x5010063f` | 1,024 | Dhara page (`flpage`) |
| `0x50100640..0x5010073f` | 256 | unassigned |
| `0x50100740..0x50100b4f` | 1,040 | codec work area (`swapram_codec_work`) |
| `0x50100b50..0x50100eff` | 944 | unassigned |
| `0x50100f00..0x50100fff` | 256 | flash program-page buffer (`flscratch`) |

`usbinit()` clears all `USB_DPRAM_SIZE` (4,096) bytes by word stores
(`sys/arch/rp2040/dev/usb.c`). The two gaps total 1,200 bytes and do not form
a heap. Any owner has to tolerate USB initialization and reset order, use data
accesses only, and stay disjoint from controller-visible buffers.

The UART-only kernel keeps `flpage` at `0x20040000`, `boot2_copy` at
`0x20040400` and `flscratch` at `0x20041f00`; `0x20040500..0x20041eff` (6,656
bytes) has no owner. Moving `flscratch` to `0x20040500` would leave all of SRAM5
unowned after boot (the PR #435 packing change). It edits `kern.ldscript`, so it
waits for the kernel text-layout work (PR #662); see the closeout record.

## USB ring accounting

`machdep.usb_tx_peak` and `machdep.usb_tx_dropped` (`7d779094`) report the
greatest ring occupancy and the bytes discarded from a full ring, per boot,
including output before a host opens the console. The drop count saturates at
`USB_TX_DROPPED_MAX`; a saturated value gives no exact delta. Two separate
sysctl reads are not an atomic pair. `check-usb-console` exercises the
enqueue accounting (`sys/arch/rp2040/doc/TESTING.md`); controller timing and
board throughput remain hardware measurements.

## Process-window pressure

The projection keeps PICOCALC's 49,352 used bytes and the 4 KiB reserve and
assumes relocation changes no code or data size. Each row is an arithmetic
bound, not a linked candidate.

| User window | End | User MPU regions | Raw kernel RAM | Remaining growth room | Verdict |
| ---: | --- | ---: | ---: | ---: | --- |
| 192 KiB | `0x20030000` | 2 | 59,392 | 5,944 | retained |
| 196 KiB | `0x20031000` | 3 | 55,296 | 1,848 | linkable bound; spends 4,096 bytes of the worst margin and a fifth MPU region |
| 200 KiB | `0x20032000` | 3 | 51,200 | -2,248 | exchange reserve no longer fits |
| 204 KiB | `0x20033000` | 4 | 47,104 | -6,344 | exchange reserve no longer fits |
| 208 KiB | `0x20034000` | 3 | 43,008 | -10,440 | exchange reserve no longer fits |

A 196 KiB window gains 4 KiB of process capacity and removes 4 KiB of the
worst-configuration margin (5,944 to 1,848). It also adds an MPU region and
changes the linker, loader, user-copy, qemu, Renode, board and documentation
oracles that name the 192 KiB boundary. No retained workload record shows a
need for the extra 4 KiB (`../program-window-large-swap.md` treats the
196-block image bound). A window above 192 KiB therefore needs 4,096 bytes of
verified static reduction in PICOCALC before the margin returns to its
current value.

## Larger SwapRAM pool, link bound

A private relink with a 32,768-byte pool (probe `--swapram-32` of the PR #435
tool, run unchanged against `d81b1788`) keeps PICO and PICO_DIAG linked, with
the post-exchange margin falling from 19,016 to 2,632 bytes. PICOCALC fails
the production exchange-reserve assertion; PICO_UART has no pool. Compression
acceptance, fallback traffic, runtime peaks and latency are unmeasured, so
link capacity is not a wear reduction. At `7aed0038` on the branch tree the
PICO margin was 5,408 bytes; `main` has since grown.

## Surviving experiments

1. A PICO-only 16/32 KiB SwapRAM comparison remains linkable (2,632 bytes of
   margin). Telemetry and recovery evidence decide whether reduced flash
   traffic repays the kernel margin (`wear-recovery.md`).
2. A PICO_UART layout can pack the three flash work objects into SRAM4 and
   leave SRAM5 unowned after boot. A supply-current measurement decides
   whether powering SRAM5 down benefits; reboot and boot-ROM staging stay
   constraints.
3. The two DPSRAM gaps can hold at most 1,200 bytes of named, data-only,
   USB-lifetime storage, admitted by linker adjacency assertions,
   initialization after the full DPSRAM clear, USB-reset behavior and a board
   transfer test.
4. USB ring high-water, wait and drop counters (now in the kernel) can decide
   whether a smaller power-of-two ring keeps boot capture and sustained
   output. Released SRAM4 and SRAM5 bytes remain scratch capacity until a
   linked owner claims them.

## Reproduction and falsifiers

```sh
arm-none-eabi-size -A sys/arch/rp2040/compile/CONFIG/unix.elf
arm-none-eabi-nm -n -S --defined-only sys/arch/rp2040/compile/CONFIG/unix.elf
arm-none-eabi-readelf -lW sys/arch/rp2040/compile/CONFIG/unix.elf
```

Different symbol values at the same revision and toolchain falsify the
occupancy rows. A process-window candidate fails when any shipped
configuration loses the 4 KiB exchange reserve, violates MPU alignment,
exposes kernel aliases to user mode or leaves an oracle at the old boundary.
A smaller u area or USB ring fails when a calibrated workload reaches the
removed bytes. A special-memory owner fails when reset, initialization,
execution attributes or concurrent hardware access contradict its declared
lifetime.
