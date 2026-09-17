# Running the DiscoBSD RP2040 kernel under emulation

Branch `emulation`, forked from
`rp2040-port` at `3fcbdb413329fad56b91912a698cd6e0d585e28b`. The board was
never touched: no `/dev/ttyACM0`, no `picotool`.

## What was tried, in order

### 1. Renode (used)

`paru -Ss renode` found `renode-bin` in the AUR; installed with
`SUDO_ASKPASS=/usr/bin/unified-askpass paru -S --noconfirm --sudoflags=-A
renode-bin`. `renode --version` reports `1.17.0 (1.17.0+20260907gitf1dd1b4af)`,
runtime .NET 9.0.19. Renode's own distribution ships no RP2040 platform:

    $ find /opt/renode -iname "*rp2040*" -o -iname "*pico*"
    /opt/renode/platforms/cpus/litex_picorv32.repl
    /opt/renode/platforms/cpus/picosoc.repl
    (plus license files matching "pico" as a substring, unrelated)

`paru -Ss pico | grep -i emul` and `paru -Ss rp2040js` found nothing in the
AUR. `npm view rp2040js` shows it exists (1.3.4, MIT, wokwi/rp2040js) but is
a Node.js library meant to be embedded in a harness, not a standalone
emulator with a GDB server; building one around it would have meant writing
as much glue as the Unicorn fallback, for a model with no published register
fidelity table, so it was not pursued once a better-documented option
surfaced.

Web search turned up `matgla/Renode_RP2040` (MIT license for the C#
peripheral models it adds; a vendored copy of the real RP2040 boot ROM under
the license of `raspberrypi/pico-bootrom-rp2040`), a third-party platform
description and peripheral set for Renode, pinned here at commit
`205a5e4b25440582008a4292074bb07f80a72328`. Its README publishes a
peripheral-by-peripheral support table; the entries this port depends on:

| Peripheral | Support | Limitation noted |
|---|---|---|
| XOSC | full | |
| ROSC | full | |
| PLL | full | |
| Resets | full | |
| Watchdog | full | tick generator stubbed with a LimitTimer |
| UART (PL011) | full | reimplemented to add DMA/PIO request lines |
| GPIO | full | |
| SIO | partial | multicore dividers unfinished |
| SSI | partial | "XIP support/caches are stubbed" |
| XIP | partial | "bootrom correctly starts firmware" |
| SysInfo | none | |
| USB | none | |
| PWM | none | |
| RTC | none | |

USB being unmodeled is why this report and BOOT-MAP.md section 12 use a
UART0-console kernel build rather than the board's default USB CDC-ACM
console. SysInfo being unmodeled is why `cpuidentify()` in `machdep.c`
prints `cpu: RP0000 rev 0` instead of the real chip ID -- `SYSINFO_CHIP_ID`
reads back zero.

The repository ships the genuine RP2040 boot ROM as an ELF,
`bootroms/rp2040/b2.elf` (16 KB `.text`, entry point 0, taken from
`raspberrypi/pico-bootrom-rp2040`), loaded at address 0. This means
`rom_func_lookup()` in `sys/arch/rp2040/dev/flash.c` -- which reads a real
16-bit pointer at 0x14 and 0x18 and calls through it to resolve 'IF', 'EX',
'RE', 'RP', and 'FC' by their real two-letter codes -- runs against actual
boot ROM machine code, not a stub. Resetting `cpu0`'s vector table to
address 0 (rather than to the kernel's, the way the repository's own
`run_firmware.resc` does for bare pico-examples binaries) makes the CPU
begin execution in the real ROM's reset vector, so the whole boot sequence
BOOT-MAP.md section 1 describes -- the 256-byte checksum read, the copy to
SRAM, the jump into boot2, boot2's own jump into the kernel at
0x10000100 -- runs as it would on silicon.

The upstream `emulation/Peripherals.csproj` targets `netstandard2.1`, which
predates both the installed Renode build and the system `dotnet` SDK
(10.0.111); building it unmodified fails:

    CSC : error CS1705: Assembly 'Infrastructure' with identity
    'Infrastructure, Version=1.0.0.0' uses 'System.Runtime, Version=8.0.0.0'
    which has a higher version than referenced assembly 'System.Runtime'
    with identity 'System.Runtime, Version=4.1.2.0'

