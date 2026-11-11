# RP2040 RAM ownership and memory-map verdict

## Claim boundary

No single memory map is optimal for every objective. A larger process window,
a larger compressed-swap pool, more kernel growth room, lower flash traffic,
lower latency and lower idle power compete for the same physical resources.
The retained map is Pareto-credible under the current priority order:
correctness and recovery, a fixed 192 KiB process window, a 16 KiB SwapRAM
pool where configured, a 4 KiB exchange reserve, and positive linked growth
room in every shipped configuration.

The measurements use revision `6fe8afd91d78`, GCC 15.2.1 and GNU ld 2.45.1.
They describe linked artifacts, not runtime heap, stack, traffic, endurance or
power peaks. Exact addresses come from `RP2040.ld`, `kern.ldscript`, ELF
sections and linker symbols. Retained board records supply separate workload
evidence.

## Physical decomposition

The main SRAM ownership is contiguous and complete:

```text
0x20000000  +-------------------------------+
            | resident user image           | 192 KiB
0x20030000  +-------------------------------+
            | kernel data, BSS, .ramfunc,   | 58 KiB
            | optional SwapRAM pool          |
0x2003e800  +-------------------------------+
            | process-0 u area               | 3 KiB
0x2003f400  +-------------------------------+
            | current u area + kernel stack  | 3 KiB
0x20040000  +-------------------------------+
            | SRAM4 + SRAM5 scratch banks    | 8 KiB
0x20042000  +-------------------------------+
```

SRAM0--3 provide the first 256 KiB through the striped address range. The
bank-specific range at `0x21000000..0x2103ffff` aliases the same cells and
adds no capacity. SRAM4 and SRAM5 are two separate 4 KiB banks. The USB
DPSRAM at `0x50100000` is a separate 4 KiB peripheral memory with different
execution, reset and ownership rules. The PicoCalc's 8 MiB PSRAM is a PIO
block device rather than a memory-mapped C address range.

The 192 KiB user range consumes two MPU regions: 128 KiB at `0x20000000` and
64 KiB at `0x20020000`. The boot ROM and the SIO divider consume two more.
Four of the eight MPU regions remain disabled. Every process uses the same
physical user addresses; the MPU grants permissions and performs no address
translation.

The u-area reservation is not an ordinary 6 KiB pool. Each 3,072-byte area
contains a 1,024-byte `struct user` allocation and a 2,048-byte kernel-stack
allocation. Context switching exchanges the complete area. A retained board
workload touched 1,828 stack bytes and left 220 painted bytes; another retained
capture touched 1,732 and left 316. Neither watermark covers every fault,
signal, interrupt and SwapRAM path.

## Configuration-specific linked occupancy

Physical static bytes equal `_ebss - __kernel_data_start`; the calculation
includes `.data`, `.ramfunc`, BSS and linker alignment. Pool bytes then extend
the used interval to `__kernel_ram_used_end`. Main-RAM slack ends at `u0`.
The final column also subtracts the 4,096 bytes reserved by the linker for an
in-place flash-swap exchange.

| Configuration | Physical static | SwapRAM pool | Used kernel RAM | Slack to `u0` | Slack after exchange reserve |
| --- | ---: | ---: | ---: | ---: | ---: |
| PICO | 17,112 | 16,384 | 33,496 | 25,896 | 21,800 |
| PICO_UART | 16,688 | 0 | 16,688 | 42,704 | 38,608 |
| PICOCALC | 29,200 | 16,384 | 45,584 | 13,808 | 9,712 |
| PICOCALC_PSRAM | 29,768 | 16,384 | 46,152 | 13,240 | 9,144 |

The ELF SHA-256 values are:

| Configuration | SHA-256 |
| --- | --- |
| PICO | `dde1d518bcd5451e8f3546f2cd5cf0f59bb9f83944666d53fac6eb0aff351f26` |
| PICO_UART | `b44bc30e21f5aaf3d9b835955767e23ff0a99158756b7229107ae342fec468a5` |
| PICOCALC | `429aa304b86405667044a4a2c8d145b7579b834ca5e31ba5de7b9a27e1fbf95f` |
| PICOCALC_PSRAM | `8694e37dd93a687567724c7e03cb61384ba9cda652190b41896b7695f4a0c9e1` |

