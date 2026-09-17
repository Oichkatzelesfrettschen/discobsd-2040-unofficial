DiscoBSD/rp2040
===============

2.11BSD UNIX on the Raspberry Pi Pico
-------------------------------------

DiscoBSD/rp2040 is a 2.11BSD-derived UNIX for the Raspberry Pi Pico. The
whole system lives on the Pico itself: a 128 KB kernel, a 1.5 MB root file
system, and 384 KB of swap in the board's 2 MB QSPI flash, with 264 KB of
SRAM split between the kernel and a 144 KB window for the running
program. The console is the board's USB cable, a standard USB serial
device that Linux, Windows, and macOS drive with no added driver. The
board boots to a login prompt and runs a shell, the classic BSD commands,
`vi`, `awk`, `sed`, `ed`, a native C compiler, and a few games, on a
Cortex-M0+ with no memory management unit.

This repository is the RP2040 port of [DiscoBSD][1], Christopher Hettrick's
independent continuation of RetroBSD; the official project targets STM32
and PIC32 boards with an SD card, and its release is [DiscoBSD 2.7][2].
This port replaces the SD card with the Pico's own flash, adds the USB
console, and reworks memory ownership, swapping, and the userland for a
single 144 KB process. Everything below is about the Pico; the last
section points at the official platforms.

[1]: http://DiscoBSD.org
[2]: https://github.com/chettrick/discobsd/releases/tag/DISCOBSD_2_7

What runs on it
---------------

| item | value |
| --- | --- |
| board | Raspberry Pi Pico, RP2040, Cortex-M0+ at 125 MHz, 2 MB flash |
| kernel | `sys/arch/rp2040`, PICO (USB console) and PICO_UART (UART0 on GP0/GP1) |
| root | 1.5 MB flash region, wear-leveled by Dhara, about 980 KB of blocks |
| swap | 384 KB of raw flash behind the root, plus a 16 KB compressed in-RAM tier |
| user program | 144 KB window, one resident process, swap for the rest |
| console | USB CDC-ACM at 115200 8N1, 80x24; login `operator`, no password; `su` to root |
| commands | 112 names across /bin, /sbin, /usr/bin, /usr/sbin, /usr/libexec, and /usr/games, most of them hard links into seven multicall executables |
| Sixth Edition UNIX | `pdp11` boots a V6 root pack on an emulated PDP-11/40 with 64 KB of core; see "Run Sixth Edition UNIX" below |
| native toolchain | `cc` drives the Smaller C compiler, `as`, and `ld` against `/usr/lib/libc.a` on the board; no preprocessor and no headers ship, so sources declare what they call and use no `#include` |

What the login screen tells you, line by line:

    2.11 BSD UNIX (pico) (console)          getty: the kernel lineage, the hostname, the line
    login: operator                         type operator; no password is asked
    DiscoBSD 2.7 -- Raspberry Pi Pico       /etc/motd, first line
    You are operator, in /home/operator...  /etc/motd: where you are and what to type
    operator@pico:~$                        the shell: user@host, working directory, $ for operator

The prompt is `user@host:directory$`, the bash convention: green
`operator@pico`, a colon, the blue working directory, and `$`. `~` is
your home. `cd /` turns it into `operator@pico:/$`. `su` turns it into
`root@pico:/home/operator#`: you are root, the directory is still the
one you were in, exactly as `su` behaves on Linux, and `#` marks root.
The shell reads the real user, host and directory each time it prompts,
so the prompt never lies after `su` or `cd`. `/home/operator` starts
empty by design: the system lives under `/bin`, `/usr/bin` and
`/usr/games`, and `menu` lists every program. `ls` colors directories
blue, executables green, devices yellow, on any terminal that renders
ANSI color, which the web console and every terminal program listed
below do.

The console at first login, from `uname -a` and `df`:

    DiscoBSD pico 2.7 PICO#1 rp2040
    Filesystem  1K-blocks     Used    Avail Capacity  Mounted on
    root              979      837      142    85%    /

