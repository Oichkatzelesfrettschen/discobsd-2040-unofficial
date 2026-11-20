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

Missing: key releases that happen when the key is released, and a mouse.

A terminal sends a byte when a key goes down and nothing when it comes
up, so each key is a make byte followed at once by its break byte. Games
that move while a key is held see an instant. The PicoCalc kernel has raw
key events (`LCDIOC_KEYEVS` on the console, a session a foreground program
opens in RAW or CBREAK mode) that carry press and release; a second
keyboard source in `pc.c` that reads them, used when the ioctl succeeds,
gives real make and break on that console and nowhere else.

Mouse: no pointing device exists on the PicoCalc. A serial mouse on an
emulated COM1, moved by the arrow keys while a modifier is held, is the
cheapest route a stock DOS mouse driver accepts; it needs a third
interrupt line and a UART model of about 100 lines.

Check: the key-event source on the Renode keyboard model, which sends
press and release; the mouse by a guest that reads three-byte packets.

## 2. Graphics modes and the 80-column screen

Missing: CGA modes 4, 5 and 6, and any way to show 80 columns on the panel.

The panel is 320 by 320. Mode 4 (320x200, four colors) fits one to one
with room to spare. Mode 6 (640x200, two colors) needs each pixel pair
merged into one, which keeps shapes and loses fine text. Eighty columns of
text need a font four pixels wide; no such font is in the tree, the two in
the GPL donors cannot be copied, and one must be drawn (95 ASCII glyphs,
then the upper half of code page 437).

Mechanism: a second front end beside the ANSI one, on `/dev/lcdgfx`
(`DEV_LCDGFX`, in PICOCALC and PICOCALC_SD, not in PICOCALC_LEAN). It
needs 16 KB for CGA memory at B800:0 in place of the 4 KB text page, a
dirty-row scan like `refresh()`, and `LCDG_WINDOW` writes in the two-bit
index format the driver already takes. The text front end draws cells from
its own font through the same call.

Smaller step on the existing console: the console's glyph bank holds 128
loadable glyphs (`LCDIOC_SETGLYPH`), exactly the count of code page 437's
upper half, so box drawing in the 40-column mode can be real with no new
device.

Check: the model panel of `check-picocalc-host` and the Renode panel both
give pixels back, so a frame can be compared with a reference. Cost: not
estimated; the 16 KB of display memory comes out of the page frames.

## 3. Windows 3.0

Windows 3.0 has a real mode that runs on an 8086 with 640 KB, a hard disk
and a CGA, Hercules, EGA or VGA display, so the processor model is not the
obstacle. Its keyboard driver takes interrupt 9, which now exists. What it
needs that is missing: item 2 with mode 6 (its CGA driver is 640x200 in two colors),
a hard disk image (present: `-h`, up to 1024 cylinders of 16 heads and 63
sectors), and patience: at the inferred speed of an XT, Windows 3.0 in real
mode was slow on real XTs too.

On this panel mode 6 shows at half its horizontal resolution, so Windows
text will be hard to read. That is a property of a 320-pixel panel, not of
the emulator.

Standard and 386 enhanced mode need item 6 and are out of reach here.
Whether a given application for Windows (Word, Excel) accepts real mode is
not verified; several of that period require standard mode.

The emulator's author does not fetch commercial software. MS-DOS, Windows
and applications come from images the owner supplies; the gate uses only
its own guest program, and the documents name FreeDOS and SvarDOS, which
are free to redistribute.

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

## 6. Protected mode

The 80286 model has none. A faithful one is descriptor tables, the
descriptor cache of each segment register, privilege checks on every
segment load and far transfer, the task state segment, and the faults of
chapters 6 to 9 of the 80286 manual: the 86Box equivalent is
`x86seg.c` at 2,639 lines beside its instruction core. It would roughly
double `cpu.c`, add a limit check to every memory access (the segment-limit
faults the real-mode model also lacks), and needs memory above one
megabyte, which costs page table space (one kilobyte of tables for each
megabyte). It buys Windows standard mode and DOS extenders, on a guest of
XT speed. Not proposed; recorded so the cut has a size.

## 7. More guest memory: PSRAM, compression

PSRAM: nothing to write. The PICOCALC kernels put a file system on the
PSRAM at `/tmp` when the part answers, and the page file is a file, so
`-s` on `/tmp` (the default) pages to PSRAM with no flash or card wear. No
board run has shown the PSRAM working (`PICOCALC.md`), so this is a build
and Renode claim; the Renode models have no PSRAM.

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

161 Thumb instructions for each guest instruction is the measured cost.
What remains in the interpreter itself: the decode switch compiles to a
comparison tree (about 11 instructions), every fetch recomputes the linear
address (about 14), and the flags are computed eagerly. A threaded decode
needs computed goto, which strict C17 does not have. The second core of
the RP2040 is idle on the PicoCalc kernels; using it for the display
refresh or the pager is a kernel interface (`CORE1.md`), not an emulator
change. No further work is proposed before a board measurement says what
the real speed is.

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
