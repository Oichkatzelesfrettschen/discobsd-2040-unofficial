# A fourth and fifth multicall box: utilbox and adminbox

The original selection and collision analysis remains below. The current
packed-root layout, sort admission, and mutable-image measurements live in
`../MULTICALL-BSS-OVERLAY.md`; those measurements supersede the pre-overlay size
and memory discussion in this document.

At the original consolidation boundary, `sbin/utilbox` linked 19 usr.bin
tools into one a.out of 51,125 bytes (text 50,004 + data 1,089 + a.out
header), built the same way as
`sbin/box`, `sbin/sysbox` and `sbin/textbox`: each tool builds in its
own directory under `bin` or `usr.bin`, `ld -r` combines its objects,
`objcopy --redefine-sym main=<tool>_main --keep-global-symbol=<tool>_main`
localizes everything else, and `sbin/utilbox/utilbox.c.in` generates
the dispatch table. `sbin/adminbox` folded shutdown, reboot and sysctl
the same way, into a second, smaller a.out of 25,988 bytes (text
23,804 + data 2,152 + a.out header).

## utilbox: tool list, source, and size

Sizes are `text+data` of each tool's own object as combined into the
box (`sbin/utilbox/*.tool.o` after `ld -r`, before the one shared
libc links in). The standalone column is the tool's own a.out, built
and installed the same way every other `bin`/`usr.bin` program is.

| Tool | Source | In box (text+data) | Standalone a.out |
| --- | --- | ---: | ---: |
| expr | bin/expr | 9,330 | 16,892 |
| more | usr.bin/more | 11,515 | 22,592 |
| md5 | bin/md5 | 3,241 | 11,444 |
| id | usr.bin/id | 1,482 | 13,020 |
| xargs | usr.bin/xargs | 1,223 | 10,716 |
| printf | usr.bin/printf | 1,088 | 10,493 |
| tail | usr.bin/tail | 1,015 | 4,288 |
| cmp | usr.bin/cmp | 809 | 9,576 |
| du | usr.bin/du | 877 | 9,324 |
| uniq | usr.bin/uniq | 687 | 8,996 |
| tr | usr.bin/tr | 723 | 3,040 |
| touch | usr.bin/touch | 619 | 8,596 |
| tee | usr.bin/tee | 604 | 1,492 |
| uname | usr.bin/uname | 646 | 9,580 |
| wc | usr.bin/wc | 518 | 9,180 |
| head | usr.bin/head | 414 | 8,920 |
| env | usr.bin/env | 222 | 10,092 |
| basename | usr.bin/basename | 152 | 2,316 |
| tty | usr.bin/tty | 73 | 2,680 |
| **19 tools** | | **33,238** | **173,237** |
| utilbox total (with dispatcher + one libc) | | **51,125** | |
| **Net root savings** | | | **122,112 bytes** |

The "in box" column undercounts the shared win: each standalone a.out
also carries its own copy of libc's stdio, printf, malloc and exit
path, the same 8-16 KB textbox measured per tool. `more` and `expr`,
the two heaviest standalone tools here, use fewer than half their
standalone bytes once that copy is shared.

## adminbox: tool list, source, and size

| Tool | Source | In box (text+data) | Standalone a.out |
| --- | --- | ---: | ---: |
| sysctl | sbin/sysctl | 3,764 | 17,332 |
| shutdown | sbin/shutdown | 2,727 | 18,284 |
| reboot | sbin/reboot | 1,063 | 15,788 |
| **3 tools** | | **7,554** | **51,404** |
| adminbox total (with dispatcher + one libc) | | **25,988** | |
| **Net root savings** | | | **25,416 bytes** |

`halt` and `fasthalt` are names `sbin/reboot/reboot.c`'s own `myname`
check already answers to; the manifest links `/sbin/halt` straight to
`/sbin/adminbox`, and `sbin/adminbox/Makefile`'s `ALIASES=halt:reboot`
puts `halt` in the dispatch table too, so both invocation paths reach
`reboot_main`.

## Original combined result

utilbox and adminbox together recovered **147,528 bytes** (about 144
KiB) of root by folding 22 standalone a.outs into two multicall
binaries totaling 77,113 bytes.

## Tiny script applets in adminbox

The RP2040 root previously stored true, false, and nohup as three shell
scripts. Each script occupied one 1,024-byte filesystem block even though the
files contained only 7, 7, and 148 bytes. Adminbox now dispatches built-in C
applets for all three names. True and false return 0 and 1. Nohup ignores
SIGHUP and SIGTERM, increments process niceness by five, preserves command
argument boundaries and exit status, and appends terminal output to
`nohup.out` with standard error joined to standard output.

