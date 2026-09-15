# A PDP-11 running Sixth Edition UNIX inside a DiscoBSD process

Target: DiscoBSD 2.7 on a Raspberry Pi Pico, RP2040, Cortex-M0+
(ARMv6-M, Thumb-1, no MMU, no hardware divide, 264 KB SRAM, 2 MB QSPI
flash). The question is whether a PDP-11 emulator booting Sixth Edition
UNIX runs as an ordinary user program on that system, and what runs if
SIMH does not.

Confirmed = read from a primary source or measured in this session.
Estimated = derived from a confirmed measurement, with the basis stated.

## Summary

| Question | Finding | Status |
| --- | --- | --- |
| User window | 144 KB; 160 KB under the LARGE epoch | Confirmed |
| Stock SIMH PDP-11 | Does not fit, and does not compile | Confirmed |
| SIMH five-file floor | 127,337 bytes before any core | Measured |
| Full 11/40 core | 248 KB, alone 1.72x the window | Confirmed |
| V6 minimum user core | 24 KB, not 128 KB | Confirmed (V6 setup doc) |
| avr11 core, Thumb-1 -Os | 6,548 text, 66 data, 176 bss | Measured |
| Recommended shape | avr11 in C, 64 KB core in `.bss` | Recommended |
| Disk image | 2.4 MB RK05 against 314 KB free | Confirmed |
| Spare flash | None; 128 + 1536 + 384 = 2048 KB | Confirmed |
| SD card on rp2040 | No driver; `pic32` has one to port | Confirmed |
| Build with smlrc | Blocked; avr11 is C++ | Confirmed |
| Bare-metal dual boot | All 264 KB, a full 248 KB 11/40 | Recommended |

## Method and provenance

Three repositories were fetched over the network in this session:

