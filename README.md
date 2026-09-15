DiscoBSD
========

2.11BSD-based UNIX-like Operating System for STM32 and PIC32 Microcontrollers
-----------------------------------------------------------------------------

DiscoBSD is a 2.11BSD-based UNIX-like operating system for microcontrollers,
with a focus on high portability to memory constrained devices without a
memory management unit.

The current and eighth official release of DiscoBSD is [DiscoBSD 2.7][1],
released on August 11, 2026.

This microcontroller-focused operating system is an independent continuation
of RetroBSD, a 2.11BSD-based OS targeting the MIPS-based PIC32MX7.
DiscoBSD is multi-platform, as it also supports Arm Cortex-M4 STM32F4 devices.

Source code to the system is freely available under a BSD-like license.

DiscoBSD/rp2040: this fork
--------------------------

This repository carries the port to the Raspberry Pi Pico (RP2040,
Cortex-M0+). The kernel lives in `sys/arch/rp2040`, the root file system is
a 1.5 MB region of the Pico's own QSPI flash with 384 KB of raw swap behind
it, and the console is the board's USB cable: the Pico enumerates as a
standard USB serial device (CDC-ACM), which Linux, Windows 10 and later,
and macOS drive with no added driver. `sys/arch/rp2040/doc/USER-ACCESS.md`
is the full connection guide; `sys/arch/rp2040/doc/` holds the boot map and
storage notes.

### Use a board you were handed

The board arrives flashed. Plug it into a USB port with a data cable. It
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
4. Open a terminal on the console. Ctrl-] quits.

       discobsd-term

5. Optional: serve the console to phones and laptops on your LAN.

       discobsd-console up

   It prints a short URL such as `http://192.168.1.20:42069/`; open it in
   any browser on the same network. `discobsd-console down` stops it,
   `discobsd-console status` reports. One browser session at a time.

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

4. Open a terminal on the console. Ctrl-] quits. Use Windows Terminal
   rather than the legacy console host for correct screen handling.

       .\discobsd-term.exe

5. Optional: serve the console on your LAN, then open the printed short
   URL from any browser on the same network. Allow the two ports through
   Windows Firewall when it asks; `discobsd-console.exe down` stops the
   servers.

       .\discobsd-console.exe up

#### macOS

1. Download `discobsd-host-macos.zip`, extract it, and open Terminal in
   the folder. Gatekeeper blocks an unsigned download the first time:
   control-click each program, choose Open, or run
   `xattr -dr com.apple.quarantine .` in the folder.
2. Plug the board in; it appears as `/dev/cu.usbmodem...`.

       ./discobsd-term --list
       ./discobsd-term --probe
       ./discobsd-term

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
- The console prints nothing after attaching: press Enter once. The board
  printed its prompt before you connected.
- The screen is garbled in a full-screen program such as `vi`: the board
  assumes 80 columns by 24 rows. Size the window to that, or run `resize`
  on the board after changing the window.
- The web page says the console is in use: another browser tab or a
  `discobsd-term` holds the serial line. Close it; the line is a single
  session.

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

### Host tools: the discobsd-host package

`distrib/rp2040/host` is the source of the tools above: `discobsd-term`
attaches a terminal that survives board reboots, `discobsd-web` serves
the console to any browser on the LAN behind a token, `discobsd-link`
publishes a short URL for it, and `discobsd-console` runs both. Its README
covers development, the tests, and the packaging; `.github/workflows/host.yml`
tests the package on Linux, Windows, and macOS and builds and
smoke-installs every artifact, and `host-release.yml` attaches them to a
release on a `host-v*` tag.

History
-------

[DiscoBSD][2] began as an undergraduate [Directed Study][3] in the winter of
2020 at the University of Victoria, Canada, as a case study of [RetroBSD][4]
to port the operating system to the Arm Cortex-M4 architecture, and to enable
portability in the hosting environment and target architectures and platforms.
The paper [*Porting the Unix Kernel*][5] details this initial porting effort.

Work on DiscoBSD has progressed in earnest since the completion of the
Directed Study, with the `DiscoBSD/stm32` port booting multi-user in
August 2022. The system is quite usable on supported development boards.

And work continues...