Documentation for the port lives in `sys/arch/rp2040/doc/`: `BOOT-MAP.md`
(flash and SRAM layout), `STORAGE.md` (Dhara root and raw swap),
`USER-ACCESS.md` (connecting, terminal size, the bootloader), and
`research/` for the design notes the code cites.

### Use a board you were handed

A board handed to you by the project's testers arrives flashed; any
other Pico is flashed in two minutes under "Build the firmware and flash
a board" below, or from the UF2 files a release carries. Plug the board
into a USB port with a data cable. It
boots within a few seconds and waits at a login prompt on the USB serial
line. Log in as `operator` with no password; run `su` (no password) for
root. Run `sync` or `halt` before unplugging so pending writes reach the
flash.

Install the host tools for your operating system from the latest
`host-v*` release at
<https://github.com/Oichkatzelesfrettschen/discobsd-pico-unofficial/releases>
and follow the steps for your platform.

#### Linux Mint, Ubuntu 24.04, Debian

1. Download `discobsd-host_<version>_all.deb` from the release page.
2. Install it. The package pulls in Python and pyserial and adds the udev
   rule that names the board `/dev/discobsd`:

       sudo apt install ./discobsd-host_*.deb

3. Plug the board in. The rule grants the console to the user at the
   desktop the moment it appears, so no group and no re-login is needed.
   Confirm the tools see it:

       discobsd-term --list
       discobsd-term --probe

   `--list` prints the device path; `--probe` reports "reachable" with the
   bytes the board answered.
4. Open a terminal on the console. Ctrl-] q quits; Ctrl-] ? lists the
   other escapes.

       discobsd-term

5. Optional: serve the console to phones and laptops on your LAN.

       discobsd-console up

   It prints a short URL such as `http://192.168.1.20:42069/`; open it in
   any browser on the same network. `discobsd-console down` stops it,
   `discobsd-console status` reports. One browser session at a time;
   press `Sync & leave` in the page to sync, log out, and hand it to the
   next, watching each step it types; `keys` shows the key reference;
   `reader` turns on screen reader mode. The status line at the top
   right says whether the console is in DiscoBSD or inside V6.

Over ssh, or as a user who is not logged in at the machine's own screen,
join the `discobsd` group instead and log in again:

    sudo usermod -aG discobsd "$USER"

#### Windows 11

1. Download `discobsd-host-windows.zip` from the release page and extract
   it, for example to `C:\discobsd-host`. It holds four standalone
   programs; no Python install is needed.
2. Windows Defender SmartScreen stops an unsigned program the first time:
   choose "More info", then "Run anyway". The programs are built by the
   repository's public CI from the tagged source.
3. Plug the board in. Windows loads its built-in USB serial driver and
   assigns a COM port. Open Windows Terminal or PowerShell in the folder
   and confirm the tools see the board:

       cd C:\discobsd-host
       .\discobsd-term.exe --list
       .\discobsd-term.exe --probe

4. Open a terminal on the console. Ctrl-] q quits; Ctrl-] ? lists the
   other escapes. Use Windows Terminal rather than the legacy console
   host for correct screen handling.

       .\discobsd-term.exe

5. Optional: serve the console on your LAN, then open the printed short
   URL from any browser on the same network. Allow the two ports through
   Windows Firewall when it asks; `discobsd-console.exe down` stops the
   servers.

       .\discobsd-console.exe up

#### macOS

No driver and nothing to sign: macOS binds its own USB serial driver to
the board and creates `/dev/cu.usbmodemrp20401`, world-writable, the
moment it is plugged in.

1. Install the tools through Homebrew's pipx (Apple's own Python 3.9 has
   no pyserial), and picotool if you will reflash:

       brew install pipx picotool
       pipx ensurepath          # once, then open a new terminal
       pipx install discobsd-host

2. Plug the board in and connect. Ctrl-] q quits; Ctrl-] ? lists the
   other escapes.

       discobsd-term --list
       discobsd-term --probe
       discobsd-term

