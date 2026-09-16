# discobsd-host: development

README.md says how to use the tools. This file says how to work on them
and on the port from each host operating system, what the release
carries, and what CI checks. "Final" here means every command below was
run on the platform it names; "production-ready" means the release
artifact behaves the same as the checkout and the tests pass where the
artifact is built.

## The package

`discobsd_host` is a plain setuptools package under `src/`, with one
dependency (pyserial) and four console scripts. Work in a virtual
environment from a checkout:

    cd distrib/rp2040/host
    python3 -m venv .venv && . .venv/bin/activate    # Windows: .venv\Scripts\activate
    python -m pip install -e ".[test]"
    ruff check .
    python -m pytest -q

The tests need no board: they exercise the WebSocket framing, the token
and origin checks, the USB identity match, the redirector, the Windows
key translation, the configuration file, and drive the web server end
to end against a fake serial line. With a board attached, the manual
checks are `discobsd-term --list`, `--probe`, a login and `Ctrl-] q`,
and `discobsd-console up`, `status`, `down`.

The root of the port carries `discobsd-console`, a shell wrapper that
runs the package from the checkout with no install (`discobsd-console.cmd`
on Windows); a symlink named `discobsd-term`, `discobsd-web` or
`discobsd-link` to it runs that command.

## Host toolchain per operating system

| host | console tools | firmware build |
| --- | --- | --- |
| Ubuntu, Debian | `apt install python3-serial python3-pytest` or the venv above | `apt install bmake build-essential gcc-arm-none-eabi binutils-arm-none-eabi byacc bison flex libbsd-dev libfuse-dev pkg-config mandoc`; picotool 2.3.1 built from source (see `.github/workflows/firmware.yml`) |
| Arch, CachyOS | `pacman -S python-pyserial python-pytest ruff` | `pacman -S bmake arm-none-eabi-gcc arm-none-eabi-binutils byacc bison flex libbsd fuse2 pkgconf mandoc`; picotool 2.3.1 (the 2.3.0 in the AUR aborts on every RP2040) |
| macOS | `brew install pipx` then `pipx install -e .` or the venv above; Apple's Python 3.9 works but has no pyserial | `brew install bmake byacc bison flex groff mandoc pkgconf picotool` and `brew install --cask gcc-arm-embedded` |
| Windows | the venv above from python.org's Python; `pytest` and `ruff` run the same | not supported; build in WSL with the Ubuntu column |

macOS particulars, each verified on macOS 26 with Apple silicon:

- The cross compiler must be the Arm GNU Toolchain cask, not the
  `arm-none-eabi-gcc` formula: the formula is configured with an absolute
  `--with-as`, so gcc ignores the `-B${TOOLBINDIR}/` the userland
  Makefiles pass to substitute the port's a.out assembler, and every
  program links with the wrong `as`. The build system finds whichever
  `arm-none-eabi-gcc` is on PATH first, then the packaged path per OS.
- `yacc` in Apple's command line tools is a shim that demands a full
  Xcode; the build prefers `byacc` wherever it is on PATH.
- The kernel's version stamp comes from `date(1)`; Apple's awk has no
  `strftime()`.
- `fsutil --mount` needs FUSE, which on macOS is macFUSE and a kernel
  extension. The build leaves that command out when `pkg-config fuse`
  finds nothing; creating, checking and packing images does not need it.
- The Pico SDK is not a dependency of the port. picotool needs it only
  to be built, and Homebrew's bottle is already built.
- Nothing needs signing: the console is a CDC-ACM device the system
  driver binds, and picotool reaches the board through libusb.

## Tests and gates on each host

Every host test the port's guide lists was run on macOS 26 with the
toolchain above, after the fixes this table names. "Linux only" marks a
test whose mechanism does not exist on macOS; the test says so itself
and exits cleanly there.