The built-ins add no BSS. A clean RP2040 build measured the following OMAGIC
and packed-root changes against the preceding main image:

| Quantity | Preceding main | Built-in applets | Change |
| --- | ---: | ---: | ---: |
| `a_text` | 22,560 | 23,380 | +820 |
| `a_data` | 1,976 | 1,984 | +8 |
| `a_bss` | 4,508 | 4,508 | 0 |
| Resident image | 29,044 | 29,872 | +828 |
| Mutable image | 6,484 | 6,492 | +8 |
| Packed adminbox | 19,774 | 20,473 | +699 |
| Packed adminbox blocks | 21 | 21 | 0 |

The packed-size increase stays inside adminbox's existing twentieth data
block; one indirect metadata block keeps the total at 21. Removing the three
script inodes therefore recovers three root blocks while adding only eight
bytes to every adminbox process's mutable image. The choice exploits two
independent granularity effects: file blocks make tiny scripts expensive,
while the packed multicall payload had 706 unused bytes in its final block.
Utilbox would have crossed from 43 to 44 packed blocks, so placing the same
applets there would have recovered only two root blocks.

The C implementation also repairs the script's unquoted `$*` expansion, which
lost empty arguments and split arguments containing spaces or wildcard
characters. The direct terminal notice writer retries interrupted writes. The
host test exercises exact argument vectors, status 0, 1, and 37, ignored
signals, the five-point priority increment, joined streams, terminal
redirection, and failed exec diagnostics.

The adminbox build rejects BSS or COMMON symbols from the built-in applet
object. The RP2040 build also rejects a packed image above 21 root blocks. The
paired gates protect mutable RAM and the block-granularity storage result
independently.

## What stayed out, and why

**find** (usr.bin/find, standalone, 19,556 bytes measured in this
worktree). `usr.bin/find/Makefile` builds three programs from one
directory -- find, bigram and code, each with its own `main` -- and
the box pattern's tool rule sweeps up every `*.o` in a tool's
directory (`ls $d/*.o | grep -v '.tool.o$'`) on the assumption that a
directory holds one program. `ld -r` on find's three objects together
fails outright:

