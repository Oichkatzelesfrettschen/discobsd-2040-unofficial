# Agent guide: DiscoBSD RP2040 port

This is the DiscoBSD port to the Raspberry Pi Pico (RP2040), a fork of
chettrick/discobsd (remote `origin`, branch master) published as
github.com/Oichkatzelesfrettschen/discobsd-pico-unofficial (remote `pico`,
branch main). Its research and notes live in the sibling repository
github.com/Oichkatzelesfrettschen/discobsd-pico-notes, checked out beside
this one as ../discobsd-pico-notes; this tree keeps code, man pages, the
necessary documentation under sys/arch/rp2040/doc, and the thirteen
research documents listed in sys/arch/rp2040/doc/research/README.md that
code, Makefiles or the root manifest cite. New research goes to the notes
repository.

## Target

RP2040: Cortex-M0+ (ARMv6-M, Thumb-1), no FPU, MMU or MPU, 264 KB SRAM,
2 MB QSPI flash. Layout: 128 KB kernel, 1536 KB Dhara root (989 KB usable),
384 KB raw swap. A process gets one 144 KB window for text, data, bss and
stack; user programs are a.out OMAGIC. Boot ROM V3 on the verified board.
The console is CDC-ACM over the board's own USB cable (/dev/ttyACM*,
resolved by /dev/serial/by-id/*DiscoBSD*); UART0 on GP0/GP1 is the
fallback. Login as operator (no password) and `su` for root (blank
password). The console shell echoes keystrokes, so serial captures need
escape sequences stripped.

## Build, image, flash

    bmake MACHINE=rp2040 distribution      # tools, kernel, world, sdcard.img
    bmake MACHINE=rp2040 flash             # distrib/rp2040/flash.uf2
    bmake MACHINE=rp2040 kernel            # sys/arch/rp2040/compile/PICO/unix.uf2
    bmake MACHINE=rp2040 check-divider     # no kernel SIO divider use
    bmake MACHINE=rp2040 check-swapram     # linked SwapRAM tier matches Config
    bmake MACHINE=rp2040 check-elf2aout    # a.out layout gate

Reflash from a running kernel: `picotool reboot -u -f`, `picotool load
<uf2>` for the kernel and the filesystem image, `picotool reboot`; a hung
kernel needs BOOTSEL held through a replug. `bmake build` does not relink
programs when libc changes; run `bmake MACHINE=rp2040 clean` first when a
libc or header change must reach every program, and rebuild sbin/sysctl and
sbin/adminbox from clean after a sysctl.h or machine/cpu.h change. The
config-generated sys/arch/rp2040/compile/PICO/Makefile is tracked; commit it
when Config changes.

## Tests

- Host: `bmake -C usr.bin/smlrc test`, `bmake -C usr.bin/stevie test`,
  `sh bin/sh/tests/posix-sh.sh`, `bmake -C usr.bin/as/tests test
  MACHINE=rp2040`, `bin/tar/tests/tartest.sh`,
  `bmake check-tiny-utility-multicall` (true, false, and nohup dispatch,
  argument, signal, priority, stream, terminal, and exit-status contracts),
  `bmake -C tests/rp2040/uarea_exchange check MACHINE=rp2040` (the longjmp
  u-area exchange loop under qemu-arm), and `bmake -C games/keen test` (plays a
  seeded puzzle and checks solution uniqueness against an independent
  counter, with ambiguous, unique and inconsistent fixtures).
- On the board, from tests/rp2040: fptest (Boot ROM float, bit-exact),
  sigtest (signal frames), streamtest (NSTATIC), tartest, romprobe (ROM
  table dump). They are not in the root manifest; stage them by adding
  `file /usr/bin/<name>` lines to distrib/rp2040/mi.rp2040 for the test
  image and revert those lines before committing.
- Kernel trace: `sysctl -w kern.systrace=1` (syscalls) or 2 (signal
  frames), `kern.systracepid` to filter; under "options SYSTRACE".
- Host console helpers: distrib/rp2040/host/discobsd-web (web terminal,
  `--bind 0.0.0.0 --token SECRET` for the LAN), discobsd-link (short
  redirect URL), discobsd-term, the udev rule.

## Conventions

- Work in a worktree under ~/worktrees/discobsd-pico-unofficial/<branch>,
  push the branch, open a PR against `pico` main, merge, delete branch and
  worktree. Never commit to main directly.
- Build outputs are ignored per directory (a .gitignore holding the
  program name); never commit compile outputs, the sdcard image or test
  a.outs.
- Multicall binaries: box, sysbox, adminbox, textbox, utilbox, gamebox; the
  manifest's `link` lines name their entry points. Editors shipped: ed and
  stevie (vi and vim are hard links to stevie); re and kilo build but are
  not shipped.
- The user's global instructions apply (emoji-free text, `--` not em dash,
  American English, POSIX sh with set -eu, ${PYTHON}, no local paths or
  secrets in commits, `Assisted-by:` trailers).
