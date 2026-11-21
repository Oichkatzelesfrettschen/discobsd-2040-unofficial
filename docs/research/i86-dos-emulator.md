# i86: what is not built, scoped

Status: proposals and measurements to make, 2026-11-20, for `usr.bin/i86`
at branch `usr.bin/x86-dos-emulator`. `sys/arch/rp2040/doc/I86.md` is the
authority for what exists. Each item below names the mechanism that is
missing, what it depends on, its cost where one can be stated, and the
check that would show it working. Nothing here has run on a board.

The target the owner named: the PicoCalc on the PICOCALC_SD kernel, disk
images and page file on the SD card, SvarDOS or MS-DOS, DOS games, Windows
3.0 with period applications, and a network when the board is a Pico W.

## Donors and what was taken from each

| Source | License | What it is | Taken |
| --- | --- | --- | --- |
| 8086tiny (adriancable) | MIT | a table-driven 8086 in one file with its own BIOS in the guest | nothing in code; its FreeDOS floppy served as the first boot workload, and its design (BIOS code in the guest behind private opcodes) is the alternative `pc.c` did not take |
| picocalc_x86 (shtirlic) | GPL-3.0 | 8086tiny on the PicoCalc with a Pico 2, flat guest RAM, 4x10 font, CGA renderer | nothing in code (license); it shows an 80-column font of 4x10 pixels is legible on this panel |
| pico-xt, pico-286 (xrip) | GPL-2.0, MIT | bare-metal PC emulators for RP2040 and RP2350 with software paging of guest RAM to PSRAM or SD in 2 KB or 4 KB pages with dirty bits | the precedent that paging guest RAM on this chip works; `mem.c` is written new, with one-kilobyte pages chosen by measurement and reference bits simulated by unmapping |
| 86Box | GPL-2.0 | a cycle-exact 808x core (`src/cpu/808x.c`) and a separate 286/386 core | nothing in code (license); read to confirm three 8088 behaviors the vectors showed (the DAA and DAS 9Fh test, the 60-6F alias, the word wrap at FFFFh). The clone is `~/Github/86Box` on the owner's machine |
| SingleStepTests 8088 and 80286 | MIT | processor states captured on a bus-level test rig | the oracle; samples are in `usr.bin/i86/tests` |

The ARM text size of the donor cores was not measured: each needs its
project's SDK headers to compile, and `i86`'s own size is measured, so the
comparison no longer decides anything.

## 1. Key releases and a mouse

Built since this note was first written: the interrupt controller and the
keyboard interrupt with scan bytes at port 60h (`I86.md`, Machine).

Built since then too: a Microsoft serial mouse on an emulated COM1, moved
by mouse keys (`I86.md`, Mouse). CuteMouse finds it and reports it through
interrupt 33h (`i86-dos-emulator/ctmouse-host.txt`).

Missing: key releases that happen when the key is released.

A terminal sends a byte when a key goes down and nothing when it comes
up, so each key is a make byte followed at once by its break byte. Games
that move while a key is held see an instant. The PicoCalc kernel has raw
key events (`LCDIOC_KEYEVS` on the console, a session a foreground program
opens in RAW or CBREAK mode) that carry press and release; a second
keyboard source in `pc.c` that reads them, used when the ioctl succeeds,
gives real make and break on that console and nowhere else.

With key releases, the mouse keys could also hold a button while its key
is held, in place of the `{` and `}` latch.

Check: the key-event source on the Renode keyboard model, which sends
press and release.

## 2. Graphics modes and the 80-column screen

Built: `i86 -g` draws on `/dev/lcdgfx`, 80 columns in a font four pixels
wide, all of code page 437, and the color adapter's modes 4, 5 and 6
(`sys/arch/rp2040/doc/I86.md`, Panel display). An earlier form of this
item said the device takes a two-bit index format; it does not. Its
window formats are RGB565, one palette index a byte and one bit a pixel
(`sys/lcdgfx.h`), and the display uses the last two.

Missing:

- Speed on the panel. The time of a frame on the wire is unmeasured; a
  game that changes most of the screen every frame redraws 64,000 pixels
  a refresh. The dirty unit is a kilobyte of a field, about thirteen scan
  lines; a finer unit (the pager's pages cannot be smaller, but a second
  compare inside a dirty page can) is the next step if the board shows
  the need.
- The 16-color modes: Tandy and PCjr 320x200, EGA 320x200 and 640x200.
  Each needs 32 KB or more of display memory, four times the loan a CGA
  mode takes from the pager, and for EGA the planar latch logic at the
  graphics controller's ports.