```text
multiple definition of `prefix_length'
multiple definition of `main'
multiple definition of `oldpath'
```

Splitting the rule to link only `find.o` would work, but that is a
different rule from every other box in the tree, and the task is to
copy the box pattern exactly, not extend it per tool. find stays a
standalone a.out.

**sed and sort** (usr.bin/sed 16,576 bytes, usr.bin/sort 14,132 bytes
measured in this worktree, both in box form). Both build cleanly and
link cleanly -- `ld -r` and `objcopy --keep-global-symbol` report no
error -- but `nm -g --defined-only` on the localized `.tool.o` files
shows COMMON symbols that keep global (`C`) binding after
`--keep-global-symbol`, because a COMMON symbol cannot be scoped
local in ELF: it is a placeholder the final link still has to size
and place, and GNU ld merges same-named COMMON symbols across input
files into one allocation regardless of any other file's intended
scope. Combining all candidate tool.o files once (before deciding the
final set) surfaced three collision groups:

| Symbols | Tools sharing them |
| --- | --- |
| `loc1`, `loc2`, `locs`, `braslist`, `braelist` | expr, sed (old BRE engine) |
| `eargc`, `eargv`, `ibuf` | sed, sort |
| `nfiles` | more, sed, sort |
| `fields` | sort, uniq |

Left in the box, sed and sort would silently share `nfiles` (and sed
would share `loc1`/`loc2`/`locs`/`braslist`/`braelist` with expr, and
`eargc`/`eargv`/`ibuf` with sort) through `-fcommon` merging at the
final `ld` step -- a correctness bug with no link-time diagnostic,
since ld's job when merging COMMON symbols is exactly to pick one
allocation for a shared name. Dropping sed and sort clears every
symbol in the table above (each remaining occurrence becomes unique);
both stay standalone a.outs.

**nohup at the original boundary** (usr.bin/nohup).
`usr.bin/nohup/Makefile` installs `nohup.sh` directly, so the source-tool
localization mechanism had no C object to fold. The later built-in applet
described above removes that constraint without changing utilbox's generic
source-tool rule.

**passwd** (usr.bin/passwd, 15,340 bytes measured in this worktree,
installs mode 04755 -- setuid root). A multicall binary handing one
name root privilege hands every name in its dispatch table the same
privilege the moment execve loads that inode; nothing in
`sbin/box/box.c.in`'s dispatcher (or utilbox's, copied from it) drops
privilege before calling a tool's `_main`, and adding that would
change the pattern per box rather than reuse it. passwd stays a
standalone a.out, per the task's own instruction that a setuid tool
stays out unless the dispatcher is shown to drop privilege first.

**init, sh, login, getty, fsck, awk, the editors and the toolchain**
excluded per the task's scope, unchanged.

## Current exec and swap footprint

Root savings measure flash bytes, while exec admission measures the complete
resident OMAGIC image plus its initial stack. The current utilbox header at
merged main `93c0064b` reports:

| Quantity | Bytes |
| --- | ---: |
| `a_text` | 53,016 |
| `a_data` | 1,073 |
| `a_bss` | 4,271 |
| Resident image | 58,360 |
| Mutable image | 5,344 |
| Packed file | 42,998 |

`exec_aout.c` and `exec_hsaout.c` fold `a_text` into the single OMAGIC data
segment. `exec_estab()` therefore admits `a_text + a_data + a_bss + heap +
stack`, then records 58,360 bytes in `p_dsize` and 53,016 clean bytes in
`p_tsize`. The SMALL epoch supplies 144 KB. An image above that ceiling and
within the 16 KB SwapRAM bonus requests the exclusive LARGE epoch before the
kernel commits the old process image. Utilbox remains far below the SMALL
ceiling.

The clean-text contract creates the important RAM and swap intersection.
`vm_swap.c` writes `p_dsize - p_tsize`, so utilbox swaps only its 5,344-byte
mutable interval, stack, and u-area. Swap-in restores clean text from the
retained raw or packed executable and verifies the packed text CRC. The BSS
overlay therefore saves resident RAM, dirty swap extent, flash writes, and
compressed-swap churn simultaneously without pretending that text executes
in place.

The original 68,861-byte mutable utilbox measurement described the
pre-overlay COMMON layout. That image helped expose the lifetime opportunity,
but the current linker verifier rejects COMMON and writable NOBITS outside the
applet-private overlay before the final link.

## tool.o against libc: the other place a COMMON symbol can collide

The sed/sort/expr collisions above were tool.o against tool.o. The
final link is tool.o against `-lc`, which pulls in libc members to
resolve remaining undefined references; a tool's own un-localized
COMMON symbol sharing a name with a libc global would merge with it
the same way, silently:

```sh
arm-none-eabi-nm -g --defined-only *.tool.o |
    awk '{print $3}' | sort -u > /tmp/tsyms
arm-none-eabi-nm -g --defined-only ../../lib/libc.a |
    awk '$2 ~ /^[TDBC]$/ {print $3}' | sort -u > /tmp/lsyms
comm -12 /tmp/tsyms /tmp/lsyms
```

utilbox's 19 tool.o files share exactly one name with libc: `errno`,
which is meant to be the one shared instance every linked object
reads and writes -- not a collision. adminbox's three share none.
libc's own `regex.o` defines `braslist`/`braelist`/`loc1`/`loc2` too,
but as local (`b`) symbols (confirmed with `arm-none-eabi-nm
lib/libc.a`), so they do not appear in this comparison and do not
collide with `expr`'s own un-localized (still-global) copies of the
same names, which remain in utilbox's `.bss` unshared with anything
else linked in.

## Symbol-collision verification

For the 19 tools that shipped in utilbox and the 3 in adminbox:

```sh
for f in *.tool.o; do
    arm-none-eabi-nm -g --defined-only "$f" | awk -v f="$f" '{print $3, f}'
