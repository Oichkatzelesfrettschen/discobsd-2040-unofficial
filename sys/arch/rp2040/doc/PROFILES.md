# Root filesystem profiles: what an image carries, and what that costs

The 1536 KB Dhara root yields 988 blocks of 1024 bytes. The maintained
`full` image leaves 239 blocks free; an image that keeps only the base system
leaves 447. The default image excludes the PDP-11 emulator and V6 pack. An
explicit `BUILD_PDP11_V6=yes` image adds both and leaves 69 blocks free with
the `full` profile. A named profile fixes the default content of an image, and
the Make variables `IMAGE_WITH` and `IMAGE_WITHOUT` add and remove single
closures or groups for one build. This document is the authority for the
closure markers in `distrib/rp2040/mi.rp2040`, the profile, group and
multicall declarations in `distrib/rp2040/profiles`, the composer and checker
`distrib/rp2040/mkmanifest.py`, the selection variables in
`distrib/rp2040/Makefile.inc`, and the `check-fs-profiles` gate.

## What a profile selects

`distrib/rp2040/mi.rp2040` brackets each optional feature between
`#closure NAME` and `#endclosure NAME`. Every line outside a bracket is
the base system and always reaches the image. A closure opens more than
once, because the directory, the binary and the hard links that belong to
one feature sit in different sections of the manifest: `dir /usr/games`
stands in the directory block (closure `gamesdir`), `pack /usr/games/gamebox`
in the executable block (closure `gamebox`), and each entry point's link in the
link block at the end, in a closure named for the entry point (`fifteen`,
`keen`, `bubble`, `fourfall`, `coil`, `ridge`). The puzzles follow the same layout
for `puzzlebox` and its entry points `flood`, `samegame`, `unruly` and
`lightup`, and install into `/usr/games`, which `gamesdir` creates.

A closure is the smallest unit an image selects. A group is a name for
several closures (or groups), declared with `group` in
`distrib/rp2040/profiles`, and is accepted wherever a closure name is: on a
`profile` line, in `IMAGE_WITH` and `IMAGE_WITHOUT`, and after `needs closure`.
The profile lines keep the group names that the closure names had before the
split, so a profile or command that named `games`, `puzzles`, `toolchain` or
`calc-bignum` selects the same files:

| Group | Closures |
| --- | --- |
| `games` | `gamesdir`, `gamebox`, `fifteen`, `keen`, `bubble`, `fourfall`, `coil` |
| `puzzles` | `puzzlebox`, `flood`, `samegame`, `unruly`, `lightup` |
| `toolchain` | `as`, `ld`, `smlrc`, `cc` |
| `calc-bignum` | `bc`, `dc` |