PicoCalc state explains the limiting rows: a 4 KiB tile atlas, two 1,696-byte
terminal arrays, a 2 KiB render stack, a 2,440-byte two-mount table and a
2,208-byte inode table. The 8 KiB USB transmit ring occupies SRAM4/SRAM5 and
does not appear in the ordinary kernel-RAM interval.

USB configurations divide DPSRAM as follows:

| DPSRAM interval | Bytes | Owner |
| --- | ---: | --- |
| `0x50100000..0x5010023f` | 576 | USB setup, endpoint controls and endpoint buffers |
| `0x50100240..0x5010063f` | 1,024 | Dhara page |
| `0x50100640..0x5010073f` | 256 | unassigned gap |
| `0x50100740..0x50100b4f` | 1,040 | heatshrink workspace |
| `0x50100b50..0x50100eff` | 944 | unassigned gap |
| `0x50100f00..0x50100fff` | 256 | flash program-page buffer |

`usbinit()` clears the complete DPSRAM. The two gaps total 1,200 bytes but do
not form a heap. Any owner has to tolerate USB initialization and reset order,
use data accesses only, and remain disjoint from controller-visible buffers.

PICO_UART assigns a 1,024-byte Dhara page at `0x20040000`, a 256-byte boot2
copy at `0x20040400`, and a 256-byte flash buffer at `0x20041f00`. The interval
`0x20040500..0x20041eff` contains 6,656 unassigned bytes. Moving the last
buffer beside the first two would leave all of SRAM5 unowned after boot.

## Process-window pressure

The projection keeps the current PICOCALC_PSRAM physical static bytes, 16 KiB
pool and 4 KiB exchange reserve. It assumes relocation does not change code or
data size. Each result is therefore an arithmetic bound rather than a linked
candidate.

| User window | End | User MPU regions | Raw kernel RAM | Remaining growth room | Verdict |
| ---: | --- | ---: | ---: | ---: | --- |
| 192 KiB | `0x20030000` | 2 | 59,392 | 9,144 | retained |
| 196 KiB | `0x20031000` | 3 | 55,296 | 5,048 | linkable projection; insufficient gain for the lost reserve and extra MPU region |
| 200 KiB | `0x20032000` | 3 | 51,200 | 952 | rejects ordinary toolchain and feature growth |
| 204 KiB | `0x20033000` | 4 | 47,104 | -3,144 | exchange reserve no longer fits |
| 208 KiB | `0x20034000` | 3 | 43,008 | -7,240 | exchange reserve no longer fits |

A 196 KiB window gains 4 KiB of process capacity while consuming 4 KiB of the
worst kernel margin, a fifth programmed MPU region, and an update to every
linker, loader, user-copy, QEMU, Renode, board and documentation oracle. Raw
flash swap also holds 384 KiB before its scratch reservation, so a window above
192 KiB worsens the two-image exchange bound. No retained workload record
identifies a need for the extra 4 KiB.

A smaller window restores kernel room but removes process capacity already
used as the fixed ABI. A 176 KiB window can accommodate a second 16 KiB of
SwapRAM at approximately the current worst growth margin, but it consumes a
third user MPU region and reduces the maximum admitted process image by 16 KiB.
No retained evidence establishes a net benefit over the present 192/16 split.

## Adversarial dispositions