The `discobsd-host-macos.zip` on the release page holds the same four
programs as Apple silicon executables for a terminal (`./discobsd-term`
in the extracted folder); Gatekeeper refuses them from the Finder, and
an Intel Mac needs the pipx install.

#### Arch Linux, CachyOS

    sudo pacman -U discobsd-host-<version>-1-any.pkg.tar.zst
    discobsd-term --probe
    discobsd-term

#### Any system with Python 3.9 or later

    pipx install discobsd_host-<version>-py3-none-any.whl
    discobsd-term --probe

The wheel installs no udev rule; on Linux, add your user to the group
your distribution uses for serial ports (`dialout` on Debian and Ubuntu,
`uucp` on Arch) and log in again.

#### If something does not work

- `discobsd-term --list` prints nothing: the cable is power-only, or the
  board is in the bootloader. Try another cable; a board in BOOTSEL mode
  shows up as a USB drive named `RPI-RP2`, so unplug and replug it without
  the button held.
- "Permission denied" on Linux: you installed the wheel rather than the
  `.deb`, or you are over ssh. Join `dialout` (Ubuntu) or `discobsd` (with
  the `.deb`) and log in again.
- The console prints nothing after attaching: press Enter once to redraw
  the prompt. The kernel keeps the boot messages in an 8 KB ring and
  sends them to the first terminal that opens; a terminal opened while
  the board is still enumerating can lose some of those bytes.
- The screen is garbled in a full-screen program such as `vi`: the board
  assumes 80 columns by 24 rows. Size the window to that, or run `resize`
  on the board after changing the window.
- The web page says the console is in use: another browser tab or a
  `discobsd-term` holds the serial line. Close it; the line is a single
  session.

### Hardware, care and recovery

- The board is a stock Raspberry Pi Pico: RP2040 at 125 MHz, 264 KB of
  SRAM, 2 MB of QSPI flash, powered from the USB connector at 5 V and
  drawing under 100 mA. Nothing is soldered to it and nothing else is
  needed; the USB cable must carry data, not only power.
- The root file system lives in the Pico's flash behind a wear-leveling
  layer. Type `sync` before unplugging; `halt` is safer still. Pulling
  the cable during a write loses at most that write, and `fsck` runs at
  the next boot.
- The V6 pack and anything you write are on the board, not in the host
  tools; unplugging carries them with it.
- A board that no longer answers on USB is not lost: hold BOOTSEL while
  plugging it in, it appears as the `RPI-RP2` drive, and the two UF2
  files from a release or a build restore it in under a minute ("Build
  the firmware and flash a board" below).
- Which firmware is on a board: `cat /etc/release` prints the commit,
  date and builder of the root file system, `uname -a` the kernel, and
  `cat /etc/COPYRIGHT` the license notice. A release or a listing states
  the same commit, so the two can be compared.
- Questions and problems go to the issue tracker of this repository;
  say what `discobsd-term --probe` printed and what the screen showed.

### Run Sixth Edition UNIX

The board carries `pdp11`, a PDP-11/40 emulator, and a Sixth Edition
(1975) root pack. The machine it emulates:

| part | what V6 sees |
| --- | --- |
| processor | PDP-11/40, KT11-D memory management (8 kernel and 8 user pages), no floating point |
| core | 64 KB; V6 reports `mem = 116`, 11.6 K words free for programs |
| disk | one RK05 pack of 4872 blocks on an RK11, backed by `/usr/v6/root.rk`, a 1 MB sparse file |
| console | a KL11 on your terminal; V6 sees it as `tty8` |
| clock | KW11-L line clock at 60 Hz, from the board's clock |
| pack contents | `/unix`, `/etc`, `/bin` (34 tools), `/usr/bin` (10), `/usr/games` (3); no compiler |

Start it from the DiscoBSD prompt as `operator`:

    $ pdp11
    pdp11: 64 KB, RK05 /usr/v6/root.rk; Ctrl-_ or ~. at a line start exits
    @unix

    login: root
    #

1. Type `unix` at the `@` prompt. The kernel boots in about five seconds.
2. Type `root` at `login:`; there is no password.
3. Use the 1975 shell: `ls /bin`, `who`, `ed`, `cal 9 2026`, `ls /usr/games`.
4. Leave with `sync`, then one of: Ctrl-_ (web console: the `^_ exit V6`
   button; discobsd-term: Ctrl-] _), or `~.` typed at the start of a
   line. The emulator prints its instruction count and the DiscoBSD `$`
   prompt returns.