The remaining closures stand alone: `coremark`, `calc`, `toolchainlib`,
`sdcard`, `lcdgfx` and `ridge`. The `sdcard` closure carries the `/mnt` mount point
and the device nodes of the PicoCalc's SD card (block major 5, raw major 11 in
`conf.c`); the `picocalc` profile names it, `full` does not, and the PICO
kernels have no driver behind those majors. Its `needs path` line holds the
mount point and all six nodes, so a manifest block that loses one stops the
build. The `lcdgfx` closure carries the one node `/dev/lcdgfx` (minor 1 of the
console's character major 10, mode 666) that the PICOCALC kernels serve; the
`picocalc` profile names it and no other does. The `ridge` closure carries the
hard link `/usr/games/ridge` to `gamebox`, a member that draws through the tile
atlas and the raw key session of the PICOCALC kernels. It sits outside the
`games` group, which `full` keeps, and declares `closure ridge needs closure
lcdgfx`, so a selection that keeps `ridge` without the device node, and the
`full` profile with `ridge` added, are refused.
`bmake MACHINE=rp2040 list-closures` prints every closure with its groups,
dependencies, install paths and cost, and marks the ones the current
`PROFILE`, `IMAGE_WITH` and `IMAGE_WITHOUT` keep.

`tools/fsutil/manifest.c` skips a comment line whole, so the markers leave
the manifest fsutil reads unchanged. `PROFILE=picocalc` names every
closure, which makes its composed manifest byte-identical to the tracked
manifest with its marker lines removed; `full` names every closure except
the PicoCalc's own.

### Multicall binaries

`games/gamebox` and `games/puzzlebox` dispatch on `argv[0]`, and each entry
point is a hard link to the binary. A `multicall` line in the profiles file
names the binary's closure, its path and its members:

    multicall gamebox /usr/games/gamebox fifteen keen bubble fourfall coil ridge

The declaration does three things. Each member closure gains a checked
`needs closure gamebox`. A selection that keeps the binary and none of its
members is refused, because the program would dispatch nothing. And the
composer verifies that the links the image installs equal the members the
selection keeps, so a dispatch table and a link set cannot diverge.

The build links only the selected members. `distrib/rp2040/Makefile.inc` runs
`mkmanifest.py --members=gamebox` (and `puzzlebox`) for the selected profile and
exports the result as `GAMEBOX_TOOLS` and `PUZZLEBOX_TOOLS`; the two
Makefiles read them into `MEMBERS`, which drives the object list, the
generated dispatch table and the install. A build that leaves them unset
links every member, and `TOOLS` keeps naming the full set that
`check-multicall-composition` compares with the manifest. A selection that
drops a binary builds nothing for it. A binary with one member needs no
overlap fence between applets, so `tools/generate_multicall_bss_overlay.sh`
omits `NOCROSSREFS` for a single object.

## The closures, measured

Each cost below is the difference in free blocks between two images built
from this tree, read from `tools/bin/fsutil --verbose --partition=1
distrib/rp2040/sdcard.img`: the image of `base` alone, and the image of
`base` plus the closure plus the closures it needs. A block is 1024 bytes, so
a block count is also a KiB count. The inode column is the difference in free
inodes, which `distrib/rp2040/Makefile.inc` fixes at 128 for the whole root.
`list-closures` prints these figures from the `cost` lines in
`distrib/rp2040/profiles`; they are measured, not computed.

| Closure or group | Blocks | Inodes | Installs |
| --- | --- | --- | --- |
| `coremark` | 16 | 1 | `/usr/bin/coremark` |
| `as` | 25 | 1 | `/usr/bin/as` |
| `ld` | 18 | 1 | `/usr/bin/ld` |
| `smlrc` | 42 | 1 | `/usr/libexec/smlrc` |
| `as`, `ld`, `smlrc` together | 84 | 3 | the three programs |
| `cc` + `toolchainlib` (the `toolchain` group with its library) | 133 | 7 | `/usr/lib`, `cc`, `libc.a`, `crt0.o` on top of the three programs |
| `bc` | 40 | 2 | OpenBSD `bc` (31,176 bytes packed) and `/usr/share/misc/bc.library` (5,125); PicoCalc only |
| `dc` | 21 | 1 | OpenBSD `dc` (18,644 bytes packed); PicoCalc only |
| `calc-bignum` (`bc` + `dc`) | 60 | 3 | the three files above |
| `calc` | 33 | 1 | `/usr/bin/calc` (31,352 bytes packed); PicoCalc only |
| `sdcard` | 1 | 7 | `/mnt` and the device nodes `/dev/sd0`, `sd0a`, `sd0b`, `rsd0`, `rsd0a`, `rsd0b`; PicoCalc only |
| `lcdgfx` | 0 | 1 | the device node `/dev/lcdgfx` (character major 10, minor 1, mode 666); PicoCalc only |
| `gamesdir` | 1 | 1 | `/usr/games` |
| `games` group | 24 | 2 | `/usr/games`, `gamebox`, and hard links `fifteen`, `keen`, `bubble`, `fourfall`, `coil` |
| `puzzles` group with `gamesdir` | 37 | 2 | `puzzlebox` and hard links `flood`, `samegame`, `unruly`, `lightup` |
| `games` and `puzzles` | 60 | 3 | both groups |
| `ridge` with `gamesdir`, `gamebox` and `lcdgfx` | 11 | 3 | the hard link `/usr/games/ridge` and what it needs (`gamebox` links `ridge` only when the selection keeps it); PicoCalc only |
| all maintained closures (`picocalc`) | 306 | 23 | |
| legacy `pdp11` | 14 | 1 | `/usr/bin/pdp11` |
| legacy `v6disk` | 157 | 2 | `/usr/v6`, `/usr/v6/root.rk` |

`cc` and `toolchainlib` carry one figure because each requires the other, so
no image holds one without the other; `/usr/lib/libc.a` alone installs at
43010 bytes. The three programs under them are separate closures: `as`,
`ld` and `smlrc` each stand alone, and `cc` needs all three and the library.
`bc` and `dc` are independent too: `bc` evaluates through the dc evaluator
linked into itself and needs no `/usr/bin/dc`. The 60 blocks of the two
together are one fewer than the sum of the rows (40 + 21), because entries
that two closures add can share a directory block (inferred from the
allocation unit, not traced block by block). The `sdcard` closure needs no
other closure: its seven inodes are `/mnt` and six device nodes, and its one
block is the `/mnt` directory. The closure rows (`coremark` 16, the toolchain
with its library 133, `games` 24, `puzzles` beyond `games` 36, `calc-bignum`
60, `calc` 33, `sdcard` 1) sum to 303, and the 7 blocks `ridge` adds inside
the full set bring them to 310, four blocks more than the 306 of the image
that holds every closure, for the same reason.

The multicall members cost far less inside the full set than alone, because
the binary, its directory and the libc linkage they share are paid once. The
first column below is a one-member image (`base` plus `gamesdir`, the binary
and the member, built with the member as the only `GAMEBOX_TOOLS` or
`PUZZLEBOX_TOOLS` word); the second is the blocks recovered by removing the
member from the `games` profile, which holds both groups at 60 blocks:

| Member | Alone: blocks | Alone: inodes | Marginal: blocks |
| --- | --- | --- | --- |
| `fifteen` | 8 | 2 | 1 |
| `keen` | 10 | 2 | 3 |
| `bubble` | 10 | 2 | 3 |
| `fourfall` | 15 | 2 | 6 |
| `coil` | 8 | 2 | 5 |
| `flood` | 18 | 2 | 4 |
| `samegame` | 18 | 2 | 5 |
| `unruly` | 21 | 2 | 6 |
| `lightup` | 23 | 2 | 8 |
| `ridge` | 11 | 3 | 7 |

The `ridge` row is not part of the 60: it is measured on the `base` profile with
`gamesdir`, `gamebox`, `ridge` and `lcdgfx` (the third inode is the device
node) and, for the marginal figure, as the difference between the `picocalc`
image built with `IMAGE_WITHOUT=ridge` (148 free blocks) and as shipped (141);
`gamebox` grows from 21,660 to 29,518 packed bytes. The members and the two
binaries account for the whole 60: the nine
marginal figures sum to 41, `gamesdir` is 1, and the two binaries and their
shared linkage are the remaining 18. Removing a member removes a hard link,
which costs no inode, so the marginal inode figure is 0 for every row.
The member figures were measured on the tree that split the closures and not
repeated here: this branch changes no game, libc or startup source, and its
only userland difference is `sbin/sysctl`.

The two legacy rows are measured only in an image built with
`BUILD_PDP11_V6=yes`; they are absent from the maintained manifest and
profiles. Their sum is the difference between the `base` profile with and
without `BUILD_PDP11_V6=yes` on this tree (171 blocks, 3 inodes).
`/usr/v6/root.rk` is 1024000 bytes of V6 pack and its closure costs 157 blocks, not
1000: `add_file()` in `tools/fsutil/fsutil.c` leaves a block of zeros
unmapped, and the kernel's `bmap()` reads an unmapped block as zeros, so
the pack's unused blocks occupy no flash.

## The profiles

| Profile | Closures | Free blocks | Free inodes |
| --- | --- | --- | --- |
| `base` | none | 447 | 43 |
| `benchmark` | `coremark` | 431 | 42 |
| `games` | `games`, `puzzles` | 387 | 40 |
| `developer` | `toolchain`, `toolchainlib` | 314 | 36 |
| `full` | every closure except the PicoCalc's, including `puzzles` | 239 | 32 |
| `picocalc` | `full` plus `calc-bignum`, `calc`, `sdcard`, `lcdgfx` and `ridge`, so also `puzzles` | 141 | 20 |

Each profile row comes from `bmake MACHINE=rp2040 PROFILE=NAME fs` followed
by `tools/bin/rp2040/fsutil --verbose --partition=1
distrib/rp2040/sdcard.img` on this tree, after a `distribution` build that
links every multicall member; `full` and `picocalc` are the default images of
`BOARD=PICO` (`sdcard.img`) and `BOARD=PICOCALC` (`sdcard-PICOCALC.img`). The `base`, `benchmark`, `games`,
`developer` and `full` rows read one block below the values this document
recorded for main before this branch merged (448, 432, 388, 315 and 240):
the base image of this tree holds 447 free blocks where main's held 448.
`sbin/sysctl` is the only userland file this branch changes; the cause of the
block was not traced. The closure costs above are differences of images on
one tree and equal main's. The `lcdgfx` cost is such a difference too: the
`base` image of this tree holds 447 free blocks and 43 free inodes, and 447
and 42 with `lcdgfx` added, so the node costs one inode and no block. The
`picocalc` image holds 148 and 20 when built with `IMAGE_WITHOUT=ridge`; the
row reads 141 free blocks and 20 free inodes as shipped: `ridge` takes seven
blocks and no inode, because its entry point is a hard link. A `full` image
built in the same run holds 241 free blocks, two above the recorded 239;
`ridge` is outside `full` and its `gamebox` is unchanged, so the difference
predates the closure and the `full` row is left as recorded.

The opt-in option augments each profile with the emulator and guest pack. It
does not add another maintained profile or change what `full` means:

| Profile with `BUILD_PDP11_V6=yes` | Free blocks | Free inodes |
| --- | ---: | ---: |
| `base` | 276 | 40 |
| `benchmark` | 261 | 39 |
| `games` | 216 | 37 |
| `developer` | 144 | 33 |
| `full` | 69 | 29 |

Each row of this table comes from `bmake MACHINE=rp2040 BUILD_PDP11_V6=yes
PROFILE=NAME fs` after a `BUILD_PDP11_V6=yes distribution` build, read with
the same `fsutil --verbose` command. The base row sits 171 blocks and 3
inodes below the maintained `base` row, which equals the two legacy rows
together (14 + 157 blocks, 1 + 2 inodes); neither legacy closure builds
alone, because `v6disk` reaches no profile without `pdp11`.

## Choosing what an image carries

`BOARD` picks the default profile. `distrib/rp2040/Makefile.inc` sets
`BOARD?=PICO` and `PROFILE?=` the board's profile, `full` for the PICO board
and `picocalc` for the PicoCalc and for `BOARD=PICOCALC_PSRAM` (the PicoCalc
kernel with the PSRAM swap driver, `PICOCALC.md`), and refuses any other
`BOARD`. Every kernel
boots every profile's root, so the board choice decides what the image
carries, not whether it boots:

    bmake MACHINE=rp2040 distribution              # full
    bmake MACHINE=rp2040 BOARD=PICOCALC distribution # picocalc
    bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution # picocalc, PSRAM kernel
    bmake MACHINE=rp2040 PROFILE=base distribution # base
    bmake MACHINE=rp2040 PROFILE=base flash        # its UF2
    bmake MACHINE=rp2040 BUILD_PDP11_V6=yes \
        legacy-pdp11-v6-distribution               # full plus PDP-11/V6

`IMAGE_WITH` and `IMAGE_WITHOUT` take space-separated closure or group names
and apply on top of whichever profile the board or `PROFILE` selects:

    bmake MACHINE=rp2040 BOARD=PICOCALC \
        IMAGE_WITHOUT="toolchain toolchainlib" distribution  # picocalc, no compiler
    bmake MACHINE=rp2040 IMAGE_WITHOUT="keen coil" distribution
                                                             # full, two games fewer
    bmake MACHINE=rp2040 PROFILE=base \
        IMAGE_WITH="gamesdir puzzlebox lightup" distribution # base plus one puzzle
    bmake MACHINE=rp2040 BOARD=PICOCALC IMAGE_WITHOUT=calc-bignum list-closures
    bmake MACHINE=rp2040 BOARD=PICOCALC IMAGE_WITHOUT=sdcard distribution
                                                             # picocalc, no SD nodes

The two variables pass to `mkmanifest.py` as `--with` and `--without`, and
the same selection reaches the games Makefiles as `GAMEBOX_TOOLS` and
`PUZZLEBOX_TOOLS`. Each name holds to letters, digits, `-` and `_`, because
the words reach the shell unquoted; the Makefile stops on any other character.
`build` and `fs` wait for `check-image-selection`, which composes the selection
and prints the refusal before any program builds. The composer applies the
variables as follows:

- A name that is neither a closure nor a group is refused, in either list.
- A closure that one list adds and the other removes is refused, whether the
  lists name it directly or through a group (`IMAGE_WITH=games
  IMAGE_WITHOUT=keen` names `keen` twice and fails; to build the games
  without `keen`, start from a profile that already holds them).
- Adding a closure the profile already holds, or removing one it lacks,
  changes nothing.
- The dependencies are then checked, not resolved. `IMAGE_WITHOUT=toolchain`
  fails with `closure toolchainlib needs closure cc, which this profile does
  not select`; the working selection names both, `IMAGE_WITHOUT="toolchain
  toolchainlib"`. Removing `gamebox` while its members stay, or every member
  while `gamebox` stays, fails the same way.
- A selection leaves `distrib/rp2040/_manifest.<profile>` recomposed on every
  invocation, so a changed selection reaches the image without a `clean`;
  `mkmanifest.py` leaves the file untouched when its text is unchanged.

The maintained composed manifest lands at
`distrib/rp2040/_manifest.<profile>`. The legacy option writes
`distrib/rp2040/_manifest.<profile>.pdp11-v6`, which
the image rule concatenates with `distrib/rp2040/md.rp2040` the way it
always has. The output path follows `BOARD` alone (`BOARD_OUT_SUFFIX` in
`distrib/rp2040/Makefile.inc`): `BOARD=PICO` writes `sdcard.img`, `flash.bin`
and `flash.uf2`, and any other board writes `sdcard-${BOARD}.img`,
`flash-${BOARD}.bin` and `flash-${BOARD}.uf2`. Two profiles built in turn for
one board overwrite that board's image, so a build that keeps both copies the
first aside; `PROFILE`, `IMAGE_WITH` and `IMAGE_WITHOUT` do not fork the
output path.

Set `PYTHON` before the build, as every Python tool in the tree requires:
`distrib/rp2040/Makefile.inc` asks `mkmanifest.py` for the member lists while
it parses, and an empty `PYTHON` leaves them unset, which links every member.

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
| unmet closure | a selected closure whose `needs closure` is absent (a `multicall` member needs its binary) |
| unmet path | a selected closure whose `needs path` is absent |
| empty multicall | a selected multicall binary none of whose members is selected |
| undispatched or unlinked member | a selected member with no link to its binary, or a link to a binary that does not dispatch it |
| unknown name | an `IMAGE_WITH` or `IMAGE_WITHOUT` word that is neither a closure nor a group |
| added and removed | a closure that `IMAGE_WITH` and `IMAGE_WITHOUT` both reach |

A dependency is checked, never resolved. A selection that names `toolchain`
without `toolchainlib` fails rather than gaining the library silently, so
the profile line and the two variables stay the whole statement of what an
image carries.

`distrib/rp2040/profiles` declares the profiles and closures in these forms:

    profile NAME [NAME ...]             a selectable composition
    group NAME NAME ...                 NAME stands for each closure or group listed
    closure NAME needs closure OTHER    OTHER (a closure or group) must be selected too
    closure NAME needs path PATH ...    PATH must reach the image
    multicall CLOSURE PATH NAME ...     CLOSURE installs PATH, which dispatches the
                                        member closures NAME ...
    cost NAME BLOCKS INODES             the measured cost (see above)

`load()` also holds the two files to one closure set: a profile that names
a closure or group the manifest never opens, a declaration for a closure with
no manifest block, a group that is also a closure or contains itself, a
`multicall` whose binary or member links the manifest does not install, and a
closure no profile reaches are each an error.

## Calibration

A checker that stopped deciding would report every profile clean, so
`--selftest` feeds it the compositions that must be rejected and asserts
the reason each one is rejected for. `check-fs-profiles` runs `--check-all`,
`--selftest` and `distrib/rp2040/check-image-selection.sh` in that order, and
all three must pass. The shell script repeats the refusals at the Make
layer (a gate whose `make` ignored the variables would fail it) and reads
back the member lists the games Makefiles receive.

| Case | Verdict | Reason asserted |
| --- | --- | --- |
| every declared profile | accept | composes |
| `toolchain` without `toolchainlib` | reject | needs closure toolchainlib |
| `toolchainlib` without `toolchain` | reject | closure toolchainlib needs closure cc |
| `games` with `pack /usr/games/gamebox` deleted | reject | hard link /usr/games/keen has no source |
| `games` and `puzzles` with `pack /usr/games/puzzlebox` deleted | reject | hard link /usr/games/flood has no source |
| `puzzles` without `gamesdir` | reject | closure puzzlebox needs closure gamesdir |
| `toolchain` with `file /usr/lib/libc.a` deleted | reject | needs /usr/lib/libc.a |
| `sdcard` with `bdev /dev/sd0` deleted | reject | closure sdcard needs /dev/sd0, which this profile does not install |
| `sdcard` with `dir /mnt` deleted | reject | closure sdcard needs /mnt, which this profile does not install |
| `games` with `dir /usr/games` deleted | reject | needs directory /usr/games |
| `games` with `link /usr/games/keen` deleted | reject | multicall gamebox dispatches keen, which no link in this profile reaches |
| `--with nosuch` | reject | --with names unknown closure or group nosuch |
| `--without nosuch` | reject | --without names unknown closure or group nosuch |
| `--with calc --without calc` | reject | closure calc is both added (as calc) and removed (as calc) |
| `--with games --without keen` | reject | closure keen is both added (as games) and removed (as keen) |
| `full` without `toolchain` | reject | closure toolchainlib needs closure cc, which this profile does not select |
| `full` without `gamebox` | reject | closure keen needs closure gamebox, which this profile does not select |
| `full` without the five gamebox members | reject | closure gamebox is selected with none of its members |
| `full` with `ridge` | reject | closure ridge needs closure lcdgfx, which this profile does not select |
| `picocalc` without `lcdgfx` | reject | closure ridge needs closure lcdgfx, which this profile does not select |
| `picocalc` without `keen`, with a part that needs `/usr/games/keen` | reject | closure calc needs /usr/games/keen, which this profile does not install |
| `full` without `toolchain` and `toolchainlib` | accept | composes |
| `full` without `keen` | accept | composes; gamebox keeps `fifteen bubble fourfall coil`, puzzlebox keeps all four |
| `full` without `gamebox` and its five members | accept | composes; gamebox keeps nothing |
| `base` with `gamesdir`, `puzzlebox`, `lightup` | accept | composes; puzzlebox keeps `lightup` |
| `full` with `dc`; `base` with `as` and `ld` | accept | composes |
| `picocalc` without `sdcard` | accept | composes |
| `picocalc` | accept | composes; gamebox keeps `fifteen keen bubble fourfall coil ridge` |
| `picocalc` without `ridge` and `lcdgfx` | accept | composes; gamebox keeps `fifteen keen bubble fourfall coil` |

The declaration checks have their own cases, each fed as extra profile text:
a group declared twice, a group that contains itself, a group named like a
closure, a group member that names nothing, a `cost` or `needs closure` that
names no closure, a `multicall` member with no link to its binary, a
`multicall` binary its closure never installs, a member claimed by two
binaries, and a profile that names nothing.

When `BUILD_PDP11_V6=yes` supplies the legacy manifest and profile fragments,
the same selftest adds three refusal cases:

| Case | Verdict | Reason asserted |
| --- | --- | --- |
| `pdp11` without `v6disk` | reject | needs closure v6disk |
| PDP-11 closure without its emulator | reject | needs /usr/bin/pdp11 |
| V6 closure without its guest pack | reject | needs /usr/v6/root.rk |

The deletion cases remove one directive from the composed manifest
in memory, which is how a hand-edited manifest breaks a closure that the
profile still names. `drop()` fails the run when its pattern matches
anything other than exactly one line, so a renamed path retires the case
rather than silently passing it.

## Adding a closure or a profile

1. Bracket the manifest lines with `#closure NAME` and `#endclosure NAME`,
   reopening the closure in each section that carries part of the feature.
   Give each independent program its own closure, and join closures that
   work only together with a `group`.
2. Declare its dependencies in `distrib/rp2040/profiles`: a `needs closure`
   line for each closure it cannot work without, and a `needs path` line
   for each file whose absence breaks it silently. A new entry point of a
   multicall binary joins that binary's `multicall` line and the `TOOLS`
   list of its Makefile, and carries its own `link` closure.
3. Name a maintained closure or group on the `profile` lines that should carry
   it and on `profile full`, which must keep naming every maintained closure. A
   default-off legacy closure belongs in its own profile and manifest
   fragments and enters composition through its guarded Make option.
4. Add a rejection case to `selftest()` in `distrib/rp2040/mkmanifest.py`
   for the way the new closure breaks, and run
   `bmake MACHINE=rp2040 check-fs-profiles`.
5. Build the profile, record its free-block count in the tables above, and
   add a `cost` line to `distrib/rp2040/profiles`.

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