- Hercules 720x348: more pixels than the panel has in either direction.
- A second display page and the blink attribute in text modes.

Check: `check-i86-host` compares frames with a reference for the built
part; a new mode adds stages to `tests/panel.asm` and a frame rule to
`tests/panel.py`.

## 3. Windows 3.0

Runs, in real mode, observed by hand on the host build with the panel
model (`I86.md`, Evidence status; transcript
`i86-dos-emulator/windows30-host.txt`). Windows 3.00a installs on a 32 MB
SvarDOS disk made by `mkdosdisk.sh` from its seven 720 KB disks, changed
through the Control-] menu. Setup's DOS part detects the CGA, the COM1
mouse ("Microsoft, or IBM PS/2") and the 84-key keyboard and copies disks
1 and 2; its Windows part, Windows itself in real mode, asks for disks 3
to 5 when the printer, application search and documentation steps are
left out. Program Manager and File Manager run, keys and Alt menus work
and the mouse driver of Windows draws its pointer. The About box reports
"Real Mode" and 393 KB free; with `-e 2048` and EMS.SYS it reports "Real
Mode (Large Frame EMS)", 615 KB free (203 KB in EMS) and 1,776 KB of free
expanded memory.

Windows found two defects of the expanded memory manager on the way,
recorded because nothing else exercises these paths. It tells version 4.0
from 3.2 by reallocating handle 0 to its own size (51h) and gave up on
expanded memory when that was refused. It switches tasks through function
5Bh with save areas, and a manager that loaded the new area without
storing the old one sent it into a stale bank (an undefined instruction at
C224:CC4D, inside the C000h pages).

`win /s` answers "Your computer cannot run Windows in either standard or
386 enhanced mode"; WIN.COM asks DOS's multiplex interrupt (2Fh, 4300h)
for an XMS driver and finds none. Section 6 lists what standard mode
needs.

Open: Microsoft Office 1.0 (Word 1.1, Excel 2.1, PowerPoint 2.0), whose
disks are at hand, is not installed yet; whether each program accepts
real mode is the next decisive run. Mode 6 shows at half its horizontal
resolution on the 320-pixel panel, so Windows text is small and pairs of
pixels blend. The speed on a board is not measured.

The emulator's author does not fetch commercial software. The owner
supplied the Windows and Office disk images; they stay outside the tree,
and so does every disk image made from them. The gate uses only its own
guest programs.

## 4. Disk images, a floppy library, exchange with a host

Present: two floppy and two hard disk images named on the command line.

Missing: a place and a route for images.

Changing disks is built: Control-] opens a menu that lists the image
files beside the drive's current one and takes a name (`I86.md`,
Machine). A floppy library is therefore a directory of images.

A place: the PICOCALC_SD root is on the card, 32 MiB with about 16 MiB
free (`df` in the Renode transcript), so `/dos` on the root holds floppy
images and a hard disk image of a few megabytes. Larger images need a
larger root partition (`SDROOT_KBYTES`) or a file on the card's FAT
partition, which the kernel does not mount; `usr.sbin/msdosfs` reads and
writes a FAT volume from user space and can copy an image in.

A route from a PC over USB exists already and needs no driver: the
PicoCalc's UF2 loader, entered with the micro-USB cable in the Pico at
power-on, presents the SD card to the host as a mass-storage device
(`PICOCALC.md`, "Programming a new kernel from the card"). Images copied to
the card's FAT partition that way, or with the card in a reader, are then
copied into the root with `msdosfs`. A mass-storage function in the
running kernel, so the card appears on the host while DiscoBSD runs, is a
kernel USB change: the driver is a CDC-ACM console only, the kernel has
about 34 KB of text headroom on PICOCALC_SD and less on the others, and
the card would have to be unmounted while the host owns it. It is not
proposed. Over the console, `usr.bin/zmodem` transfers a file today at
serial speed.

## 5. A DOS set for the PicoCalc

Built, in `docs/research/i86-dos-emulator/`:

- `mksvardos.sh` makes a 360 KB SvarDOS boot floppy from the first disk of
  the SvarDOS 360 KB set: kernel, SvarCOM, `mem`, `mode`, `more`, the
  `sved` editor, and a CONFIG.SYS and AUTOEXEC.BAT of three lines each. It
  reports 584 KB free of 640 KB and runs resident in 128 page frames (98
  pages read back in a session).