Keys differ in V6. DEL interrupts (Backspace sends DEL on most
terminals, so Backspace interrupts too), `#` erases a character, `@`
erases the line, Ctrl-\ quits, Ctrl-D logs out. Ctrl-C, Ctrl-U and the
arrow keys mean nothing to it. A `#` prompt where `whoami` is "not found"
is V6, not DiscoBSD's root shell. The full key table per system is in
`distrib/rp2040/host/README.md` under "Keys and exits".

What you write on the V6 pack stays on it. The pack is a 1 MB sparse file
in `/usr/v6`; its untouched blocks cost nothing, and a session that swaps
or writes allocates about 30 KB of the root the first time, reused after.
`usr.bin/pdp11/README.md` documents the emulator, the pack and how it was
built from the TUHS distribution.

### Build the firmware and flash a board

1. Install the cross toolchain and the build tools. Ubuntu 24.04:

       sudo apt install bmake gcc-arm-none-eabi binutils-arm-none-eabi \
           byacc bison flex libbsd-dev libfuse-dev pkg-config mandoc vim \
           python3 picotool

   Arch: `pacman -S bmake arm-none-eabi-gcc arm-none-eabi-binutils byacc
   bison flex libbsd mandoc vim python picotool`. Ubuntu's `picotool` is
   too old for `picotool uf2 convert`; `.github/workflows/firmware.yml`
   shows how CI builds picotool 2 from source.

2. Build the kernels, the userland, and the root image. The tree builds
   with zero warnings; CI enforces that.

       bmake MACHINE=rp2040 build
       bmake MACHINE=rp2040 flash

3. Put the board in BOOTSEL: hold the BOOTSEL button while plugging the
   cable in, or from a running system run `picotool reboot -u -f`. The
   board mounts as a USB drive named `RPI-RP2`.

4. Load both images in the same BOOTSEL visit, the kernel and the file
   system, then reboot into the system:

       picotool load sys/arch/rp2040/compile/PICO/unix.uf2
       picotool load distrib/rp2040/flash.uf2
       picotool reboot

   A change to the file system alone needs only `flash.uf2` reloaded.
   Copying the two `.uf2` files onto the `RPI-RP2` drive in a file
   manager works as well, one after the other, re-entering BOOTSEL
   between them because the board reboots after each copy.

5. Connect with `discobsd-term`. Every push to `main` also builds both
   `.uf2` files in CI and publishes them as the `discobsd-rp2040-firmware`
   artifact under Actions, so a tester can flash without a toolchain.

Build the host tools from source
--------------------------------

`distrib/rp2040/host` is the `discobsd-host` Python package behind the
commands above. Its README covers development, the tests, and the
packaging (a PKGBUILD, a `debian/` tree, and a PyInstaller spec).
`.github/workflows/host.yml` tests the package on Linux, Windows, and
macOS and builds and smoke-installs every artifact; `host-release.yml`
attaches them to a GitHub release on a `host-v*` tag.

    cd distrib/rp2040/host
    python3 -m pip install -e ".[test]"
    ruff check . && pytest

