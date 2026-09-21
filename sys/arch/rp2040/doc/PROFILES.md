# Root filesystem profiles: what an image carries, and what that costs

The 1536 KB Dhara root yields 988 blocks of 1024 bytes, and the shipped
image leaves 96 of them free. Four optional feature closures account for
346 of the 892 blocks in use, so an image that keeps only the base system
boots with 442 free blocks -- a 4.6-fold increase in the space a user has
for files. This document is the authority for the closure markers in
`distrib/rp2040/mi.rp2040`, the profile declarations in
`distrib/rp2040/profiles`, the composer and checker
`distrib/rp2040/mkmanifest.py`, and the `check-fs-profiles` gate.

## What a profile selects

`distrib/rp2040/mi.rp2040` brackets each optional feature between
`#closure NAME` and `#endclosure NAME`. Every line outside a bracket is
the base system and always reaches the image. A closure opens more than
once, because the directory, the binary and the hard links that belong to
one feature sit in different sections of the manifest: `dir /usr/games`
stands in the directory block, `pack /usr/games/gamebox` in the executable
block, and the three entry points in the link block at the end.

`tools/fsutil/manifest.c` skips a comment line whole, so the markers leave
the manifest fsutil reads unchanged. `PROFILE=full` names every closure,
which makes the composed manifest byte-identical to the tracked manifest
with its marker lines removed.

## The closures, measured

Each cost below is the difference in free blocks between two images built
from this tree, read from `tools/bin/fsutil --verbose --partition=1
distrib/rp2040/sdcard.img`. A block is 1024 bytes, so a block count is
also a KiB count. The inode column is the difference in free inodes, which
`distrib/rp2040/Makefile.inc` fixes at 128 for the whole root.

| Closure | Blocks | Inodes | Installs |
| --- | --- | --- | --- |
| `coremark` | 15 | 1 | `/usr/bin/coremark` |
| `games` | 14 | 2 | `/usr/games`, `gamebox`, and hard links `fifteen`, `keen`, `bubble` |
| `pdp11` | 14 | 1 | `/usr/bin/pdp11` |
| `v6disk` | 156 | 2 | `/usr/v6`, `/usr/v6/root.rk` |
| `toolchain` + `toolchainlib` | 147 | 7 | `/usr/lib`, `cc`, `as`, `ld`, `smlrc`, `libc.a`, `crt0.o` |
| all four features | 346 | 13 | |

`/usr/v6/root.rk` is 1024000 bytes of V6 pack and costs 156 blocks, not
1000: `add_file()` in `tools/fsutil/fsutil.c` leaves a block of zeros
unmapped, and the kernel's `bmap()` reads an unmapped block as zeros, so
the pack's unused blocks occupy no flash. `toolchain` and `toolchainlib`
carry one figure because each requires the other, so no image exists that
holds one without the other; `/usr/lib/libc.a` alone installs at 56182
bytes.

## The profiles

| Profile | Closures | Free blocks | Free inodes |
| --- | --- | --- | --- |
| `base` | none | 442 | 43 |
| `benchmark` | `coremark` | 427 | 42 |
| `games` | `games` | 428 | 41 |
| `developer` | `toolchain`, `toolchainlib` | 295 | 36 |
| `retro-lab` | `pdp11`, `v6disk`, `toolchain`, `toolchainlib` | 125 | 33 |
| `full` | every closure | 96 | 30 |

`full` is the default. `distrib/rp2040/Makefile.inc` sets `PROFILE?=full`,
so a build that names no profile writes the image the port ships:

    bmake MACHINE=rp2040 distribution              # full
    bmake MACHINE=rp2040 PROFILE=base distribution # base
    bmake MACHINE=rp2040 PROFILE=base flash        # its UF2

The composed manifest lands at `distrib/rp2040/_manifest.<profile>`, which
the image rule concatenates with `distrib/rp2040/md.rp2040` the way it
always has. Two profiles built in turn overwrite one `sdcard.img`, so a
build that keeps both images copies the first aside.

## What the checker refuses

`tools/fsutil/fsutil.c`'s `add_hardlink()` prints `link source not found`
on stderr and returns, and fsutil's exit status stays 0. An image composed
with `link /usr/games/keen` and without `pack /usr/games/gamebox`
therefore builds clean and boots with a name that opens nothing. A missing
`/usr/lib/libc.a` is quieter still: no stage of the build reads it, and
the failure reaches the user as a compile on the board that cannot link.
`mkmanifest.py` decides both against the composed manifest before fsutil
runs, and a failure stops the build:

| Refusal | Condition |
| --- | --- |
| duplicate path | two directives name the same inode |
| missing parent | a path whose directory no selected line creates |
| dangling hard link | a `link` whose target no earlier selected line installs |
| dangling symlink | a `symlink` whose resolved target the image lacks |
| unmet closure | a selected closure whose `needs closure` is absent |
| unmet path | a selected closure whose `needs path` is absent |