- `mkdosdisk.sh` makes a bootable SvarDOS hard disk image of 2 to 500 MB:
  one FAT partition formatted by mtools, the partition sector of
  `mbr.asm`, and the system placed by SvarDOS's own `SYS C:` run inside a
  host build of the emulator. It ends by booting the image and requiring
  the shell's banner. Observed on the host, 2026-11-20: 16 MB (FAT12) and
  40 MB (FAT16) images boot on the 80286 model and list their directory.
  `mbr.asm` exists because the partition sector SvarDOS ships uses 80386
  instructions, which stop an 80286.

The settings follow from the machine: BUFFERS=4 and a small environment,
because every such byte is guest memory the emulator pages; no memory
manager, disk cache, EMS or XMS driver, because no memory exists above
640 KB. SvarDOS's kernel is the EDR-DOS kernel; MS-DOS programs and
drivers run on it, and an owner-supplied MS-DOS boots as well (5.0 is
observed).

To build next: SvarDOS packages unpacked onto the disk image (its `pkg`
tool reads a package file from the disk, so a set of `.svp` files copied
in by `mkdosdisk.sh` and installed by a key script is the route), and a
measured choice of BUFFERS and FILES against page traffic. The images
are built on a host and copied to the card; the board has no nasm or
mtools, though `i86` itself runs `FORMAT` and `SYS` there.

## 6. Protected mode and extended memory (Windows standard mode)

The 80286 model has no protected mode, and the first thing standard mode
asks for is not the processor: WIN.COM refuses before it starts because no
XMS driver answers (section 3). In the order Windows meets them:

1. **An XMS driver with free extended memory.** Windows 3.0 needs at least
   256 KB of it for standard mode. Its own HIMEM.SYS drives the hardware
   (A20 through the keyboard controller, LOADALL or interrupt 15h, 87h on
   an 80286); an i86 driver in the manner of EMS.SYS, the XMS entry point
   of interrupt 2Fh, 4310h trapping to firmware, is the lean route and
   needs nothing of the processor.
2. **Memory above one megabyte.** The pager's maps are flat: two
   pointer-sized entries for each kilobyte of address space, 8 KB for the
   first megabyte. Each further megabyte costs 8 KB more in the 192 KB
   process window, so 16 MB of 80286 address space is 128 KB of maps and
   does not fit. A few megabytes fit at the price of page frames; more
   need a two-level or hashed map above 1 MB with a slower path there.
   The page file already takes logical pages above 1 MB (expanded memory
   uses them).
3. **Address line 20.** Held low today, which keeps the 8086 wrap. The
   gate is the keyboard controller's output port (commands D0h and D1h on
   port 64h), which the controller here does not accept; with the line
   high, FFFF:0010 to FFFF:FFFF reach the 64 KB above 1 MB.
4. **Firmware.** Interrupt 15h function 88h (extended memory size, 0
   today), 87h (block move, a copy in C) and 89h (switch to protected
   mode); CMOS bytes 17h, 18h, 30h and 31h for the memory size; and the
   way back to real mode. An 80286 leaves protected mode only by reset:
   Windows' standard-mode loader resets the processor through the keyboard
   controller (command FEh) or a triple fault, after writing a shutdown
   code into CMOS byte 0Fh and a resume address at 40:67, and the
   firmware resumes there without clearing memory. i86 needs that reset.
5. **The processor.** LMSW with the protection bit, LGDT, LIDT, LLDT, LTR
   and their stores, LAR, LSL, VERR, VERW, ARPL, CLTS; a descriptor cache
   (24-bit base, limit, access rights) behind each segment register, used
   by every memory access instead of segment times 16, with the limit
   checked; type and privilege checks on segment loads; the faults of
   chapters 6 to 9 of the 80286 manual with error codes; interrupt and
   trap gates through the IDT, call gates, and returns across privilege
   levels; the task state segment; and LOADALL (0F 05), which 80286
   memory managers use. 86Box's `cpu/x86seg.c` (2,639 lines) is the
   reference. The SingleStepTests 80286 set, which captures real mode,
   tests none of it; the vectors left out of the sample for memory above
   1 MB and interrupts 12 and 13 test the boundaries.
6. **The budget.** An estimate, not a measurement: `cpu.c` from 10.8 KB
   of text at `-O2` to some 20 to 25 KB, and a base addition and a limit
   compare on every memory access; with the maps of a few megabytes, the
   resident frames fall from 128 toward 100 and paging rises, on a guest
   already of XT speed.

Not proposed. Real mode with expanded memory runs Windows 3.0 today; the
next decisive evidence is whether Word 1.1, Excel 2.1 and PowerPoint 2.0
run there (section 3). Should one of them require standard mode, items 1
to 4 are the smaller first stage: an XMS driver, a few megabytes above 1
MB, the A20 gate and the firmware, which also serve DOS programs that use
XMS without protected mode.