done | awk '{print $1}' | sort | uniq -c | sort -rn | awk '$1>1'
```

reports zero duplicate global names in either box. Each `<tool>_main`
appears exactly once per its own `.tool.o` and nowhere else; no bare
`main` symbol survives the `objcopy --redefine-sym` step. Confirmed
with `arm-none-eabi-nm utilbox.o` / `adminbox.o`: the dispatcher
object itself defines exactly one global `T main` and one `r tools`
table, referencing the 19 (or 6) declared `_main` entry points from
`@DECLS@`/`@TABLE@`.

`utilbox.o`'s and `adminbox.o`'s "no such tool" fallback strings
(`utilbox: no such tool` / `adminbox: no such tool`, confirmed present
via `arm-none-eabi-strings`) are byte-identical in structure to
`box.c.in`'s `box: no such tool` path: same `write(2, msg, len)` call
in the same unmatched-name branch, generated from the same
`box.c.in` template with the box's own name substituted. `utilbox
nosuchname` and `adminbox nosuchname` reach that line the same way
`box nosuchname` does; there is no divergent code path to diff at
runtime, only the message text, which the object file confirms.

## Filesystem verification

Extracting `distrib/rp2040/sdcard.img` through `fsutil --mount`
(FUSE) and `stat`-ing the linked names confirms real hard links, not
independent copies:

- `/usr/bin/utilbox` and all 19 linked names (`/usr/bin/id`,
  `/usr/bin/more`, ..., `/bin/expr`, `/bin/md5` across directories)
  report `Links: 20` and identical size (51,125 bytes).
- `/sbin/adminbox`, `/sbin/shutdown`, `/sbin/reboot`, `/sbin/sysctl`,
  `/sbin/halt`, `/usr/bin/true`, `/usr/bin/false`, and `/usr/bin/nohup`
  report `Links: 8`, inode 43, and identical packed size (20,473 bytes).

A clean `bmake MACHINE=rp2040 distribution` with the built-in applets installs
18 directories, 50 files, 19 devices, 76 hard links, and one symlink into the
988 KB root partition. `fsutil --check --partition=1` reports 91 files, 662
allocated blocks, and 309 free blocks. The preceding main image reports 94
files, 665 allocated blocks, and 306 free blocks. Run size-producing builds in
an isolated worktree so generated kernels and filesystem images stay outside
the canonical checkout.

`ps` reads `p_comm`, which the kernel sets from the exec'd `argv[0]`
in `exec_setupstack()` (`sys/kern/exec_subr.c`), independent of the
program's own dispatch logic; a hard link gives each name its own
directory entry and its own `argv[0]` when invoked by that name, so
`ps` continues to show `id`, `more`, `sysctl`, and so on rather than
`utilbox` or `adminbox`.

`sbin/utilbox/Makefile`'s `ALIASES=whoami:id groups:id logname:id`
compiles all three names into the dispatch table, so the box answers to
each when invoked as that `argv[0]`. The root manifest links only the
names that produce output: `whoami` prints the effective user and
`groups` prints the group names. `logname` stays out of the manifest
because `getlogin()` is a libc stub returning NULL
(`lib/libc/gen/getlogin.c`) over the `nosys` `setlogin` at syscall 43
(`sys/kern/init_sysent.c`), so a `logname` link would surface
`getlogin: Unknown error` rather than a login name. Every other utilbox
dispatch name carries a manifest link; `logname` is the one intentional
exception, and wiring it waits on kernel login-name storage.

## Board verification procedure

Flash the rebuilt image, then from the board's shell. Run the whole
block in one session, without a reboot in between: back-to-back execs
of different utilbox and adminbox members is the actual test of the
swap-map contiguity question above.

```sh
id                      # prints uid/gid, not "utilbox"
more /etc/motd          # pages the file
uniq /etc/passwd | wc -l
head -3 /etc/passwd; tail -3 /etc/passwd
tr a-z A-Z < /etc/motd
cmp /etc/passwd /etc/passwd; echo $?
basename /usr/bin/utilbox
env | head -1
printf '%s\n' hello
touch /tmp/utilbox-test && ls -l /tmp/utilbox-test
echo hi | tee /tmp/utilbox-test2
xargs echo < /etc/shells
du /etc/passwd
tty
uname -a
expr 2 + 2
md5 /etc/motd
true; echo $?             # prints 0
false; echo $?            # prints 1
rm -f nohup.out; nohup echo applet-test; cat nohup.out
ps | grep -E ' id$| more$| head$| tail$'   # confirm p_comm shows the linked name
sysctl -a | head -1
shutdown -h now  # or: reboot ; or: halt   -- run this LAST, it ends the session
```

Each command should behave exactly as its standalone version did
before the fold; `ps` output for any of them while running should
show the invoked name, not `utilbox` or `adminbox`.

## Historical 96 KB admission failure

The first utilbox predated both the BSS overlay and the 144 KB SMALL window.
Tail's 32,769-byte line buffer, tee's two 8,192-byte buffers, and du's
8,000-byte link table pushed `_end` to 0x2001cd00, 18 KB beyond the former
0x20018000 ceiling. The former admission calculation omitted `a_bss`, so an
exec could cross that ceiling and write globals into kernel RAM.

Current `exec_estab()` includes text, data, BSS, heap, and stack before
commit. The current utilbox resident image ends at 0x2000e3f8, well below the
SMALL top at 0x20024000. The LARGE epoch extends that top to 0x20028000 only
after SwapRAM evacuation. The overlay verifier and ELF layout fixtures also
reject COMMON, escaped NOBITS, cross-applet relocations, and an image that
exceeds its declared window before a packed root image can ship.