Retargeting the csproj to `net9.0` and `cores/load_peripherals.py`'s DLL
path to match (`tools/renode/fetch-renode-rp2040.sh` does both, then runs
`dotnet build`) fixes this without touching any peripheral model; the build
then succeeds with 0 errors, 23 warnings (unused-field and hides-inherited-
member warnings inside the vendored peripherals, none touched by this
port).

Renode was evaluated first per the task brief's candidate order, and it
worked well enough to stop there -- QEMU and the Unicorn fallback were not
attempted.

### 2. QEMU (ruled out without building anything)

`qemu-system-arm --version` reports 11.1.1 (already installed).
`qemu-system-arm -M help` lists no RP2040 or Raspberry Pi Pico machine type.
A web search surfaced an unmerged RFC patch series ("[PATCH RFC v2 00/30]
arm: add Raspberry Pi...") proposing Pico/RP2040 support for QEMU; it is not
in any released QEMU, including 11.1.1, so this candidate was set aside
without a bring-up attempt, per the brief's 15-minute budget for a "generic
Cortex-M0 machine, bent to fit" probe -- Renode's exact, pre-built model
made that probe unnecessary.

### 3. Unicorn fallback (not attempted)

Not needed: Renode reached a real, informative boot -- see below.

## How far the kernel boots

Console output, `PICO_UART` config, captured verbatim while
`tools/renode/boot.resc` ran, with `PYTHON=${PYTHON:-python3}` then
`$PYTHON tools/renode/console.py --port 3456` (this exact transcript
reproduced byte-for-byte across two separate runs):

```

DiscoBSD 2.7 (PICO_UART) #1 818: Fri Sep 11 17:41:10 PDT 2026
     eirikr@x570-5600X3D:/sys/arch/rp2040/compile/PICO_UART
cpu: RP0000 rev 0, manufacturer 0x000
cpu: Cortex-M0+, ARMv6-M, no MMU and no MPU
cpu: 125 MHz core, 125 MHz peripheral
fl0: 950 kbytes on QSPI flash, 1472 kbytes raw
fl0a: partition type b7, sector 2, size 949 kbytes
fl1: 384 kbytes raw QSPI flash for swap
phys mem  = 264 kbytes
user mem  = 96 kbytes
root dev  = (0,1)
swap dev  = (0,8)
root size = 949 kbytes
swap size = 384 kbytes
```

This proves, in Renode, the same things BOOT-MAP.md section 7 lists for
real hardware up through the swap line: boot2, XIP, clock bring-up
(`SystemClock_Config`'s XOSC/PLL/watchdog-tick sequence completes rather
than spinning forever), the real boot ROM's function table lookup (`fl0`
only prints once `rom_func_lookup` has resolved 'IF' and the erase/program
pointers and Dhara has resumed its journal through them), and UART0 output.

Past that line the console goes quiet, and no run recorded here has yet
reached `login:`. `dotnet`'s CPU usage stays pinned near 150% (`ps aux` on
the Renode process), so the emulated CPU is executing rather than stopped.
GDB catches it inside the boot ROM:

    (gdb) print/x $pc
    $1 = 0x17ce
    (gdb) print/x $r12
    $2 = 0x4001801c
    (gdb) print/x $r6
    $3 = 0x0