Nothing needs installing to run the console from the checkout itself:
`discobsd-console` at the root of the tree is a POSIX sh wrapper that
runs the package from `distrib/rp2040/host/src` with the interpreter in
`${PYTHON}` (default python3), and `discobsd-console.cmd` beside it does
the same for cmd and PowerShell (default python). The one dependency is
pyserial. Linked or copied under the names discobsd-term, discobsd-web
or discobsd-link, the sh wrapper runs that tool instead.

    ./discobsd-console up          # Linux, macOS, Git Bash
    .\discobsd-console up          # PowerShell, cmd
    ./discobsd-console status
    ./discobsd-console down

Source tree
-----------

    bin, sbin       User and system utilities; on this port most are members
                    of the multicall executables box, sysbox, and adminbox.
    usr.bin         Multi-user utilities; textbox, utilbox, and grepbox fold
                    the smaller ones. usr.bin/smlrc is the native C compiler.
    games           gamebox: fifteen, keen, bubble.
    lib             libc and crt0 for the target.
    sys/arch/rp2040 Kernel: boot2, machine-dependent code, USB CDC-ACM and
                    UART drivers, the Dhara flash layer, SwapRAM, the packed
                    executable loader, and doc/.
    distrib/rp2040  The root manifest (mi.rp2040), the flash image build,
                    the on-board libc, and the host tools package.
    tests           Host and on-board regression tests (tests/rp2040).
    tools           Cross-build tools: config, fsutil, hsaout, the a.out
                    utilities, the checkers the make targets run.

Verification gates
------------------

The tree builds with zero warnings and CI enforces it. The host-side
checks that guard the port's invariants run from the tree root:

    bmake MACHINE=rp2040 check-swapram check-flash-swap check-swapram-evac
    bmake MACHINE=rp2040 check-cache-footprint check-exec-spool check-divider
    bmake MACHINE=rp2040 check-hsaout check-config-makefile check-ufs-prototypes
    PYTHON=python3 bmake MACHINE=rp2040 check-elf2aout

On-board tests live in `tests/rp2040`; each has a host driver that flashes,
logs in over the USB console, and checks the result.

References
----------

- RP2040 Datasheet and the Raspberry Pi Pico Datasheet (Raspberry Pi Ltd),
  the authority for the SRAM banks, the USB controller, the boot ROM, and
  the QSPI flash interface this port programs.
- Dhara, the flash translation layer the root uses:
  <https://github.com/dlbeer/dhara>.
- heatshrink, the compressor behind SwapRAM and packed executables:
  <https://github.com/atomicobject/heatshrink>.
