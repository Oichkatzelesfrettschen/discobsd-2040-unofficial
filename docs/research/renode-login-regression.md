# The Renode login test fails on main before getty runs

## Finding

`bmake check-renode`'s second test, "The boot reaches a login prompt and a
shell", times out waiting for the line `Automatic boot in progress: starting
file system checks.` on every tree tried on 2026-09-20, including a control
built at `0b35cbcc`, the commit before the stdio core change #150. The first
test, the device probe through `swap size = 380 kbytes`, passes on every one
of them in about two seconds. The line the second test waits for is the
first thing `/etc/rc` prints, so whatever fails sits between the kernel's
last probe line and init's first shell: init, the exec of `/bin/sh`, or
`/etc/rc` reaching its first `echo`.

## Trees tried, all with the same result

| Tree | Content | Host state | Result |
| --- | --- | --- | --- |
| `getty-console-fixes` on `558fcdf9` | #150 plus the getty patches | three builds and a second emulator running | timeout at the rc banner |
| `libc-doprnt-c17` on `558fcdf9` | #150 plus the `_doprnt` change | idle | timeout at the rc banner |
| `getty-console-fixes`, rerun | as above | idle | timeout at the rc banner |
| `control-0b35cbcc` | #147 and #148 on `39b1bf77`, before #150 | idle | timeout at the rc banner |

Renode 1.17.0+20260907gitf1dd1b4af, models at the pinned commit
`205a5e4b`, freshly fetched and built by `tools/renode/fetch-renode-rp2040.sh`
on this host. The last recorded pass of this test is the transcript in
`sys/arch/rp2040/doc/research/emulation.md`, kernel build 818 of
2026-09-11, and the last commit that names a Renode boot is #117 of
2026-09-17.

## What this establishes and what it does not

Established: the failure is independent of #150, of the getty change, and of
host load. Not established: which commit between #117 and `0b35cbcc`
introduced it, or whether the cause is in the tree at all rather than in the
emulator build on this host. Two attempts to record the raw UART text after
the probe, one through `tools/renode/console.py` against `boot.resc` and one
through Renode's `CreateFileBackend` on `uart0`, both produced no bytes when
Renode ran headless from a script; the Robot harness is the only path that
has driven the machine here, and it records only the lines it waits for.

## Consequence

`check-renode` cannot currently distinguish a getty change that breaks the
console from one that does not, on this host, because the run never reaches
getty. The getty patches from 2.11BSD 480, 484, 487 and 493 are therefore
proposed with their build and size evidence and with this gate recorded as
failing on `main` before them, not as passing after them.

## Next measurement

Bisect between `a6b821f1` (#117) and `0b35cbcc` with `check-renode` alone,
about twenty minutes a point, on an idle host; or capture the UART through
the Robot harness by adding a test that waits on a line that cannot appear
and reads the terminal tester's buffer on failure, which the harness does
record.