A dependency is checked, never resolved. A profile that names `toolchain`
without `toolchainlib` fails rather than gaining the library silently, so
the profile line stays the whole statement of what an image carries.

`distrib/rp2040/profiles` declares the dependencies in three forms:

    profile NAME [CLOSURE ...]          a selectable composition
    closure NAME needs closure OTHER    OTHER must be selected too
    closure NAME needs path PATH ...    PATH must reach the image

`load()` also holds the two files to one closure set: a profile that names
a closure the manifest never opens, a declaration for a closure with no
manifest block, and a closure no profile reaches are each an error.

## Calibration

A checker that stopped deciding would report every profile clean, so
`--selftest` feeds it the compositions that must be rejected and asserts
the reason each one is rejected for. `check-fs-profiles` runs `--check-all`
and `--selftest` in that order, and both halves must pass:

| Case | Verdict | Reason asserted |
| --- | --- | --- |
| every declared profile | accept | composes |
| `toolchain` without `toolchainlib` | reject | needs closure toolchainlib |
| `toolchainlib` without `toolchain` | reject | needs closure toolchain |
| `pdp11` without `v6disk` | reject | needs closure v6disk |
| `games` with `pack /usr/games/gamebox` deleted | reject | hard link /usr/games/keen has no source |
| `toolchain` with `file /usr/lib/libc.a` deleted | reject | needs /usr/lib/libc.a |
| `games` with `dir /usr/games` deleted | reject | needs directory /usr/games |

The three deletion cases remove one directive from the composed manifest
in memory, which is how a hand-edited manifest breaks a closure that the
profile still names. `drop()` fails the run when its pattern matches
anything other than exactly one line, so a renamed path retires the case
rather than silently passing it.

## Adding a closure or a profile

1. Bracket the manifest lines with `#closure NAME` and `#endclosure NAME`,
   reopening the closure in each section that carries part of the feature.
2. Declare its dependencies in `distrib/rp2040/profiles`: a `needs closure`
   line for each closure it cannot work without, and a `needs path` line
   for each file whose absence breaks it silently.
3. Name it on the `profile` lines that should carry it, and on
   `profile full`, which is the default and must keep naming every closure.
4. Add a rejection case to `selftest()` in `distrib/rp2040/mkmanifest.py`
   for the way the new closure breaks, and run
   `bmake MACHINE=rp2040 check-fs-profiles`.
5. Build the profile and record its free-block count in the tables above.

`sys/arch/rp2040/doc/STORAGE.md` carries the flash budget the profiles
divide, and `sys/arch/rp2040/doc/TESTING.md` names the gate.

## The zone in force

A filesystem profile decides what the image carries; `TZ_ZONEINFO` decides
whether libc can read it. The two are one decision when zone files are
involved, so they are stated together here.

`localtime(3)` answers from one of two sources. Built with `TZ_ZONEINFO`, it
reads a zone file: `tzload()` opens the path `TZ` names or
`/etc/localtime`, parses the first-version layout, and keeps the
transitions in `struct state` -- 370 transition times, their type indices,
ten local time types and the abbreviation characters. Built without it, the
zone is the single offset the kernel keeps, which `gettimeofday(2)` reports
as `tz_minuteswest` and `tzsetkernel()` installs; `struct state` then holds
one type.

No profile in this tree ships a zone file. `share/zoneinfo` builds them and
`zic(8)` is here, so an installation can add them, but neither
`/etc/localtime` nor `/usr/share/zoneinfo` appears in any manifest. Without
one, `tzload()` cannot succeed and `tzset()` reaches `tzsetkernel()` every
time, so the file-backed form is weight that answers nothing.

Measured, per program that calls `localtime(3)`:

| | text | zero-initialized data | stack while parsing |
| --- | --- | --- | --- |
| `TZ_ZONEINFO` | 1387 | 2084 | 2033 plus MAXPATHLEN |
| default | 578 | 124 | none |

Four shipped binaries reach it: `box` (for `ls`), `sysbox` (for `date`),
`tar` and `login`. Between the two builds they differ by 836 to 880 bytes
each, and the image's free blocks by nine.

    bmake MACHINE=rp2040 distribution                 # 102 free blocks
    bmake MACHINE=rp2040 TZ_ZONEINFO=1 distribution   #  93 free blocks

A system that installs zone files builds with `TZ_ZONEINFO=1`. Selecting it
without installing them costs the space and changes no answer.

`tests/libc_contracts` `check-zone` runs in both shapes. The file-backed one
is where the reader's bounds are measured, because they are the bounds on a
file; the default one asserts that a zone file named in `TZ` is not opened
and that the answer is the kernel's offset. The two cannot be confused by
accident: `tzload()` is not declared in the default build, so a call to it
fails to compile rather than reaching a file.