| Proposal | Contrary evidence or failure mode | Disposition |
| --- | --- | --- |
| Treat the unstriped aliases as additional RAM | The aliases address SRAM0--3 cells already owned through `0x20000000`; opening them to user mode also exposes aliased kernel cells | falsified as capacity |
| Place hot data in one aliased bank | Retained bus counters measured 250 contested SRAM0 accesses out of 8,665,460 with core 1 observing, about 0.003 percent | declined until a workload produces material contention |
| Disable XIP cache and use its 16 KiB as SRAM | A retained board control slowed one command from about 4 seconds to 93 seconds; kernel execution depends on XIP | falsified for the running kernel |
| Treat USB DPSRAM as general RAM | It is peripheral memory, cleared by USB initialization, execute-never under the effective map and partly controller-owned | rejected as a heap; named data-only owners remain possible |
| Put executable flash-recovery code in DPSRAM | The ARM peripheral region is execute-never | falsified |
| Put kernel objects or a user image in PicoCalc PSRAM | The device is reached through PIO transactions and has no memory-mapped execution or load/store window | falsified; block swap remains valid |
| Move the u-area stacks into SRAM4/SRAM5 | USB kernels fill both banks, context switching exchanges the complete u area, and the retained stack margin is narrow | rejected without a new context format and complete stack corpus |
| Shrink `USIZE` or the 2 KiB kernel stack | Painted slack is 220 bytes in the greatest retained workload and misses reserved-but-unwritten frames | falsified by the admission rule |
| Shrink the 8 KiB USB transmit ring immediately | The ring retains boot output until a host opens the console; no high-water or throughput record establishes a safe smaller depth | blocked on telemetry |
| Move `.ramfunc` into UART scratch | PICO_UART has room, but USB builds do not; the saving is below the 4 KiB process-window step and creates a configuration-specific execution map | declined without a measured latency, power or capacity objective |
| Reorder kernel RAM below the user range | Equivalent capacity remains, while the user ABI, executable origin, MPU map and every boundary oracle change | rejected as churn without resource gain |
| Execute all user text from filesystem flash | OMAGIC images, mutable process layout, flash-program XIP outages and the absence of address translation prevent a general mapping | falsified as a direct remap; the bounded XIP shared-libc design remains separate |
| Double the SwapRAM pool in every configuration | PICO retains 5,416 bytes after a 32 KiB pool and exchange reserve; PICOCALC and PICOCALC_PSRAM exceed their current kernel-RAM budgets by 6,672 and 7,240 bytes | falsified as a uniform map; a PICO-only experiment remains linkable |

## Surviving map and experiments

The fixed 192 KiB process window, 58 KiB kernel interval, two 3 KiB u areas
and 8 KiB scratch interval remain the shipping map. The selection preserves
two user MPU regions and 9,144 bytes of worst-case linked growth room after the
exchange reserve. The value is a static margin, not permission to spend all of
it.

Four bounded experiments survive:

1. A PICO-only 16/32 KiB SwapRAM comparison remains linkable. The 32 KiB arm
   retains 5,416 projected bytes after the exchange reserve. Telemetry and
   recovery evidence decide whether reduced flash traffic repays the smaller
   kernel margin.
2. A PICO_UART layout can pack all three flash work objects into SRAM4 and
   leave SRAM5 unowned after boot. Supply-current measurement decides whether
   powering SRAM5 down has a useful benefit. Reboot and boot-ROM staging remain
   explicit constraints.
3. The two USB DPSRAM gaps can host at most 1,200 bytes of named, data-only,
   USB-lifetime storage. Linker adjacency assertions, initialization after the
   complete DPSRAM clear, USB-reset behavior and a board transfer test form the
   admission evidence.
4. USB transmit-ring high-water, wait and dropped-output counters can establish
   whether a smaller power-of-two ring preserves boot capture and sustained
   console output. Released SRAM4/SRAM5 bytes remain scratch capacity until a
   separately linked owner claims them; they do not automatically enlarge the
   process window.

A future 196 KiB proposal first needs at least 4,096 bytes of verified static
reduction in PICOCALC_PSRAM merely to restore the current 9,144-byte margin.
The extra MPU region, swap geometry, full boundary-oracle update and a workload
that needs more than 192 KiB remain independent admission costs.

## Reproduction and falsifiers

The linked measurements derive from clean configuration artifacts with:

```sh
arm-none-eabi-size -A sys/arch/rp2040/compile/CONFIG/unix.elf
arm-none-eabi-nm -n -S --defined-only \
    sys/arch/rp2040/compile/CONFIG/unix.elf
arm-none-eabi-readelf -lW sys/arch/rp2040/compile/CONFIG/unix.elf
```

Different symbols at the same revision and toolchain falsify the occupancy
rows. A process-window candidate fails when any shipped configuration loses
the 4 KiB exchange reserve, violates MPU alignment, admits user access to
kernel aliases, or leaves an active oracle at the old boundary. A smaller
u-area or USB ring fails when a calibrated workload reaches the removed bytes.
A special-memory owner fails when reset, initialization, execution attributes
or concurrent hardware access contradict its declared lifetime.
