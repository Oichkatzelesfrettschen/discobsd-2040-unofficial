# Agent guide: DiscoBSD RP2040 port

This is the DiscoBSD port to the Raspberry Pi Pico (RP2040), a fork of
chettrick/discobsd (remote `origin`, branch master) published as
github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial (remote `rp2040`,
branch main). Its research and notes live in the sibling repository
github.com/Oichkatzelesfrettschen/discobsd-2040-notes, checked out beside
this one as ../discobsd-2040-notes; this tree keeps code, man pages, the
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
    bmake MACHINE=rp2040 check-cache-footprint # cache ABI and chain invariants
    bmake MACHINE=rp2040 check-elf2aout    # a.out layout gate

Reflash from a running kernel: `distrib/rp2040/host/discobsd-flash
<uf2>...` (kernel, filesystem image, or both), which reboots into
BOOTSEL, unmounts the RPI-RP2 volume, loads and reboots; by hand it is
`picotool reboot -u -f`, `picotool load <uf2>`, `picotool reboot`. A hung
kernel needs BOOTSEL held through a replug. On macOS the toolchain is
`brew install bmake byacc bison flex groff mandoc pkgconf picotool` plus
`brew install --cask gcc-arm-embedded` (not the arm-none-eabi-gcc
formula); distrib/rp2040/host/DEVELOPMENT.md has the per-OS table. `bmake build` does not relink
programs when libc changes; run `bmake MACHINE=rp2040 clean` first when a
libc or header change must reach every program, and rebuild sbin/sysctl and
sbin/adminbox from clean after a sysctl.h or machine/cpu.h change. The
config-generated sys/arch/rp2040/compile/PICO/Makefile is tracked; commit it
when Config changes.

## Tests

- `bmake MACHINE=rp2040 check` runs every tier; the tiers are check-lint
  (shellcheck -S error, ruff), check-host (host cc and python),
  check-posix-sh (32-bit Linux), check-cross (after build), check-qemu,
  check-mips, check-host-package and check-board-build. check-renode boots
  the kernel under Renode and stands outside check, because it wants the
  emulator and a fetched model tree.
  sys/arch/rp2040/doc/TESTING.md lists each gate and what it proves;
  a new test joins a tier there and in the root Makefile. CI runs the
  tiers in .github/workflows/firmware.yml.
- On the board, from tests/rp2040: fptest (Boot ROM float, bit-exact),
  sigtest (signal frames), streamtest (NSTATIC), tartest, romprobe (ROM
  table dump). They are not in the root manifest; stage them by adding
  `file /usr/bin/<name>` lines to distrib/rp2040/mi.rp2040 for the test
  image and revert those lines before committing.
- Kernel trace: `sysctl -w kern.systrace=1` (syscalls) or 2 (signal
  frames), `kern.systracepid` to filter; under "options SYSTRACE".
- Host tools: distrib/rp2040/host is the discobsd-host Python package
  (discobsd-term, discobsd-web with `--bind 0.0.0.0 --token SECRET` for the
  LAN, discobsd-link, discobsd-console up/down/status), with the udev rule,
  systemd user units, packaging/ (PKGBUILD, debian/, PyInstaller spec), and
  tests (`ruff check . && pytest` there). CI: .github/workflows/host.yml
  builds and smoke-installs every platform artifact; firmware.yml builds
  the UF2 files.

## Conventions

- Work in a worktree under `~/worktrees/discobsd-2040-unofficial/<branch>`,
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
