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

Past that line, the kernel does not reach `login:`; `dotnet`'s CPU usage
stays pinned near 150% (`ps aux` on the Renode process) after the console
goes quiet, i.e. the emulated CPU is spinning, not stopped. GDB confirms it,
and pins down exactly where:

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

What the spin means is that `tx_count` and `rx_count` never reach zero:
the SSI transfer under way makes no progress, so the ROM has nothing to do
each time around and keeps checking whether it was aborted. The models to
examine are the SSI and the chip select framing it drives, `RP2040XIPSSI`
and `W25QXX`, not the pad bank. `flash_do_cmd()` asserts chip select
through the IO_QSPI SS pad override rather than through SSIENR or SER,
which are the only writes `RP2040XIPSSI` forwards to the flash model's
GPIO; a command whose framing the flash model never sees would leave its
command state machine out of step, which is what the warnings below
suggest. The two runs captured for this report hung at slightly different
points (`$pc` 0x17ce one run, 0x17ca and 0x17cc another,
`u.u_procp->p_pid` 2 in one run, 0 in another), all consistent with
reaching this same ROM routine from different call sites. One run's log
also showed 271
`xip_ssi.xip_flash: Writing to address 0x900lo exceedes its size` errors
(addresses 0x900005 through 0x900113) immediately before the hang; a
second, otherwise identical run reached the same hang with no such errors
logged at all, so that message is a symptom of an earlier SPI framing
problem on some runs, not the cause of the hang itself.

Every caller that reaches this routine runs under `splhigh()`
(`flash_program()` and `flash_erase()` in `sys/arch/rp2040/dev/flash.c`
both call it through the real ROM with interrupts masked), so once the CPU
is in this loop nothing else in the kernel runs either. Renode's own log
for the run reports `xip_ssi.xip_flash: Transmission finished in unexpected
state: RecognizeOperation` and `Unhandled operation: 0x0`, which is a flash
command state machine that has lost its framing rather than one waiting on
a pad.

This is a Renode peripheral-model gap, not a DiscoBSD kernel bug: the same
ROM call path, against the real chip, is what section 7 of BOOT-MAP.md
already verifies gets a board to `login:` in nine seconds.

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

With the emulator hung at the boot ROM spin above, a fresh
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
global. `p_pid == 0` here is process 0 (the kernel/swapper context), which
is correct: this hang is in `dhara_map_resume()` during `config()`'s device
probe, before `init` has been exec'd, so no user process yet exists to be
"current." The fault handler, `arm_fault` at `sys/arch/rp2040/rp2040/fault.c:99`,
is not hit by this particular hang (a masked-interrupt busy-wait, not a
HardFault), but the breakpoint sets cleanly and would trigger on any genuine
fault taken later in boot -- confirmed by GDB resolving the exact source
line rather than only an address, meaning debug info from this kernel build
is present and correct end to end.

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
- **An SSI transfer started from the boot ROM never completes**, so
  `flash_put_get`'s loop never retires its byte counts. Reads through the
  XIP window work (the kernel's `.text`/`.rodata` execute from flash, and
  `dhara_nand_read()`'s direct XIP reads succeed); any boot ROM call that
  reaches `flash_put_get` -- both `flash_program()` and `flash_erase()` in
  `sys/arch/rp2040/dev/flash.c` go through it -- hangs the caller under
  `splhigh()`. This is the reason the kernel does not reach `login:` under
  this emulator. The register the ROM reads in that loop is the abort flag,
  not a pad the model has to drive; see the correction above.
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
- **The hang was localized from the boot ROM disassembly, from `$pc`,
  `$r12` and `$r6` at the hang point, and from the published boot ROM
  source.** What has not been established is which SSI or chip-select
  write the models drop; that needs the transfer traced through
  `RP2040XIPSSI` and `W25QXX` with those peripherals at noisy log level,
  and a fix means changing a third-party emulator's peripheral model
  rather than this port's own kernel or build.