[1]: https://github.com/chettrick/discobsd/releases/tag/DISCOBSD_2_7
[2]: http://DiscoBSD.org
[3]: https://github.com/chettrick/CSC490
[4]: https://RetroBSD.org
[5]: https://github.com/chettrick/CSC490/raw/master/project_outputs/Porting_the_Unix_Kernel-CSC490-Christopher_Hettrick.pdf

DiscoBSD Resource Requirements
------------------------------

A basic, minimal system uses 128 Kbytes of flash and 128 Kbytes of RAM.

The kernel is loaded into the flash and only uses 32 Kbytes of RAM.
User programs each use the remaining 96 Kbytes of RAM, via swapping; the
RP2040 port gives them 144 Kbytes.
Devices with more RAM can be used to run larger user programs.

An SD card, at least 512 Mbytes in size, is required for the root file system.

Installing and Running
----------------------

Installation consists of loading the kernel into the microcontroller's flash
memory, and imaging the SD card with the file `sdcard.img`.

On Windows host systems use a disk imaging utility such as [Rufus][6].

On Unix-like host systems with `dd` run:

    $ dd bs=1M if=sdcard.img of=/path/to/SD/card

The board-specific kernel `unix` must be loaded into the MCU's flash memory.
Formats are ELF `unix.elf`, binary `unix.bin`, and Intel HEX `unix.hex`.

#### Loading a `DiscoBSD/stm32` kernel firmware into board's flash memory:

On Windows host systems use [STM32CubeProgrammer][7] for flash programming.

On Unix-like host systems use `st-flash` from the [stlink-org project][8] to
load the binary-formatted kernel `unix.bin` into flash memory at 0x08000000.

    $ st-flash --reset write unix.bin 0x08000000

#### Loading a `DiscoBSD/pic32` kernel firmware into board's flash memory:

On all supported host systems use [pic32prog][9] for flash programming.

    $ pic32prog unix.hex

Communication with the DiscoBSD console requires a serial port. A USB to TTL
device or the built-in VCP USB serial port on development boards can be used.

    $ cu -l /dev/cuaU0 -s 115200

Depending on the host system, other serial port utilities such as `screen`,
`minicom`, `putty`, or `teraterm` may be used.

Log in as user `operator` with no password, then `su` to root: operator is
in the wheel group, so `su` needs no password. Direct root login on the
console is refused.
Shutdown DiscoBSD with the `halt`, `shutdown`, or `reboot` commands.

Manual pages on DiscoBSD are available through the `man` command.

[6]: https://github.com/pbatard/rufus
[7]: https://www.st.com/en/development-tools/stm32cubeprog.html
[8]: https://github.com/stlink-org/stlink
[9]: https://github.com/majenkotech/pic32prog-autotools

Building
--------

DiscoBSD is cross-built on UNIX-like host operating systems.

Currently supported host operating systems: OpenBSD, Linux, FreeBSD.

### Instructions to configure a supported host development environment:
* [OpenBSD][10]
* [Debian-based Linux][27]

The build system fully supports both BSD make and GNU make.

From the source tree root, run:

    $ make build

or just:

    $ make

which builds `sys/arch/stm32/compile/${BOARD}/unix` ELF-formatted kernels
and builds and populates a whole file system userland in `DESTDIR`.

Running:

    $ make distribution

will build everything from a `make build`, plus generate a file system
image in the file `distrib/stm32/sdcard.img` for imaging to an SD card.

`DiscoBSD/stm32` is the default port, but `DiscoBSD/pic32` may be built via:

    $ make clean
    $ make MACHINE=pic32 MACHINE_ARCH=mips distribution

to generate a file system image in the file `distrib/pic32/sdcard.img`
for imaging to an SD card, `sys/arch/pic32/compile/${BOARD}/unix`
ELF-formatted kernels, and `sys/arch/pic32/compile/${BOARD}/unix.hex`
Intel HEX-formatted kernels.

Put the generated file system image `sdcard.img` onto an SD card.

The `make` target `installfs` uses the `dd` utility to image `sdcard.img`
to the SD card attached to the host operating system at `SDCARD`, such as
`/dev/rsdXc`, `/dev/daX`, `/dev/mmcblkX`, `/dev/sdX`, or `/dev/rdiskX`,
replacing `X` with the actual drive number or letter, as the case may be.

