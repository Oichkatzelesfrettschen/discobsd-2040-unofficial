# pdp11: Sixth Edition UNIX on the board

`pdp11` is a PDP-11/40 with KT11 memory management, an RK11 controller
with one RK05 pack, and a KL11 console, built as an ordinary DiscoBSD
program. It boots the V6 pack installed at `/usr/v6/root.rk` on the
process's own terminal:

    $ pdp11
    pdp11: 64 KB, RK05 /usr/v6/root.rk; Ctrl-_ or ~. at a line start exits
    @unix

    login: root
    # ls /bin
    # ed
    # who

Type `unix` at the `@` prompt, `root` at `login:` (no password). V6's
interrupt character is DEL, its quit character is Ctrl-\, and it erases
with `#` and kills a line with `@` -- the shell you get is 1975's. Ctrl-_,
or `~.` at the start of a line, leaves the emulator and restores the
terminal; the pack keeps what you wrote, so `sync` before you leave. A
tilde anywhere else, or followed by anything but a period, reaches V6.

## What is emulated

| Part | Model |
| --- | --- |
| CPU | 11/40 instruction set, no FPP, no EIS beyond MUL/DIV/ASH/ASHC/XOR/SOB |
| Memory | `MEMSIZE` bytes of core, 64 KB by default; nonexistent memory traps through 4 |
| MMU | KT11-D: 8 kernel and 8 user pages, SR0 and SR2, access-control and length aborts |
| Disk | RK11 with one RK05 (203 cylinders, 2 surfaces, 12 sectors of 512 bytes) on a file |
| Console | KL11 on fd 0 and 1, polled with FIONREAD, tty raw for the emulator's lifetime |
| Clock | KW11-L at 60 Hz of wall time |

The instruction set and device registers descend from Julius Schmidt's
JavaScript PDP-11 through Dave Cheney's avr11 (WTFPL, `COPYING`),
translated to C and corrected: the MMU's access-control field is decoded
(avr11's checks were dead code), odd-address and nonexistent-memory
references trap, DMA reports bus errors through RKER instead of faulting
the CPU, and an RK error sets the RKER and RKCS bits that V6's driver
retries on and reports.

## The pack

`v6.rk.gz` is a V6 root pack of 2000 blocks (1 MB logical) built by
`tests/mkv6pack.py` from TUHS's `Dennis_v6/v6root` with the file list in
`tests/v6.list`: the RK kernel as `/unix`, `/etc`, a console-only `/dev`,
`/bin` and a few `/usr/bin` tools, and three of the games. The kernel's
swap range is patched to 120 blocks starting at block 2000, so the guest
swaps inside 60 KB just past the file system instead of at block 4000.
The pack's untouched blocks are holes: `gunzip` writes them as zeros and
`fsutil` leaves a block of zeros unallocated, so the pack costs the root
about 150 KB of its 1 MB.

The V6 software is distributed under the Caldera license
(`Caldera-license.pdf`). Rebuild the pack from the upstream image with:

    curl -O https://www.tuhs.org/Archive/Distributions/Research/Dennis_v6/v6root.gz
    gunzip v6root.gz
    "${PYTHON}" tests/mkv6pack.py -o v6.rk -f 2000 -s 2000 120 v6root tests/v6.list
    gzip -9 v6.rk

## Building and testing

The repository entry point requires the explicit legacy option:

    bmake MACHINE=rp2040 BUILD_PDP11_V6=yes build
    bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-pdp11-v6

The first command links the emulator against the port libc at 0x20000000,
converts it to a.out, and stages the program and pack with the rest of the
opt-in build. The second command builds the same sources with the host compiler,
boots the shipped pack on a pseudo-terminal, and checks the boot prompt, login,
a pipeline, `ed`, a write, and `sync`.

`MEMSIZE` is a compile-time constant. 64 KB gives V6 `mem = 116`, 11.6 K
words for user programs, which is what the setup document asks for; 96 KB
fits the 144 KB process window with less room for the host.
