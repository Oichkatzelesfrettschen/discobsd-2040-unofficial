# DiscoBSD on the Pico: the map from source to a shell prompt

The port lives in this DiscoBSD tree on branch `rp2040-port`,
branch `rp2040-port`. Machine-readable copies of the RP2040 and Pico
datasheets are under `docs/rp2040/`, with `INDEX.md` resolving every section
number to a line in the text. Section numbers below refer to the RP2040
datasheet unless marked Pico.

## 1. What the boot ROM needs from flash

The ROM, fixed in silicon, reads 256 bytes from flash offset 0 with a plain
`03h` serial read, computes CRC32 over the first 252 with polynomial
0x04c11db7, initial value 0xffffffff, unreflected, no final XOR, and compares
it to the little-endian word in the last four bytes (2.8.1.3.1). On a match
it copies the 256 bytes to 0x20041f00 and jumps to them with `lr` = 0. On
128 misses it drops to the USB bootloader, which is why a board with no valid
image always lands in BOOTSEL and nothing here can brick it.

The second stage configures the SSI for quad-I/O continuous read (4.10),
writes VTOR = 0x10000100, loads SP and PC from the two words there, and
jumps. From that instant the kernel runs from flash through the XIP window
at 0x10000000 (2.6.3).

Port artifacts:

| Piece | File | Verified by |
|---|---|---|
| Second stage | `sys/arch/rp2040/boot2/boot2_w25q080.S` | assembles byte-identical to pico-sdk's `bs2_default.bin` for `PICO_BOARD=pico` |
| Its link address | `sys/arch/rp2040/boot2/boot2.ld` | 0x20041f00, 252 bytes |
| Pad and checksum | `tools/boot2sum/boot2sum.c` | output identical to pico-sdk `pad_checksum -s 0xffffffff` on the same input |
| Placement | `sys/arch/rp2040/conf/kern.ldscript` `.boot2` at `BOOT2` | `objdump -h`: `.boot2` at 0x10000000, `.text` at 0x10000100 |

## 2. Memory map

| Region | Address | Size | Why |
|---|---|---|---|
| boot2 | 0x10000000 | 256 | ROM contract |
| kernel text and rodata | 0x10000100 | 512K minus 256 | `RP2040.ld` FLASH |
| root and swap, Dhara journal | 0x10080000 | 1536K | `RP2040.ld` FSFLASH, `flash.h` FLASH_FS_OFFSET |
| user space | 0x20000000 | 96K | `lib/elf32-arm.ld` links every executable at 0x20000000; `USER_DATA_START` in `machparam.h` |
| kernel data, bss, RAM-resident flash writers | 0x20018000 | 152K | `RP2040.ld` RAM |
| process 0 u area | 0x2003e000 | 4K | U0AREA |
| current u area and stack | 0x2003f000 | 4K, `_estack` = 0x20040000 | UAREA |
| SRAM4, SRAM5 | 0x20040000 | 8K | untouched; ROM stages boot2 in SRAM5 |

The kernel is 85,807 bytes of flash and 25,664 of RAM.

## 3. Clocks and ticks, in the order the kernel does them

`startup()` in `rp2040/machdep.c`:

1. XOSC: startup delay 47 (about 1 ms at 12 MHz), enable, wait STABLE (2.16).
2. clk_ref from XOSC (2.15).
3. Watchdog TICK register: enable, 12 cycles, giving the 1 MHz tick (4.7.2).
   The timer does not count without it (4.6.4), and `mdelay` spins on the
   timer. Then release TIMER and SYSINFO from reset (2.14).
4. PLL_SYS: FBDIV 125, POSTDIV1 6, POSTDIV2 2, giving 12 x 125 / 12 = 125 MHz;
   power up, wait LOCK (2.18).
5. clk_sys onto the PLL through the aux mux, then clk_peri enabled from
   clk_sys (2.15), which the UART divides.
6. IO_BANK0 and PADS_BANK0 out of reset; GP25 as SIO output for the LED.

`clkstart()` in `rp2040/clock.c`, called from `init_main`, arms SysTick from
the processor clock with reload CPU_KHZ x 1000 / HZ - 1 for 1 ms ticks
(2.4.5.1.1). Nothing else in the port starts SysTick; the STM32 tree got it
from the ST HAL.

## 4. Root filesystem, three layers