- The paper [*Porting the Unix Kernel*][3] on DiscoBSD's inception.
- [The Design and Implementation of the 4.3BSD UNIX Operating System][4].
- [Lions' Commentary on UNIX 6th Edition][5].
- The Unix Heritage Society's [Unix Archive][6] and [Source Tree][7].
- [ARMv6-M Architecture Reference Manual][8], the Cortex-M0+'s ISA.

[3]: https://github.com/chettrick/CSC490/raw/master/project_outputs/Porting_the_Unix_Kernel-CSC490-Christopher_Hettrick.pdf
[4]: https://archive.org/details/designimplementa0000unse
[5]: https://www.peerllc.com/peer-to-peer-books/lions-commentary-on-unix/
[6]: https://www.tuhs.org/Archive/Distributions/UCB/
[7]: https://www.tuhs.org/cgi-bin/utree.pl
[8]: https://developer.arm.com/documentation/ddi0419

License and redistribution
--------------------------

Everything in the image is under a permissive license and nothing is
under the GPL. `NOTICE` at the top of the tree reproduces every notice
that binary redistribution requires; `/etc/COPYRIGHT` on the board is
its short form. The parts and their licenses:

| part | holder | license |
| --- | --- | --- |
| the system, the rp2040 port, the host tools | DiscoBSD, RetroBSD | BSD 3-Clause (`LICENSE`); port files marked 2026 DiscoBSD are ISC |
| kernel, libc, most of /bin and /usr/bin | The Regents of the University of California | Berkeley licenses of 1980-1993; the advertising clause was withdrawn by the University in 1999 |
| the AT&T-descended programs (sh, ed, sed, awk, find, cpio, dd, look, deroff) and the V6 pack | Caldera International, Inc. | Caldera ancient-UNIX license, 2002: BSD-style with an acknowledgement in advertising |
| second-stage boot code | Raspberry Pi (Trading) Ltd. | BSD 3-Clause |
| Dhara, heatshrink | Daniel Beer, Scott Vokes | ISC |
| compiler_rt soft float | LLVM Team, University of Illinois | NCSA or MIT |
| Smaller C | Alexey Frunze | BSD 2-Clause |
| as, ld | Serge Vakulenko | MIT/X-style |
| textbox tools | sbase contributors | MIT |
| stevie | public domain | Unlicense |
| pdp11 emulator | Julius Schmidt, Dave Cheney | WTFPL |
| coremark | EEMBC | Apache 2.0, plus a trademark agreement on the CoreMark name |
| libgcc | Free Software Foundation | GPL-3 with the Runtime Library Exception 3.1 |

To redistribute the firmware, as UF2 files or on a board:

1. Ship `NOTICE` (or a document reproducing it) with the files or in
   the product's documentation. The board carries `/etc/COPYRIGHT`,
   which satisfies the "other materials provided with the
   distribution" wording of every license above when the buyer can
   read it, and the printed or online documentation must carry the
   full `NOTICE`.
2. Put the two acknowledgement sentences in any advertising that
   mentions the software's features: "This product includes software
   developed or owned by Caldera International, Inc." (required by the
   Caldera license) and "This product includes software developed by
   the University of California, Berkeley and its contributors" (no
   longer required since 1999, kept as courtesy). A Tindie listing that
   describes what the board runs is advertising in this sense.
3. Do not use the names DiscoBSD, RetroBSD, the University of
   California, Caldera, Raspberry Pi, or any contributor to endorse or
   promote the product; that clause is in every license.
4. Name the product without "Raspberry Pi" or "Pico" in the product
   name and use referential wording in the description ("runs on a
   Raspberry Pi Pico"), which is what Raspberry Pi's trademark rules
   permit without a license; the Raspberry Pi logo may appear only on
   a listing that sells the genuine board itself. Reselling a genuine
   Pico with this firmware installed is a resale of a genuine product.
5. Either satisfy EEMBC's CoreMark acceptable-use agreement (unmodified
   benchmark, trademark notices) or leave `coremark` out of a product
   image by removing its `pack` line from `distrib/rp2040/mi.rp2040`.
6. UNIX is a registered trademark of The Open Group; describe the
   system as "2.11BSD-derived" rather than "UNIX" in a product name.

The source for the exact image is the tag or commit the UF2 files were
built from; state it in the listing so the licenses' "source code"
conditions can be met by pointing at it.

Other platforms: STM32 and PIC32
--------------------------------

The official DiscoBSD supports STM32F4 boards and the PIC32MX7, with the
root file system on an SD card. Those ports build from this tree too, and
their documentation is the official project's:

- Official repository and releases: <https://github.com/chettrick/discobsd>
- Port notes: `distrib/stm32/README.md` and `distrib/pic32/README.md`
- Host setup: `tools/openbsd/README.md` and `tools/linux/README.md`

Build them with the upstream commands, from the tree root:

    make distribution                                   # DiscoBSD/stm32, the upstream default
    make MACHINE=pic32 MACHINE_ARCH=mips distribution   # DiscoBSD/pic32
    make SDCARD=/dev/sdX installfs                      # image the SD card

Flash an STM32 kernel with `st-flash --reset write unix.bin 0x08000000`
(or STM32CubeProgrammer on Windows) and a PIC32 kernel with
`pic32prog unix.hex`; connect at 115200 8N1 with `cu`, `screen`,
`minicom`, or PuTTY. `make BOARD=F412GDISCO ocd` and `gdb-ocd` debug an
STM32 board through OpenOCD and GDB. `make release` builds a release
archive for the default architecture.