For example, imaging an SD card attached at `sd2` on an OpenBSD host
operating system through the raw i/o device:

    $ make SDCARD=/dev/rsd2c installfs

Note that using BSD make on a FreeBSD host requires the system makefile
include directory to be specified on the command line or via the
`MAKESYSPATH` environment variable. For example:

    $ make -m /usr/share/mk

or

    $ MAKESYSPATH=/usr/share/mk
    $ export MAKESYSPATH

    $ make

[10]: tools/openbsd/README.md
[27]: tools/linux/README.md

Building a DiscoBSD Release
---------------------------

A DiscoBSD release consists of, for each supported architecture:

* a file system image that contains the full base system
* kernels, in various file formats, for each supported development board
* the ANNOUNCEMENT.md file
* the README.md file for the architecture
* this README.md file

Before making a release, first create a distribution in `DESTDIR`
with kernels in `sys/arch/${MACHINE}/compile/${BOARD}` by following
the steps in [Building](#building):

    $ make distribution

Once all the distribution source objects exist then a release can
be created; one release for each architecture.

A `DiscoBSD/stm32` release, as the default architecture, is created by:

    $ make release

A `DiscoBSD/pic32` release is created by:

    $ make MACHINE=pic32 MACHINE_ARCH=mips release

A DiscoBSD release is created from an already-populated `DESTDIR`,
and it is placed in `RELEASEDIR`, `distrib/obj/releasedir` by default.

[Releases are available][11], for each architecture, as a `.tar.gz`
gzip-compressed tar archive and as a `.zip` zip-compressed archive.

[11]: https://github.com/chettrick/discobsd/releases

Debugging
---------

`DiscoBSD/stm32` is debugged through OpenOCD and GDB. The `make` targets for
debugging are `ocd` and `gdb-ocd`.

Debug a particular development board via:

    $ make BOARD=F412GDISCO ocd

in one terminal, and:

    $ make BOARD=F412GDISCO gdb-ocd

in another terminal.

Additional Information
----------------------

Port-specific information can be found in `distrib/${MACHINE}/README.md`
for [DiscoBSD/stm32][12] and [DiscoBSD/pic32][13].

[12]: distrib/stm32/README.md
[13]: distrib/pic32/README.md

References and Resources
------------------------

* The [RetroBSD wiki][14] has a wealth of PIC32 architecture information.
* [The Design and Implementation of the 4.3BSD UNIX Operating System][15].
* [Lions' Commentary on UNIX 6th Edition][16] and [recreations of it][17].
* [Advanced Programming in the UNIX Environment][18] 1st Edition.
* [The Design of the UNIX Operating System][19].
* [OpenBSD's curated list of books][20] relevant to BSD Operating Systems.
* The paper [*Porting the Unix Kernel*][5] details DiscoBSD's inception.
* The Unix Heritage Society's [Unix Archive][21] and [Source Tree][22].
* STMicroelectronics [Reference Manual][23] and [Programming Manual][24].
* [The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors][25].
* [ARMv7-M Architecture Reference Manual][26].

[14]: https://github.com/RetroBSD/retrobsd/wiki
[15]: https://archive.org/details/designimplementa0000unse
[16]: https://www.peerllc.com/peer-to-peer-books/lions-commentary-on-unix/
[17]: http://www.lemis.com/grog/Documentation/Lions/
[18]: http://www.kohala.com/start/apue.html
[19]: https://archive.org/details/DesignUNIXOperatingSystem
[20]: https://www.openbsd.org/books.html
[21]: https://www.tuhs.org/Archive/Distributions/UCB/
[22]: https://www.tuhs.org/cgi-bin/utree.pl
[23]: https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
[24]: https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf
[25]: https://booksite.elsevier.com/9780124080829/
[26]: https://developer.arm.com/documentation/ddi0403

Source Tree Roadmap
-------------------

    bin         User utilities in both single and multi-user environments.
    distrib     System distributions and releases.
    etc         Templates for system configuration files and scripts in /etc.
    games       Useful and semi-frivolous programs. The important stuff.
    include     Standard C include files.
    lib         System libraries.
    libexec     System daemons and utilities (executed by other programs).
    sbin        System administration utilities in both single and
                multi-user environments.
    share       Architecture-independent shared resource data files.
    sys         Kernel sources.
    tools       Build tools and simulators.
    usr.bin     User utilities in multi-user environments.
    usr.sbin    System administration utilities in multi-user environments.

`DiscoBSD/stm32` dmesg
--------------------

```
DiscoBSD 2.7 (F412GDISCO) #1 786: Tue Aug 11 11:11:11 MDT 2026
     chris@stm32.discobsd.org:/sys/arch/stm32/compile/F412GDISCO
cpu: STM32F412xE/G rev 1/C, 100 MHz, bus 50 MHz
oscillator: phase-locked loop, clock source: high speed external
uart2: pins tx=PA2/rx=PA3, af=7, console
uart6: pins tx=PG14/rx=PG9, af=8
sd0: port sdio0
sd0: type SDHC, size 31178752 kbytes
sd0a: partition type b7, sector 2, size 204800 kbytes
sd0b: partition type b8, sector 409602, size 2048 kbytes
sd0c: partition type b7, sector 413698, size 204800 kbytes
phys mem  = 256 kbytes
user mem  = 96 kbytes
root dev  = (0,1)
swap dev  = (0,2)
root size = 204800 kbytes
swap size = 2048 kbytes
Automatic boot in progress: starting file system checks.
/dev/sd0a: 1478 files, 12214 used, 191785 free
/dev/sd0c: 3 files, 3 used, 203996 free
Updating motd... done
Starting daemons: update cron 
Tue Aug 11 11:11:11 MDT 2026


2.11 BSD UNIX (name.my.domain) (console)

login: root
Password:
DiscoBSD 2.7 (F412GDISCO) #1 786: Tue Aug 11 11:11:11 MDT 2026

Welcome to DiscoBSD.

erase ^?, kill ^U, intr ^C
# 
```

`DiscoBSD/pic32` dmesg
--------------------

```
DiscoBSD 2.7 (MAX32) #1 786: Tue Aug 11 11:11:11 MDT 2026
     chris@pic32.discobsd.org:/sys/arch/pic32/compile/MAX32
cpu: 795F512L 80 MHz, bus 80 MHz
oscillator: HS crystal, PLL div 1:2 mult x20
spi2: pins sdi=RG7/sdo=RG8/sck=RG6
uart1: pins rx=RF2/tx=RF8, interrupts 26/27/28, console
uart2: pins rx=RF4/tx=RF5, interrupts 40/41/42
uart4: pins rx=RD14/tx=RD15, interrupts 67/68/69
sd0: port spi2, pin cs=RC14
gpio0: portA, pins ii---ii-iiiioiii
gpio1: portB, pins iiiiiiiiiiiiiiii
gpio2: portC, pins i-ii-------iiii-
gpio3: portD, pins --iiiiiiiiiiiiii
gpio4: portE, pins ------iiiiiiiiii
gpio5: portF, pins --ii--------i-ii
gpio6: portG, pins iiii--i-----iiii
adc: 15 channels
pwm: 5 channels
sd0: type I, size 524288 kbytes, speed 10 Mbit/sec
sd0a: partition type b7, sector 2, size 204800 kbytes
sd0b: partition type b8, sector 409602, size 2048 kbytes
sd0c: partition type b7, sector 413698, size 204800 kbytes
phys mem  = 128 kbytes
user mem  = 96 kbytes
root dev  = (0,1)
swap dev  = (0,2)
root size = 204800 kbytes
swap size = 2048 kbytes
Automatic boot in progress: starting file system checks.
/dev/sd0a: 1470 files, 13583 used, 190416 free
/dev/sd0c: 3 files, 3 used, 203996 free
Updating motd... done
Starting daemons: update cron 
Tue Aug 11 11:11:11 MDT 2026


2.11 BSD UNIX (name.my.domain) (console)

login: root
Password:
DiscoBSD 2.7 (MAX32) #1 786: Tue Aug 11 11:11:11 MDT 2026

Welcome to DiscoBSD.

erase ^?, kill ^U, intr ^C
# 
```