| Layer | Tool or code | Output |
|---|---|---|
| filesystem | `tools/fsutil --repartition=fs=795k:swap=192k`, then `--new --partition=1 --inodes=160 --manifest` | `distrib/rp2040/sdcard.img`, a PC partition table and a 2.11BSD filesystem |
| translation layer | `tools/flashimg`, vendored Dhara over a memory model of the region | `distrib/rp2040/flash.bin`, the 1536K region |
| flash | `picotool uf2 convert -o 0x10080000` | `distrib/rp2040/flash.uf2` |

Sizes come from `tools/bin/flashimg -c`: with 1 KB units in 8 KB erase blocks
and gc ratio 4, Dhara leaves 989 KB of logical blocks. `Makefile.inc` takes
795 for root and 192 for swap, two 96K process images; the 1 KB left is the
partition table. The root holds 76 objects in 511 KB with 284 KB and 84
inodes free. `flash.bin` was resumed by a separate Dhara instance and every
sector read back identical to `sdcard.img`.

The kernel side, `dev/flash.c`: `fl0` with the SD driver's minor numbering,
partition table read from logical block 0, `fl0a` root, `fl0b` swap, reads
through the XIP window, erase and program through the boot ROM from
RAM-resident functions with interrupts masked, cache flushed afterwards.

## 5. Build, from a clean checkout

    cd <tree root>
    bmake MACHINE=rp2040 tools
    bmake MACHINE=rp2040 build
    bmake MACHINE=rp2040 kernel
    bmake -C etc MACHINE=rp2040 DESTDIR=$PWD/distrib/obj/destdir.rp2040 distribution
    bmake MACHINE=rp2040 fs
    bmake MACHINE=rp2040 flash

Host requirements: bmake, arm-none-eabi-gcc, picotool 2.3.1 (the 2.3.0 in the
AUR aborts on every RP2040; the PKGBUILDS monorepo carries 2.3.1), groff
(mandoc conflicts with man-db on this host), vim (stands in for `ex`).
`make build` still reports failures in uucp's install, two manual page
names, and nothing the root uses; they predate this port.

## 6. Flash and connect

1. Hold BOOTSEL, plug the Pico in; it enumerates as `2e8a:0003`.
2. `picotool load distrib/rp2040/flash.uf2`
3. `picotool load sys/arch/rp2040/compile/PICO/unix.uf2`
4. `picotool reboot`
5. `minicom -D /dev/ttyACM0`

The console is the same cable. After the reboot the board re-enumerates as
a CDC-ACM device, `2e8a:000a`, and Linux attaches it as `/dev/ttyACM0`. The
kernel keeps the last 4 KB of output in a ring until a terminal opens the
port, so the boot messages are readable after the fact. No serial adapter
is needed; UART0 on GP0 and GP1 stays available as `/dev/tty0`.

Getting back to BOOTSEL: `picotool reboot -u` asks the kernel's reset
interface, which calls the ROM's USB-boot entry. A hung kernel needs the
button held through a power cycle, and that always wins, because the ROM
checks the button before it looks at flash (2.8.1). Holding the button at
reset with a working kernel selects single-user mode.

## 7. What the first lines on the console prove

| Console output | Proves |
|---|---|
| `/dev/ttyACM0` appears | boot2, XIP, clocks, PLL_USB, the USB device controller, enumeration |
| any character at all | the bulk IN path, `cnputc`, the output ring |
| `cpu: RP2040 rev 2` | SYSINFO out of reset, chip id decode |
| `fl0: 989 kbytes on QSPI flash` | boot ROM table lookup, Dhara resume of the flashed journal |
| `fl0a: partition type b7` and `fl0b: type b8` | partition table read through Dhara |
| `phys mem`, `swap size = 192 kbytes` | `flsize` on `fl0b` |
| `Automatic boot in progress` | init exec'd from the root: a.out loader, user RAM window, syscalls, SysTick |
| `login:` | getty on `/dev/console`, tty layer, UART interrupts |

## 8. Triage ladder