- open-simh at `a1f57fa3738ed31148d31126ba1a7278ff845c6d`
  (<https://github.com/open-simh/simh>).
- avr11 at `e8cadec088e6659abfe77beddcf32b14b587facb`
  (<https://github.com/davecheney/avr11>).
- unix-history-repo, branch `Research-V6`, at
  `4b87ee08354dc081ad897853173e6f7f4b52c116`
  (<https://github.com/dspinellis/unix-history-repo>).

Sizes come from `arm-none-eabi-gcc` and `arm-none-eabi-g++` 16.2.0 (Arch
repository) with `-c -Os -mcpu=cortex-m0plus -mthumb -w`, measured by
`arm-none-eabi-size`. The C++ compilations add `-fno-exceptions
-fno-rtti`. The avr11 figures are built against a hand-written
`Arduino.h` shim whose `Serial` methods are empty inline stubs and a stub
`SdFat.h`, so every avr11 number is a lower bound on a real port: the
console and disk back ends are replaced by nothing.

Board figures come from the port tree read-only and from
`research/discobsd-rp2040/board-inventory-2026-09-15.txt`.

The 96 KB per-process figure in `ondevice-languages.md`,
`ram-compression.md`, `storage-techniques.md` and the port tree's
`usr.bin/smlrc/README.rp2040.md` is stale. `sys/arch/rp2040/include/
machparam.h` sets `USER_DATA_SIZE (144 * 1024)` and `MAXMEM` to the
same value, and `sys/arch/rp2040/conf/RP2040.ld` states 144K for the
`USERRAM` region; `machdep.c` panics at boot when the two disagree.
Every budget below uses 144 KB.

## 1. The window, the flash, and the swap map

`sys/kern/exec_subr.c`'s `exec_estab` sums text, data, bss, heap and
stack and refuses an image whose total exceeds `MAXMEM`. One image past
`MAXMEM` but within `MAXMEM + SWAPRAM_BONUS` asks for the LARGE epoch,
which calls `exec_spool_to_flash` and `swapram_enter_large`.
`sys/arch/rp2040/include/swapram.h` defines `SWAPRAM_BONUS` as
`SWAPRAM_KB * 1024`, and `sys/arch/rp2040/compile/PICO/Config` sets
`options "SWAPRAM_KB=16"`. The LARGE window is therefore 160 KB exactly,
and the bonus is reachable only by a fresh exec: swapram.h records that a
running process's stack already sits under the plain window, so `brk`
never reaches the bonus. An emulator that wants the bonus must declare
its emulated core as a compile-time array, not `malloc` it.

`RP2040.ld` allocates the whole chip: 128K for boot2 and kernel, 1536K
for the Dhara-backed root, 384K for raw swap. 128 + 1536 + 384 = 2048 KB
= 2 MB. No region is spare for a disk image.

`sys/arch/rp2040/doc/STORAGE.md` records the constraint that decides
between the two windows. A 58 KB multicall binary "linked and ran, and
then could not be swapped: the swap map hands out contiguous runs, and
after a few forks 256 KB of swap held 115 KB free in three pieces, none
of 70 KB." Swap is now 384 KB. A 148 KB image needs a single 148 KB run
in that 384 KB beside `sh` and `init` after fragmentation, which is the
same failure one scale up.

`ram-compression.md` prices the write: 45 ms per 4 KB sector erase. An
88 KB image costs 22 sectors, about 1.0 s per swap out; a 148 KB image
costs 37 sectors, about 1.7 s. The emulator is usable as the process the
operator is talking to and is unusable as one of several.

`sys/sys/exec_hsaout.h` helps on flash and not in RAM: a packed a.out
carries heatshrink streams for text and initialized data only, and
`a_bss` names a length the loader zeroes. An emulated core declared as
`.bss` costs zero bytes of root filesystem and its full size in SRAM.

## 2. SIMH does not fit, and separately does not compile

`PDP11/pdp11_cpumod.c`'s `cpu_tab` gives the 11/40 a `maxm` of
`UNIMEMSIZE`, which `PDP11/pdp11_defs.h` sets to `001000000`, 262,144
bytes; `cpu_set_size` caps usable memory at `maxm - IOPAGESIZE`, and
`IOPAGESIZE` is `000020000`, 8,192 bytes. A full 11/40 is therefore
253,952 bytes, 248 KB, of emulated core, allocated by `pdp11_cpu.c:3441`
as `calloc (MEMSIZE >> 1, sizeof (uint16))`. That single allocation is
1.72 times the 144 KB window and 1.55 times the 160 KB LARGE window,
before any emulator code exists.

The code is worse than the data. Five of the PDP-11 sources cross-compile
cleanly for Cortex-M0+ at `-Os`:

| Object | text | data | bss |
| --- | --- | --- | --- |
| `pdp11_cpu.o` | 31,829 | 12,940 | 924 |
| `pdp11_sys.o` | 7,189 | 612 | 0 |
| `pdp11_cpumod.o` | 6,308 | 4,328 | 344 |
| `pdp11_rk.o` | 6,171 | 3,832 | 40 |
| `pdp11_io.o` | 1,620 | 0 | 51,200 |
| Total | 53,117 | 21,712 | 52,508 |

127,337 bytes, 88 percent of the 144 KB window, for the CPU, the RK05,
the I/O dispatch and the model table alone. `pdp11_io.o`'s 51,200 bytes
of bss are SIMH's Unibus read and write dispatch tables: a third of the
window is framework indirection before one instruction is emulated.

Nothing in that total is SCP, SIMH's command interpreter and device
framework, which every simulator links. `scp.c` is 16,793 lines against
the 7,881 lines of the five files above, whose measured density is 6.74
bytes of Thumb-1 text per line; at that density `scp.c` alone is about
113 KB of text (estimated, and the basis is weak -- command parsing and
string handling are not instruction decode, so treat it as an order of
magnitude, not a figure).

The sum already exceeds the window. The second reason is independent:
`scp.c`, `sim_console.c`, `sim_fio.c`, `sim_disk.c` and `sim_timer.c` all
fail to compile at `sys/socket.h`, reached through `sim_sock.h`.
`PDP11/pdp11_stddev.c`, the console device, fails the same way. SCP
assumes a hosted POSIX with BSD sockets, termios, a filesystem API and a
real-time clock; DiscoBSD's 2.11BSD-derived libc is 36 KB across 89
members and has no sockets at all. Porting SIMH here is not a matter of
trimming devices, it is a rewrite of the framework every device calls.

Stock SIMH does not run as a DiscoBSD user program on this board. That
is settled.

## 3. What V6 actually needs, which is far less than assumed

`usr/sys/param.h` line 10 sets `MAXMEM (64*32)` with the comment "max
core per process - first # is Kw": 64 Kw, 128 KB, is the ceiling on one
V6 process, which is the full 16-bit address space and not a requirement.

The requirement is in the distribution's own setup document,
`usr/doc/start/start`: the boot prints `mem = xxx`, which "gives the
memory available to user programs in .1K units," and "Most of the UNIX
software will run with 120 (for 12K words), but some things require much
more." 12K words is 24 KB of user core. The emulated machine must hold
the V6 kernel plus that, not 128 KB and not 248 KB.

The V6 kernel's own size is bounded by the PDP-11 kernel address space,
64 KB, and `usr/sys` is 211 KB of C and assembly source for the whole
system including three disk drivers. A resident kernel near 40 KB is the
working estimate; the source tree carries no built binary (`usr/sys/run`
is the build script, 900 bytes, not an image), so this number is
estimated and is checkpoint 1 below.

A 64 KB emulated core -- roughly 40 KB kernel, 24 KB user -- boots and
runs the shell and the standard utilities by the distribution's own
statement. 96 KB gives headroom for the C compiler and `ed` on larger
files.

## 4. The minimal emulator: avr11, measured

Dave Cheney's avr11 is a PDP-11/40 emulator running V6 on an ATmega2560
with external SRAM, derived from Julius Schmidt's JavaScript PDP-11
simulator (the README states the derivation and the WTFPL licensing).
The whole tree is 2,757 lines. Compiled for Cortex-M0+ at `-Os` against
the shim described above:

| Object | lines | text | data | bss |
| --- | --- | --- | --- | --- |
| `cpu.cpp` | 1,252 | 4,920 | 58 | 62 |
| `unibus.cpp` | 149 | 612 | 8 | 0 |
| `mmu.cpp` | 116 | 464 | 0 | 68 |
| `rk05.cpp` | 186 | 312 | 0 | 37 |
| `cons.cpp` | 110 | 240 | 0 | 9 |
| Total | 1,813 | 6,548 | 66 | 176 |

6,548 bytes of Thumb-1 text is 4.9 times smaller than `pdp11_cpu.o`
alone and 8.1 times smaller than the SIMH five-file text total. The
reason is scope: `mmu.cpp` is 116 lines because V6 uses KT11 segmentation
with eight kernel and eight user pages, a PAR and a PDR each, and the
emulator needs `decode` and the register window and nothing else. The
`rk05.cpp` back end is 186 lines because V6's RK11 driver issues read,
write, seek and reset against a linear block image.

The console and disk stubs are the honest gap. Restoring them costs the
DiscoBSD side: `read`/`write` on the tty and `lseek`/`read`/`write` on an
image file, which the 2.11BSD libc supplies directly. Budget 2 KB of
glue. avr11's trap path calls `longjmp(trapbuf, INTFAULT)`;
`lib/libc/arm/gen` ships `setjmp.S`, `_setjmp.S` and `sigsetjmp.S`, so
the mechanism ports unchanged.

### The budget, three ways

Static libc for a program using `stdio`, `setjmp` and no floating-point
`printf` (rp2040 omits `doprnt_float` unless `PRINTF_FLOAT=yes`, per
`STORAGE.md`) runs 10 to 15 KB; take 15 KB. Stack: `SSIZE` is 2048 bytes
initially and the emulator's own recursion is shallow; take 8 KB.

| Design | emulator | libc | stack | core | total | window |
| --- | --- | --- | --- | --- | --- | --- |
| (a) 64 KB core | 9 KB | 15 KB | 8 KB | 64 KB | 96 KB | 144 KB, fits |
| (b) 96 KB core | 9 KB | 15 KB | 8 KB | 96 KB | 128 KB | 144 KB, fits |
| (c) 128 KB core | 9 KB | 15 KB | 8 KB | 128 KB | 160 KB | over both |
| (d) full 11/40 | 9 KB | 15 KB | 8 KB | 248 KB | 280 KB | impossible |

Design (a) fits the plain window with 48 KB to spare and never touches
the LARGE epoch. Design (b) fits with 16 KB to spare. Design (c) is the
"V6 with a 128 KB configuration" the question asks about, and it turns
on a rounding: 160 KB is the LARGE ceiling exactly, and `exec_estab`
rejects only `need > swapram_ceiling`, so the image is admitted with
zero bytes to spare and any growth in libc or stack refuses it at exec.
What kills design (c) is the swap map rather than the window. The whole
160 KB must find one contiguous run in 384 KB of swap beside `sh` and
`init`, which is the failure `STORAGE.md` measured at 58 KB. Design (d) is
impossible inside DiscoBSD in one sentence: 248 KB of emulated core alone
exceeds the 160 KB maximum any process can ever be given.

### Paging the core instead of holding it

Option (c) can be bought by keeping only part of the PDP-11 core resident
and paging the rest. The natural unit is the KT11 page, 8 KB, so a cache
of eight pages is 64 KB resident against a 128 KB backing store in the
384 KB swap region or a file on the root. The cost is not the read: XIP
flash reads run at bus speed. The cost is the write. Every dirty page
eviction erases two 4 KB sectors at 45 ms each and reprograms them, about
100 ms, and V6 dirties its user page on every process it runs. A shell
command that touches four pages pays 400 ms of flash per eviction round
and wears the sector. Paging turns a working 64 KB design into a
half-second-per-command 128 KB design; it is not worth it while design
(b) exists.

### The disk image

`usr/sys/dmr/rk.c` sets `NRKBLK 4872`; at 512 bytes a block an RK05 pack
is 2,494,464 bytes, 2,436 KB. The board's root has 314 KB free
(`board-inventory-2026-09-15.txt`: `df` reports 979 blocks, 665 used,
314 available; `STORAGE.md`'s 175 KB figure predates that root). The
image must lose 87 percent of the pack.

The distribution's setup document says the tape holds 12,100 records and
"only the first 4000 512-byte blocks on the disk are significant," and
that the binary disk "is enough to run the system," with sources and
manuals on the second and third packs. So `/usr/source` (3.1 MB in the
history repo) and `/usr/doc` (2.0 MB) are already out of scope. What
remains is `/unix`, `/etc`, `/bin` and `/usr/bin`. A root of the kernel
plus about twenty-five utilities at PDP-11 a.out sizes lands near 220 KB
with its i-list (estimated; the basis is the 64 KB kernel bound and
typical V6 binaries of 2 to 10 KB, and this is checkpoint 3 below).
220 KB against 314 KB free is tight and survivable, and it leaves no room
for the user to write much -- and V6 must write: `/etc/utmp`, `/tmp` and
`/dev` are written on every login, so a read-only image never reaches a
usable shell.

The alternatives are all worse or absent. The 384 KB swap region is live
swap; taking it means the emulator itself cannot be swapped. Shrinking
`FSFLASH` moves the problem rather than solving it, because flash is
fully allocated. An external SPI SD card is the right answer and does not
exist: `sys/arch/rp2040/dev` holds `flash.c`, `flash_swap.c`, `uart.c`
and `usb.c` and nothing else. `sys/arch/pic32/dev/sd.c` and
`sys/arch/stm32/dev/sd.c` are SPI SD drivers in this same tree, so the
work is a port, not an invention, and the Pico has spare SPI pins for it.

## 5. Bare metal instead: the dual-boot route

`dual-boot.md` establishes the mechanism from the RP2040 datasheet: the
boot ROM carries no partition table and no slot list, but it does support
"load directly into SRAM and run," and NuttX's `nshsram` config was built
and measured there linking 100 percent into SRAM with zero bytes of
flash. A bare-metal PDP-11 emulator built the same way owns all 264 KB of
SRAM and the CPU, and is loaded over USB by `picotool load -x` or a
BOOTSEL UF2 drop with no change to the DiscoBSD image at all.

That configuration runs the full 11/40: 248 KB of core plus 9 KB of
emulator is 257 KB against 264 KB, which fits if the emulator holds its
disk image on flash or SD rather than in SRAM and keeps its own stack in
the two unstriped 4 KB banks. It also sidesteps the fully-allocated-flash
problem, because an SRAM-resident image occupies no flash region.

The two routes answer different questions and both are worth having.
Under DiscoBSD you get V6 as an ordinary program that starts from a
shell prompt, coexists with the rest of the root, and is capped at a
64 to 96 KB PDP-11. Bare metal you get the real machine and a reboot to
reach it. The constraints favor bare metal for a faithful 11/40 and
favor the DiscoBSD process for everything else.

## 6. Compiling it

Cross-compiling is straightforward and is what every measurement above
does. `arm-none-eabi-gcc -Os -mcpu=cortex-m0plus`, link against
`lib/elf32-arm.ld` at 0x20000000, convert with `elf2aout`, optionally
pack with `tools/hsaout` into the `EX_HSPACK` container. The emulated
core is `.bss`, so packing it costs nothing and gains nothing.

Building on the board with `smlrc` is blocked before any of smlrc's
documented limits matter: avr11 is C++, using namespaces, default
arguments, `bool`, and member functions, and Smaller C compiles C.
`usr.bin/smlrc/README.rp2040.md` then adds the limits that would apply
after a C translation -- no old-style function definitions, no
function-like macros in smlrc's own preprocessor (the on-device `cc` runs
`cpp` first and builds smlrc with `-DNO_PREPROCESSOR`), no `double`, and
no `interrupt` attribute -- none of which an emulator needs once it is C.
The real obstacle is size: the compiler itself is 48,753 text, 1,372
data and 26,328 bss, and a 1,252-line `cpu.cpp` is near the largest
translation unit `SYNTAX_STACK_MAX` at 3,200 entries accepts. A hand-
written C emulator of about 1,800 lines split across five files compiles
on the board; avr11 as it stands does not.

## 7. Recommendation and checkpoints

Build an avr11-derived PDP-11/40 emulator, translated to C, as a DiscoBSD
user program with a 64 KB emulated core in `.bss`, growing to 96 KB if
the measurements allow.

1. Translate avr11's `cpu`, `mmu`, `unibus`, `rk05` and `cons` to C.
   Replace the `Serial` calls with `read`/`write` on fd 0 and 1 and the
   `SdFat` calls with `lseek`/`read`/`write` on an image file. Expect
   about 1,800 lines and 9 KB of Thumb-1 text.
2. Make the core size a compile-time constant and declare it as a file-
   scope array so it lands in `.bss` and the a.out stays small.
3. Trim a V6 root to the kernel, `/etc`, `/bin` and the working `/usr/bin`
   on a host, write it as a linear RK05-format image, and install it on
   the DiscoBSD root.
4. Boot, and measure.
5. If and only if a full 11/40 is wanted, build the same emulator as a
   bare-metal SRAM-resident image per `dual-boot.md` and load it with
   `picotool load -x`.

The falsifiable checkpoints, in the order they should be measured,
because each one can kill the design:

1. **The V6 kernel's real size.** Boot the trimmed V6 under host SIMH
   with `SET CPU 64K` and read the `mem =` line. The design needs the
   kernel plus 12K words inside 64 KB. If the kernel exceeds about 40 KB,
   design (a) becomes design (b) and the margin goes from 48 KB to 16 KB.
   This is estimated today and everything else depends on it.
2. **The emulator's linked size on DiscoBSD.** Build the C translation
   and run `arm-none-eabi-size`. The prediction is 9 KB text and 15 KB of
   libc; a result past 30 KB of the two together eats the margin.
3. **The trimmed image's size.** Predicted near 220 KB against 314 KB
   free. Past 300 KB, the SD driver port becomes mandatory rather than
   optional and the design stalls until `sys/arch/pic32/dev/sd.c` is
   ported.
4. **Instruction rate.** Time a V6 boot to the login prompt. A Cortex-M0+
   at 125 MHz with a 6.5 KB interpreter dispatching 16-bit PDP-11
   opcodes should reach a few hundred thousand instructions per second;
   below about 50,000 the shell is unusable and the dispatch loop needs a
   jump table rather than a decode cascade.
5. **Swap behavior.** Run the emulator alongside `sh` and confirm the
   96 KB image finds a contiguous swap run after a few forks. This is
   the failure `STORAGE.md` recorded at 58 KB, one scale up.

The one impossibility, stated plainly: a full 248 KB PDP-11/40 cannot run
inside a DiscoBSD process on this board, because the emulated core alone
exceeds by 88 KB the 160 KB that the LARGE epoch sets as the maximum any
process may ever occupy.