The numeric coprocessor is a separate choice. Neither Windows 3.0 nor
Office 1.0 needs one; Excel uses it when present. An exact 80287 is the
x87 instruction set over 80-bit extended precision, as 86Box does with
Berkeley SoftFloat 3e (`cpu/x87.c`, `cpu/softfloat3e`), tens of kilobytes
of Thumb code. The RP2040's boot ROM carries fast double precision
routines, which the Pico SDK's `pico_double` wraps; built on those, a
287 would be small and fast but round to 53 bits where the 287 keeps 64.

## 7. More guest memory: PSRAM, compression

PSRAM: nothing to write, with one condition. The page file is a file at
`/tmp` (the default `-s`), and `/etc/rc.rp2040` puts `/tmp` on the card's
type ba partition when the card has one; the kernel then gives the whole
PSRAM to swap (`sd_tmpattach()` in `sd.c`). Only a card without that
partition leaves `/tmp` on the PSRAM's 6 MB file system, where the page
file pages with no flash or card wear. The card images of the PicoCalc
board sessions carry a ba partition, so on them the page file is on the
card. No board run has shown the PSRAM working (`PICOCALC.md`), so the
PSRAM case is a build and Renode claim; the Renode models have no PSRAM.

Expanded memory: an EMS page frame at D000:0 backed by the page file is
the natural way to give DOS programs more than 640 KB, since the pager
already exists: four more 16 KB windows whose pages come from file offsets
above one megabyte, and an interrupt 67h service. About 200 lines. This is
the "swap for more DOS RAM" the owner asked for, in the form DOS programs
can use.

Compression: the window holds 136 frames. A compressed pool takes frames
away to hold more pages in the space, and wins only if the pages it keeps
resident would otherwise be read back from the file more often than the
frames it displaced. The measurement that decides it: dump each evicted
page of the SvarDOS and FreeDOS sessions, compress with a small LZ, and
replay the fault trace at 136 frames against 136 minus P frames plus a
pool of P kilobytes. Not run. With 128 frames SvarDOS reads 98 pages back
in a whole session, so there is little left to win for that system.

## 8. Network on a Pico W

The tree has no driver for the Pico W's CYW43439 radio, and the PicoCalc's
Pico socket shares pins with the board. A network for DOS has two halves:
the kernel half (the radio's SPI protocol, its firmware blob, a network
stack the kernel does not have at all) and the emulator half (a packet
driver interface on a software interrupt, or an NE2000 on ports, either of
which hands frames to a host socket). Only the emulator half belongs to
this program, and it has nothing to connect to until the kernel half
exists. A serial line to a host (SLIP over an emulated COM port onto the
second UART) is the route that needs no radio. Not scoped further.

## 9. Speed

163 Thumb instructions for each interpreter step is the measured cost
(`I86.md`, Cost, says what a step is).
What remains in the interpreter itself: the decode switch compiles to a
comparison tree (about 11 instructions), every fetch recomputes the linear
address (about 14), and the flags are computed eagerly. A threaded decode
needs computed goto, which strict C17 does not have. The second core of
the RP2040 is idle on the PicoCalc kernels; using it for the display
refresh or the pager is a kernel interface (`CORE1.md`), not an emulator
change.

The first board measurement (`i86-dos-emulator/board-speed-pico.txt`)
gives 0.32 million guest instructions a second for a CPU-bound loop on a
bare Pico at 125 MHz, with no display and no paging: XT speed, and 30 to
40 percent below the instruction-count estimate, because the loop costs
about 219 Thumb instructions a step rather than 153 and the board runs at
about 1.8 cycles per Thumb instruction. The panel, the card and the PSRAM
are not in that figure. Panel output runs on the same core: `st7365p.c`
polls the transmit FIFO at 12.5 MHz for every byte, so a full graphics
frame of 64,000 RGB565 pixels holds the emulating core for about 82 ms
(inference from the byte count), longer than one 55 ms timer tick.

## Board run, when the owner opens it

PICOCALC_SD image with `/usr/bin/i86` on the card and two files copied to
the card's root: `guest.img` (from `usr.bin/i86/tests/guest.hexdump`) and a
SvarDOS floppy from `mksvardos.sh`.

    i86 -d -T -v -w 40 -f 16 -a /guest.img -k aB1
    i86 -w 40 -a /svmin.img

The first must print the lines of `renode-picocalc-sd.txt` run 1; the
second must reach `A:\>`. Time from the command to the prompt, and
`i86 -v` page counts on leaving with Control-], are the first board
measurements of speed and of the page file on the card.