| test | macOS | what it took |
| --- | --- | --- |
| `bmake MACHINE=rp2040 build`, `flash` | passes, warning-free | host tools ported: binstall, ar, size, strip, hsaout, fsutil, config; toolchain found on PATH; byacc preferred; bison from Homebrew's keg for awk; `date` in newvers.sh; the tree's tzfile.h ahead of the SDK's; `git rev-list -- HEAD` on a case-insensitive filesystem |
| check-hsaout, check-config-makefile, check-swapram, check-flash-swap | pass | kernel headers behind the host's (`-idirafter`) in hsaout and its test |
| check-swapram-evac | passes | the same header order; the swap pool's ELF section attribute under `__ELF__`; clang's K&R-definition error relaxed for subr_rmap.c |
| check-cache-footprint | the cache footprint checks pass; the exec-spool subtest is broken on every host (it links kernel code whose callees `swapnext`, `swap_cursor_publish` and `malloc3_contiguous_next` are undefined, on Linux CI as here, and with them stubbed it crashes), so the gate is not in CI until that test is repaired | Apple's linker spelling for dead-section stripping; clang's operand-width check off for Cortex-M inline assembly it never emits |
| check-divider | passes | a Python with `capstone` and `pyelftools` named in `PYTHON` |
| check-elf2aout | needs the MIPS cross toolchain on any host | not a macOS matter |
| `usr.bin/pdp11` V6 boot | passes | `cons_poll` read FIONREAD into a `long`; termios for every host build |
| `usr.bin/as/tests` | pass; the LD_PRELOAD fault injection is Linux only; the Unicorn run needs `pip install unicorn` | ranlib read the archive through a stream sharing the descriptor's offset, wrong on Apple's stdio |
| `bin/tar/tests/tartest.sh` | passes | tape ioctls left out; the directory stream kept open across recursion (a telldir cookie is per stream on macOS) |
| `check-tiny-utility-multicall` | passes | the pty step through `tools/ptyrun.py` instead of util-linux `script -c` |
| `usr.bin/smlrc`, `usr.bin/stevie`, `games/keen` | pass (smlrc link-only: no qemu-arm on macOS) | smlrc run from its output directory, under its 95-byte file name limit |
| `bin/sh/tests/posix-sh.sh` | Linux x86-64 only: it builds a 32-bit host binary | none |
| `tests/rp2040/uarea_exchange` | Linux only: needs qemu-arm user-mode emulation, which Homebrew's qemu does not build | none |

## Reflashing

`discobsd-flash` (POSIX sh, Linux and macOS) and `discobsd-flash.ps1`
(Windows) drive picotool: reboot the kernel into BOOTSEL, wait for the
boot ROM, unmount or eject the `RPI-RP2` volume, load each UF2, reboot.
The eject is the point: leaving BOOTSEL with the volume mounted earns
"Disk Not Ejected Properly" on macOS and a stale mount on Linux, and
picotool's own wait after a forced reboot is shorter than macOS takes to
enumerate the boot ROM, which is why `picotool info -f` fails there and
the two-step form does not.

    discobsd-flash sys/arch/rp2040/compile/PICO/unix.uf2       # kernel only
    discobsd-flash distrib/rp2040/flash.uf2                     # everything
    discobsd-flash --bootsel      # for a copy by hand; then --eject

## Release artifacts

`.github/workflows/host.yml` runs the tests on Ubuntu, Windows and macOS,
then builds and smoke-installs each artifact on its own platform:

| artifact | built how | carries |
| --- | --- | --- |
| wheel, sdist | `python -m build` | the package; `pipx install` on any OS |
| `.deb` (Ubuntu 24.04) | `packaging/debian`, `dpkg-buildpackage` | the package, udev rule, systemd user units, `discobsd-connect`, `discobsd-flash` |
| `.pkg.tar.zst` (Arch) | `packaging/arch/PKGBUILD`, `makepkg` | the same set |
| Windows zip | `packaging/pyinstaller/discobsd-host.spec`, one-file executables | the four `.exe`, `discobsd-flash.ps1` |
| macOS zip | the same spec on `macos-latest` (Apple silicon) | the four executables, ad-hoc signed, `discobsd-connect`, `discobsd-flash` |

The macOS executables are not notarized: they run from a terminal, with
or without the quarantine attribute a browser download adds, but
Gatekeeper refuses a Finder launch and an Intel Mac gets "Bad CPU type".
Both cases are the pipx install. Notarizing would need an Apple Developer
account and a Developer ID certificate in CI.

A release is a `host-v<version>` tag: `host-release.yml` runs the host
workflow and attaches the artifacts. The version lives in four places
and moves together: `pyproject.toml`, `src/discobsd_host/__init__.py`,
`packaging/arch/PKGBUILD`, and a new entry at the top of
`packaging/debian/changelog`.

`firmware.yml` builds the kernel, the world and the flash image on
Ubuntu and on macOS, asserts the log is warning-free, runs the host
gates, and publishes the UF2 files as an artifact.