| Symptom | First suspect | Check |
|---|---|---|
| board returns to BOOTSEL on every power-up | boot2 checksum or placement | `picotool info -a` sees the image; `objdump -s -j .boot2 unix` ends in the CRC |
| LED never blinks, no console | clock bring-up hangs in a wait loop, or VTOR | SWD through a second Pico with openocd and `arm-none-eabi-gdb`; read PC |
| no `/dev/ttyACM0`, `lsusb` shows nothing | PLL_USB or clk_usb, or enumeration | `dmesg` for "device descriptor read" errors; then a serial adapter on GP0 and GP1 to read the kernel's messages |
| `ttyACM0` appears but stays silent | output ring never armed, DTR handling | open the port with a terminal, not `cat`; check `SET_CONTROL_LINE_STATE` handling in `dev/usb.c` |
| HardFault early | a Thumb-2 encoding, an unaligned word access | the fault frame's PC in the `.dis` file |
| `panic: swap size` | `fl0b` missing or partition table unread | `fl0` lines above it |
| `panic: root` or init not found | Dhara resume failed, or wrong gc ratio | `flashimg -c` and `FLASH_GC_RATIO` must match what wrote the chip |
| init runs, then faults | userland built for the wrong CPU, or user window | `arm-none-eabi-readelf -A` on a binary shows `Tag_CPU_arch: v6S-M` |
| `mdelay` never returns | watchdog tick not running | TICK register bit 10 RUNNING |

SWD: a second Pico running the debugprobe firmware, `openocd -f
interface/cmsis-dap.cfg -f target/rp2040.cfg`, then gdb on `unix.elf`.

## 9. Resolved in this pass

- boot2 absent: added, verified identical to the SDK's.
- kernel and user RAM inverted relative to the userland link address:
  user space now at 0x20000000, kernel above it.
- timer never counted: watchdog tick started, TIMER and SYSINFO released.
- SysTick never armed: `clkstart` writes it.
- `fl0a` and `fl0b` rejected: partition table support in `dev/flash.c`.
- Dhara wasted half the flash: 1 KB units in 8 KB blocks, 571 KB to 989 KB.
- no way to put a filesystem into flash: `tools/flashimg`, verified by
  read-back.
- fsutil could not give a small root enough inodes: `--inodes`.
- userland built for Cortex-M4: `MACHINE_CPU` in `sys.mk`.
- GCC 16 C23 default broke the whole tree: `-std=gnu17` on CC.
- mandoc and ex absent on this host: groff and vim fallbacks.
- 200 MB manifest against a 795 KB root: `mi.rp2040`, `md.rp2040`,
  `rc.rp2040`, `fstab.rp2040`.

- no console without a serial adapter: `dev/usb.c`, a CDC-ACM device with
  the Pico SDK reset interface, verified only by walking its compiled
  descriptors.
- BOOTSEL unreadable at runtime: `bootsel_pressed()` in `machdep.c`.
- the UART cdevsw slot was empty: `conf.c` tested the STM32 unit names.

## 10. Debugging without a probe

There is no SWD probe on this bench, so OpenOCD and a live gdb are not
available; the RP2040's own USB is the console and the reset interface.
What found every kernel and libc bug so far:

- The fault report. `arm_fault` prints the exception frame, r4-r11, the
  process, the syscall number, and the user frame's registers. Callee-saved
  registers holding user values named the setjmp clobber; r6 equal to r10
  named the staging bug; ASCII zeros in r6 named the cvt overrun; the high
  word of 7.0 in r4 named the modf alias.
- `arm-none-eabi-addr2line -f -e unix <pc>` on the kernel ELF, and the same
  on a program ELF relinked with the exact command `bmake -n` prints; a
  `-g` relink must drop `-g` from the link line or the GCC driver pulls in
  newlib's `libg.a` beside the tree's libc.
- `arm-none-eabi-objdump -d` around the address, and `nm -n` to map an
  address to the nearest symbol when no line table exists.
- `flashimg -c` and a host resume of the emitted image to prove the
  filesystem layer before it reaches the board.
- Static tools now installed for the tree: `cppcheck` and GCC's
  `-fanalyzer` on the drivers, `lizard` for complexity (usb_setup and
  flstrategy are the two functions over 25 CCN), `cscope` and `ctags` for
  navigation, `cloc` and `scc` for size.

## 11. Still open

The board boots to a root shell over USB in 9 seconds; ls, pipes, sed,
sort, awk, picoc, df, mount, ps -ax, and the swap path all run. Open:

- picoc runs a program with its 8 KB arena; a larger arena
  (`STACKSIZE=`) or `-s` script mode overruns the 96 KB window, which the
  kernel now ends with a memory fault rather than a flood.
- `ps` without arguments lists nothing because it filters on the terminal
  name; `ps -ax` shows every process.
- The UART receive path has never been exercised.
- The native Thumb-1 toolchain (assembler, linker relocations, Smaller C
  backend) is in progress in two worktrees, `thumb-as-ld` and
  `smlrc-thumb`, and has not been merged.
