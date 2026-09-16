# The console from a Mac: drivers, Python, terminal, and reflash

Written on 2026-09-16 while bringing the host tools, and then the whole
port build and its test suites, up on macOS 26.0 (Apple silicon, Xcode
command line tools, Homebrew 7) against a board running the current
image. Everything below was run, not inferred. The fixes went to the port
in one pull request (host 1.0.8, "host: macOS section; discobsd-web ends
a reset viewer socket quietly", later widened to the build and tests);
distrib/rp2040/host/DEVELOPMENT.md in the port carries the per-host
tables that the reader who only wants to build should use.

## What "final, elegant, production-ready" means here

These words set the bar for every host-side change from now on, so they
are defined once:

- Final: a reader can follow the note from a clean Mac to a logged-in
  console without a second source, and nothing is left "to try later".
  Every command in the note was executed on this machine with the
  output it claims.
- Elegant: the smallest change that removes the whole class of problem,
  in the place the problem lives, with the reason recorded next to it.
  No wrapper around a wrapper, no option that exists only to paper over
  a bug, no platform branch where the platform does not differ.
- Production-ready: the released artifact behaves the same as the
  checkout, the tests pass on the platform, an error a user can hit
  prints one line that says what to do, and nothing needs root, a kext,
  a signature, or a notarization ticket.

## Drivers and signing: none

macOS binds its own CDC-ACM driver (AppleUSBACM under IOSerialFamily)
to the board's console interface when it is plugged in. No kernel
extension, no system extension, no signing question arises, because
nothing of ours runs in the kernel. The board shows up as:

    /dev/cu.usbmodemrp20401     crw-rw-rw-  root wheel
    /dev/tty.usbmodemrp20401    (the dial-in twin; blocks on carrier)

The node is world-writable, so no group membership or udev-style rule
is needed. The name is stable: "usbmodem" plus the USB serial number
"rp2040" plus the interface index. The host package's discovery
(ports.py) matches the board on vendor 2e8a, product 000a, serial
rp2040 through pyserial's enumeration, and rewrites tty. to cu. on
darwin, so `discobsd-term --list` prints the cu. node.

The board's second interface, the picotool "Reset" vendor interface,
gets no driver and needs none: picotool reaches it through libusb from
user space.

## Python: what Apple ships, what to install

Apple's command line tools carry Python 3.9.6 at /usr/bin/python3 with
pip 21.2 and no pyserial. 3.9 is exactly the package's floor
(requires-python >= 3.9) and it works: a venv from it installs the
package and passes all 29 tests. It is still the wrong daily
interpreter, because pip 21 warns on every install and Apple will drop
3.9 without notice. The install that a Mac should get is Homebrew's
pipx, which brings python@3.14 and keeps the tools in their own venv:

    brew install pipx picotool       # picotool only for reflashing
    pipx ensurepath                  # once; adds ~/.local/bin to PATH
    pipx install discobsd-host       # or: pipx install distrib/rp2040/host
    discobsd-term --list             # /dev/cu.usbmodemrp20401
    discobsd-term --probe            # prints the login banner
    discobsd-term                    # attach; Ctrl-] q leaves

`uv tool install discobsd-host` does the same job. Both create links in
~/.local/bin, and the second one installed silently replaces the first
one's links, so pick one. `discobsd-connect` also works: every Mac has
`cu`, and `brew install tio` gives the terminal that reconnects across
reboots on its own.

## What was verified

| step | result |
| --- | --- |
| `discobsd-term --list` | `/dev/cu.usbmodemrp20401` |
| `discobsd-term --probe` | reachable, tail ends in `login: ` |
| scripted login (pyserial) | operator login, `uname -a`, `whoami`, `df`, `exit` |
| interactive `discobsd-term` under a pty | login, command, `Ctrl-] ?` help, `Ctrl-] q` quits, exit 0 |
| `discobsd-console up / status / down` | short link on 42069 redirects to the tokenized 7681 page; page 200 with token, 401 without; state in `~/Library/Application Support/discobsd`, web.env mode 0600 |
| `ruff check . && pytest` on Python 3.14 (pipx) | 29 passed |
| same on Apple's Python 3.9 venv | 29 passed |
| released `discobsd-host-macos.zip` (host-v1.0.7) | arm64 Mach-O, ad-hoc signed; `--version` and `--list` run from a terminal, also with the quarantine attribute set; `spctl --assess` rejects it, so a Finder double-click is refused |
| `picotool info` on the running kernel | no BOOTSEL device, correctly suggests `-f` |
| `picotool reboot -u -f`, then `picotool info -a` | RPI-RP2 mounts, `RP2 Boot` enumerates, picotool reads the device without root |
| `picotool reboot` | kernel returns, fsck runs, probe succeeds |

Typeahead before `discobsd-term` prints "attached" is discarded, because
tty.setraw flushes pending input (TCSAFLUSH). A person cannot type that
fast; a script that pipes into discobsd-term must wait for the attach
line first. Not changed.

## Errors met and resolved

1. `ModuleNotFoundError: No module named 'serial'` from /usr/bin/python3.
   Apple's Python has no pyserial. Resolution: pipx (above); the tools
   never run on the system interpreter.

2. `pipx: command not found`. Homebrew formula `pipx`; `pipx ensurepath`
   once. This shell already had ~/.local/bin on PATH from an earlier
   uv install; a fresh Mac needs the ensurepath step and a new shell.

3. Test suite printed, twice, a socketserver traceback ending in
   `ConnectionResetError: [Errno 54] Connection reset by peer` from
   `FrameReader.read` in web.py, while all tests passed. On Linux a
   viewer that closes its socket with unread data yields a clean EOF
   from recv(); macOS raises ECONNRESET. The exception escaped through
   the HTTP handler and socketserver logged it. In production this is
   one traceback on stderr per abruptly closed browser tab. Fix in host
   1.0.8: recv() errors in FrameReader.read are end of stream, and the
   session closes the way a normal close does. The suite is silent now.

4. `picotool info -f` on the running kernel rebooted the board but then
   reported "no accessible RP-series devices in BOOTSEL mode were
   found", rebooted it back into the kernel, and printed nothing. The
   board did enter BOOTSEL; picotool's own wait after the forced reboot
   is shorter than the time macOS takes to enumerate RP2 Boot and mount
   RPI-RP2, so it gave up. The two-step form the port's guide already
   prescribes, `picotool reboot -u -f` and then the command, works every
   time. Documented, not patched: the one-shot form is a convenience
   and picotool is upstream's.

5. The web page returned 401 in the first check. That was the check:
   the env file's key is `DISCOBSD_WEB_TOKEN`, not `DISCOBSD_TOKEN`.
   With the right key the page is 200 and, without a token, 401.

## Reflashing from a Mac

    picotool reboot -u -f              # kernel into BOOTSEL; RPI-RP2 mounts
    picotool load distrib/rp2040/flash.uf2
    picotool reboot

or, with no picotool at all, copy the UF2 onto the mounted RPI-RP2
volume in Finder or with `cp`; the board reboots when the copy ends.
A hung kernel needs BOOTSEL held through a replug, as everywhere.

## The release zip and Gatekeeper

CI freezes the four commands with PyInstaller on `macos-latest`, which
is Apple silicon, so the zip is arm64-only and ad-hoc signed (the
linker does that on arm64). From a terminal the executables run, with
or without the quarantine attribute a browser download adds. Gatekeeper
refuses a Finder launch of an unnotarized download, and an Intel Mac
gets "Bad CPU type". Both cases are the pipx install, which is the
recommended one on macOS anyway. Notarizing would need an Apple
Developer account and a Developer ID certificate in CI; the README
says which path to take instead of promising that.

## Reflashing without the "Disk Not Ejected Properly" complaint

The port now carries `discobsd-flash` (POSIX sh) and `discobsd-flash.ps1`
(PowerShell) beside `discobsd-connect`, in the Debian and Arch packages
and in both zips. They reboot the kernel into BOOTSEL through picotool,
wait for the boot ROM themselves (picotool's own wait after `-f` is
shorter than macOS takes to enumerate it), unmount or eject `RPI-RP2`
before every reboot out of BOOTSEL, load each UF2, and reboot. Verified
here: `--bootsel` and `--eject` round trips, a kernel load in under ten
seconds, no volume left behind, and the console back at login.

## Building the port on a Mac: the toolchain

Every command in the port's guide was then run on this Mac. The
toolchain that works, all from Homebrew:

    brew install bmake byacc bison flex groff mandoc pkgconf picotool
    brew install --cask gcc-arm-embedded

Not the `arm-none-eabi-gcc` formula: it is configured with an absolute
`--with-as`, so gcc ignores the `-B${TOOLBINDIR}/` that eight of the
port's Makefiles pass to put the tree's own a.out assembler in front,
and every program links against the wrong `as`. Arm's own toolchain
(the cask) has no such setting; the build system now takes whichever
`arm-none-eabi-gcc` is on PATH first and only then the packaged path per
operating system. The Pico SDK is not a dependency: picotool needs it to
build, and Homebrew's bottle is built. macFUSE is not needed either:
fsutil's `--mount` is left out when `pkg-config fuse` finds nothing.

What stopped the build on macOS, in the order met, each fixed in the
tree rather than worked around on the host:

| stop | cause | fix |
| --- | --- | --- |
| tools/binstall | no pwcache(3), `st_atim` spelled `st_atimespec`, `strtofflags` takes `unsigned long` | an `__APPLE__` block with the two lookups and the two spellings |
| tools/aoututils ar | Apple's libc declares `strmode(int, char *)` | the tree's declaration only where Apple's is absent |
| size, strip | `#include </usr/include/stdio.h>`; macOS has no /usr/include | plain `<stdio.h>`; the tree's include directory is already `-idirafter` |
| tools/hsaout | a `machine` link at the front of the include path shadowed `<machine/signal.h>`, which macOS's `<sys/signal.h>` includes | the kernel headers behind the host's (`-idirafter .`), in the tool and its test |
| tools/fsutil | FUSE required | built only when pkg-config finds it |
| tools/config | Apple's `yacc` is a shim that wants full Xcode | `byacc` wherever it is on PATH |
| kernel link | `awk strftime()` is a gawk extension | `date(1)` |
| every program | `$(...)` inside a bmake `!=` assignment is expanded by make, not the shell | `$$(...)` |
| share/zoneinfo | the SDK ships a tzfile.h without the constants | `-iquote` the tree's include directory for the quoted include |
| usr.bin/awk | `bison -Wno-yacc` needs bison 3; Apple's is 2.3, Homebrew's is keg-only | the keg looked for first |
| usr.bin (after head) | `git rev-list HEAD` is ambiguous next to a file named `head` on a case-insensitive filesystem | `git rev-list --count HEAD --` |
| games/cribbage | `nroff` gone from macOS 26 | groff from Homebrew |

After those, kernel, world and flash image build warning-free. The
Mac-built kernel was flashed through `discobsd-flash` and booted: fsck,
daemons, login, and `sysctl kern.version` naming this Mac.

## The test suites on a Mac, with -k

Every test the port's guide lists was then run with `-k`, so all the
failures showed at once, and each was taken to its cause. Two were real
bugs that a Linux host had hidden:

- usr.bin/pdp11 never printed the bootstrap prompt. `cons_poll` read
  the FIONREAD byte count into a `long`; the ioctl fills an `int`, and
  the upper half stayed whatever was on the stack. Zero-initialized
  locals "fixed" it, which is how it was found. It is an `int` now.
- The tree's `ranlib` wrote a symbol table missing `write` on its first
  pass over a fresh archive, and a correct one on the second. It read
  the archive through a stdio stream on a dup of the raw descriptor,
  so the two shared one file offset that `get_arobj` moved underneath
  the stream; Apple's stdio keeps a seek that lands inside its buffer
  without a fresh `lseek`, then refills from the wrong place. The stream
  now opens the archive itself.

The rest were host seams: `script -c` (util-linux only) replaced by the
tree's own `tools/ptyrun.py` in the multicall check; tar's tape ioctls
left out and its directory stream kept open across recursion on macOS
(a telldir cookie is per stream there); Apple's linker spelling for
dead-section stripping; clang's operand-width check off for Cortex-M
inline assembly the host tests never emit; the swap pool's ELF section
attribute under `__ELF__`; smlrc run from its output directory, under
its 95-byte file name limit that a worktree path exceeds. Three tests
are Linux mechanisms and now say so: the LD_PRELOAD fault injection in
the ar tests, the 32-bit host `sh` build, and the qemu-arm u-area loop.
One was already broken everywhere: the exec-spool subtest of
check-cache-footprint links kernel code whose callees (`swapnext`,
`swap_cursor_publish`, `malloc3_contiguous_next`) the test never
defines. Linux CI, asked to run the gate on this PR, failed the same
link; with the three stubbed the test crashes. The gate is not in CI
until that test is repaired, which is work of its own.

CI: firmware.yml has a macOS job with the Homebrew toolchain above that
builds kernel, world and flash image, asserts a warning-free log, and
runs the gates and suites that pass here; the Linux job runs the same
widened set, minus check-cache-footprint. host.yml ships the flash scripts in both zips and runs
their usage check.