0x17ce is inside the boot ROM image (`bootroms/rp2040/b2.elf`, loaded at
0x0), in `flash_put_get`:

    17c4:	4666      	mov	r6, ip
    17c6:	27c0      	movs	r7, #192	@ 0xc0
    17c8:	6836      	ldr	r6, [r6, #0]
    17ca:	02bf      	lsls	r7, r7, #10
    17cc:	423e      	tst	r6, r7
    17ce:	d0d5      	beq.n	177c <flash_put_get+0xc>

`ip` (r12) holds `0x4001801c` throughout the spin: `IO_QSPI_BASE + 0x1c`,
the CTRL register of QSPI pin 3 (SD1) in the RP2040's IO_QSPI pad bank
(pins are 8 bytes apart, STATUS then CTRL, so pin 3's CTRL sits at
`0x18 + 4`), and the mask is `0xc0 << 10 == 0x30000`, the INOVER field at
bits 16-17.

These five instructions are `flash_was_aborted()`, and the branch runs the
way the source says it should. raspberrypi/pico-bootrom-rp2040,
`bootrom/program_flash_generic.c`, reads:

    int flash_was_aborted() {
        return *(io_rw_32 *) (IO_QSPI_BASE +
            IO_QSPI_GPIO_QSPI_SD1_CTRL_OFFSET)
               & IO_QSPI_GPIO_QSPI_SD1_CTRL_INOVER_BITS;
    }

    void flash_put_get(const uint8_t *tx, uint8_t *rx, size_t count,
        size_t rx_skip) {
        const uint max_in_flight = 16 - 2;
        size_t tx_count = count, rx_count = count;
        while (tx_count || rx_skip || rx_count) {
            uint32_t tx_level = ssi_hw->txflr;
            uint32_t rx_level = ssi_hw->rxflr;
            bool did_something = false;
            if (tx_count && tx_level + rx_level < max_in_flight) { ... }
            if (rx_level) { ... }
            if (!did_something && flash_was_aborted()) break;
        }
        flash_cs_force(OUTOVER_HIGH);
    }

SD1's INOVER is the abort flag `flash_abort()` sets; nothing in the kernel
calls it, so it reads back clear, and the `beq` at 0x17ce takes the branch
back to the loop top. That is the loop continuing normally, not a poll
waiting on a value that never arrives. An earlier pass of this report read
the branch the other way and named RP2040QspiPads as the model at fault; it
is not, and this section supersedes that reading.

The obvious next inference, that `tx_count` and `rx_count` therefore never
reach zero, is also wrong, and a later pass measured it rather than
reasoning about it. With the CPU paused inside `flash_put_get` the SSI
registers read:

    CTRLR0 0x18000000 = 0x00070000  DFS_32 7, TMOD 0 (TX_AND_RX), FRF 0
    SSIENR 0x18000008 = 0x00000001
    BAUDR  0x18000014 = 0x00000006
    TXFLR  0x18000020 = 0x00000000
    RXFLR  0x18000024 = 0x00000004
    SR     0x18000028 = 0x0000000E  TFNF | TFE | RFNE

An empty transmit FIFO with four bytes waiting in the receive FIFO is a
transfer draining normally. The chip select framing works too: with
`sysbus.gpio_qspi` and `sysbus.xip_ssi.xip_flash` at noisy level the log
shows `0x06` WriteEnable recognized, GPIO1's output override raising and
lowering the line, `CS# deasserted` reaching the flash model, then `0x02`
PageProgram decoded with `write enabled: True`, three address bytes and the
data. `RP2040GPIO`'s OUTOVER write callback does reach the connected
peripheral, so the ROM's `flash_cs_force` frames its commands the way
silicon does.

The CPU sits in `flash_put_get` because that is the ROM's byte pump and it
is where a flash-heavy kernel spends its cycles, not because the loop is
stuck. What the kernel is doing while the console is quiet resolves, under
`cpu0 LogFunctionNames`, to `namei`, `iget`, `bread` through
`dhara_map_read`, `exec_check` into `exec_hsaout_check`, then thousands of
`get_bits` and `hsx_expand` calls interleaved with `flash_swap_append` and
`SysTick_Handler`: an execve of a compressed a.out being written to raw
swap. `lbolt` advances about 990 ticks per virtual second against an HZ of
1000, `time.tv_sec` increments, and `proc[1]` moves from SRUN to sleeping
on `selwait` and then on `&proc[1]`, which is init sleeping and then
waiting on a child. The premise this report opened with, that the kernel
hangs, came from runs bounded by host wall clock rather than by virtual
time: the longest reached 1.25 virtual seconds.

The earlier runs stopped at slightly different
points (`$pc` 0x17ce one run, 0x17ca and 0x17cc another,
`u.u_procp->p_pid` 2 in one run, 0 in another), all consistent with
reaching this same ROM routine from different call sites. One run's log
also showed 271
`xip_ssi.xip_flash: Writing to address 0x900lo exceedes its size` errors
(addresses 0x900005 through 0x900113) just before the console went quiet; a
second, otherwise identical run reached the same point with no such errors
logged at all, which is the run-to-run variation the managed transfer
thread produces rather than a fault in the boot path.

Three log messages read as faults and are not. `Unhandled operation: 0xFF`
comes from the ROM's `flash_exit_xip`, which clocks 0xff bytes on purpose
to break a QSPI continuation mode the model has no command for.
`Transmission finished in unexpected state: RecognizeOperation` comes from
`RP2040XIPSSI` signalling chip select from its SSIENR and SER write
callbacks as well as through the pad, so the flash model takes a redundant
deassert while already idle. `Unhandled operation while processing byte:
0x7` is a real model gap: `W25QXX.HandleCommand` has no case for
`ReadRegister`, so every status read returns zero and the ROM's
`flash_wait_ready` sees the write-in-progress bit clear forever. That makes
the model finish sooner than silicon, never later, so it cannot stall a
boot; it is worth reporting upstream along with `EraseChip` zeroing its
memory where `EraseBytesInRange` fills with 0xff.

What remains open is how far the console gets. Fifteen lines arrive by 6.8
virtual milliseconds, ending at `swap size = 380 kbytes`, and then the
system does a long stretch of decompression and swap writes that prints
nothing. During that stretch Renode advances roughly one virtual
millisecond per sixty of host wall clock, so reaching the nine seconds
section 7 of BOOT-MAP.md measures on hardware costs minutes rather than
seconds here. A gate built on this emulator has to assert on console
content and budget for that, and no run recorded in this report has yet
crossed the quiet stretch.

## Exact, replayable commands

Build (see BOOT-MAP.md section 12 for the fuller version with the
known build hiccups noted inline):

    cd <tree root>
    bmake MACHINE=rp2040 tools
    tools/bin/binstall -U tools/config/config tools/bin/config
    cd sys/arch/rp2040/compile/PICO_UART && ../../../../tools/bin/config Config && cd -
    bmake MACHINE=rp2040 kernel
    bmake -k MACHINE=rp2040 build
    bmake -C etc MACHINE=rp2040 DESTDIR=$PWD/distrib/obj/destdir.rp2040 distribution
    bmake MACHINE=rp2040 fs
    bmake MACHINE=rp2040 flash

Launch:

    cd tools/renode
    sh fetch-renode-rp2040.sh
    renode --disable-gui -P 4567 -e "include @boot.resc"

Console:

    PYTHON=${PYTHON:-python3}
    $PYTHON tools/renode/console.py --port 3456

GDB attach:

    arm-none-eabi-gdb -q \
        -ex "file sys/arch/rp2040/compile/PICO_UART/unix.elf" \
        -ex "target remote localhost:3333"

## Worked GDB example

With the emulator paused in the boot ROM byte pump above, a fresh
`arm-none-eabi-gdb` session against the same running instance:

    $ arm-none-eabi-gdb -q -batch \
        -ex "file sys/arch/rp2040/compile/PICO_UART/unix.elf" \
        -ex "target remote localhost:3333" \
        -ex "print/x \$pc" \
        -ex "print u" \
        -ex "print *u.u_procp" \
        -ex "break arm_fault" \
        -ex "detach"

    $1 = 0x17cc
    $2 = {u_procp = 0x2001ae8c <proc>, u_frame = 0x0, u_comm = '\000' ...,
          ..., u_ru = {ru_utime = 1, ru_stime = 26, ru_nswap = 7,
          ru_inblock = 174, ...}, ...}
    $3 = {p_nxt = 0x0, p_prev = 0x2001aef0 <proc+100>, p_pptr = 0x0,
          p_flag = 3, p_uid = 0, p_pid = 0, p_ppid = 0, p_sig = 0,
          p_stat = 3, ...}
    Breakpoint 1 at 0x1000f9e6: file
        ../../../../arch/rp2040/rp2040/fault.c, line 99.

"Current process" in this 2.11BSD-derived kernel is `u.u_procp`
(`sys/sys/user.h`: `struct user { ...; struct proc *u_procp; ...} u, u0;`),
a pointer into the reachable `proc` array rather than a separate `curproc`
global. `p_pid == 0` here is process 0, the swapper context, which this
early sample caught while `dhara_map_resume()` ran during `config()`'s
device probe. A later sample taken further into the same boot finds
`proc[1]` sleeping on `selwait` and then waiting on a child, which is init
running. The fault handler, `arm_fault` at
`sys/arch/rp2040/rp2040/fault.c:99`, is not reached, but the breakpoint
sets cleanly and would trigger on any genuine fault taken later in boot --
confirmed by GDB resolving the exact source line rather than only an
address, meaning debug info from this kernel build is present and correct
end to end.

Renode's own monitor answers the same questions without GDB, and a
free-running target needs no async-continue plumbing there:
`sysbus.cpu0 PC` and `sysbus ReadDoubleWord <addr>` against symbol
addresses from `arm-none-eabi-nm` on `unix.elf` read out `lbolt`, `time`
and any `proc[]` field.

A kernel-side bug independent of the emulator was found and fixed along the
way: `arm_fault()`'s nested-fault path (`fault.c:115`, before this change)
called `usbpoll()` unconditionally, which does not exist when
`UARTUSB_ENABLED` is undefined -- the link for `PICO_UART` failed with
`undefined reference to 'usbpoll'` until that call was put behind
`#ifdef UARTUSB_ENABLED`. This is a real defect in any UART-only RP2040
build, on real hardware or emulated, not a workaround for this report.

## Tool versions

| Tool | Version |
|---|---|
| Renode | 1.17.0 (1.17.0+20260907gitf1dd1b4af), runtime .NET 9.0.19 |
| Renode_RP2040 | commit `205a5e4b25440582008a4292074bb07f80a72328` |
| dotnet SDK | 10.0.111 |
| qemu-system-arm | 11.1.1 (checked, not used) |
| arm-none-eabi-gcc | 16.2.1 20260810 |
| arm-none-eabi-gdb | (system package; `target remote` protocol verified working) |
| bmake | system package (NetBSD make) |

## What is not modeled or is honestly incomplete

- **USB is not modeled at all** in Renode_RP2040 ("USB: not supported" in
  its own README). This report uses a UART0-console kernel build
  (`PICO_UART`) instead of the board's default USB CDC-ACM console
  (`PICO`) for exactly this reason; the `PICO` config was not run under
  Renode.
- **SysInfo is not modeled.** `SYSINFO_CHIP_ID` reads back 0, so
  `cpu: RPxxxx rev N` prints as `RP0000 rev 0`.
- **The flash path works.** Reads through the XIP window execute the
  kernel's `.text` and `.rodata` and serve `dhara_nand_read()`; the boot
  ROM's own erase and program calls frame their chip select through the
  IO_QSPI pad override and the flash model receives it. The SSI drains its
  FIFOs. No model change is needed to get past `swap size = 380 kbytes`.
- **`W25QXX` answers every status-register read with zero**, because
  `HandleCommand` has no `ReadRegister` case even though the model builds a
  status register in its constructor. The ROM's `flash_wait_ready`
  therefore never sees the write-in-progress bit set, which makes the model
  faster than silicon rather than slower. `EraseChip` also zeroes its
  memory where `EraseBytesInRange` fills with 0xff; DiscoBSD issues neither
  0x60 nor 0xc7, so that one does not reach this port. Both belong upstream.
- **Runs are not bit-reproducible.** `RP2040XIPSSI` drives transfers from a
  managed thread rather than from the bus access, so the interleaving with
  the CPU depends on host timing: two replays of one script put the same
  page program at different flash offsets and different virtual times. A
  gate has to assert on console content, never on timings or addresses.
- **PWM, RTC, and DMA-adjacent IRQ/DREQ wiring for SSI are unfinished**
  per Renode_RP2040's own table; none of these are on the boot path this
  report exercises, so their absence was not independently confirmed here.
- **SIO multicore dividers are partial**; DiscoBSD runs single-core on
  cpu0 only, so this was not exercised.
- **QEMU was not built or patched against**; the unmerged RFC series found
  by web search was not fetched or evaluated beyond confirming it is
  unmerged.
- **The Unicorn/Capstone fallback in tools/rp2040emu/ was never started.**
  Renode succeeded well enough (real boot ROM, real UART, real clock
  bring-up, GDB attach with correct kernel debug info) that the task's
  stopping rule ("stop at the first candidate that gets the kernel to a
  login prompt" -- read here as "the furthest, most informative boot") was
  met without it.
- **What the console prints after the swap-size line is not established.**
  Every run recorded here was bounded by host wall clock rather than by
  virtual time, and Renode advances roughly one virtual millisecond per
  sixty of host wall clock through the flash-heavy stretch, so crossing the
  nine seconds a board takes to reach `login:` costs minutes. Establishing
  it means an `emulation RunFor` long enough to finish, run detached, with
  the UART captured by `LoggingUartAnalyzer` into the log file.
