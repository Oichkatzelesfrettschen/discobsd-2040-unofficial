# A fourth and fifth multicall box: utilbox and adminbox

The original selection and collision analysis remains below. The current
packed-root layout, sort admission, and mutable-image measurements live in
`multicall-bss-overlay.md`; those measurements supersede the pre-overlay size
and memory discussion in this document.

`sbin/utilbox` links 19 usr.bin tools into one a.out of 51,125 bytes
(text 50,004 + data 1,089 + a.out header), built the same way as
`sbin/box`, `sbin/sysbox` and `sbin/textbox`: each tool builds in its
own directory under `bin` or `usr.bin`, `ld -r` combines its objects,
`objcopy --redefine-sym main=<tool>_main --keep-global-symbol=<tool>_main`
localizes everything else, and `sbin/utilbox/utilbox.c.in` generates
the dispatch table. `sbin/adminbox` folds shutdown, reboot and sysctl
the same way, into a second, smaller a.out of 25,988 bytes (text
23,804 + data 2,152 + a.out header).

## utilbox: tool list, source, and size

Sizes are `text+data` of each tool's own object as combined into the
box (`sbin/utilbox/*.tool.o` after `ld -r`, before the one shared
libc links in). The standalone column is the tool's own a.out, built
and installed the same way every other `bin`/`usr.bin` program is.

| Tool | Source | In box (text+data) | Standalone a.out |
|---|---|---:|---:|
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
|---|---|---:|---:|
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

## Combined result

utilbox and adminbox together recover **147,528 bytes** (about 144
KiB) of root, folding 22 standalone a.outs into two multicall
binaries totaling 77,113 bytes.

## What stayed out, and why

**find** (usr.bin/find, standalone, 19,556 bytes measured in this
worktree). `usr.bin/find/Makefile` builds three programs from one
directory -- find, bigram and code, each with its own `main` -- and
the box pattern's tool rule sweeps up every `*.o` in a tool's
directory (`ls $d/*.o | grep -v '.tool.o$'`) on the assumption that a
directory holds one program. `ld -r` on find's three objects together
fails outright:

```
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
|---|---|
| `loc1`, `loc2`, `locs`, `braslist`, `braelist` | expr, sed (both carry the old regexp.c BRE engine) |
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

**nohup** (usr.bin/nohup). `usr.bin/nohup/Makefile` installs
`nohup.sh` directly; there is no C source and no a.out to fold.

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

## Memory footprint at exec

Root savings are flash bytes; the number that decides whether utilbox
actually runs is RAM committed at exec time. `sbin/box/Makefile`'s
own header names the risk this port already designed around: "two
images of 40 kbytes swap where one of 70 could not find a contiguous
run in the swap map." rp2040's `exec_aout.c` sets `epp->text.len = 0`
for a.out images -- text executes in place from flash, not from the
264-kbyte SRAM (`physmem = 264 * 1024` in
`sys/arch/rp2040/rp2040/machdep.c`) -- so the byte count that matters
per exec is `data + bss`, which becomes `p_dsize` in
`exec_subr.c:264`. Linking the elf before `elf2aout` strips it:

| Binary | text | data | bss | data+bss (p_dsize) |
|---|---:|---:|---:|---:|
| box (existing) | 34,528 | 1,764 | 10,192 | 11,956 |
| utilbox | 50,004 | 1,089 | 67,772 | 68,861 |
| adminbox | 23,804 | 2,152 | 5,724 | 7,876 |

`sys/sys/param.h` sets `MAXMEM (96*1024)`, the kernel's declared
per-process core ceiling, and rp2040 carries no override. `exec_estab()`
does check an overflow bound before committing a new image, but for
the a.out loader that check is `text.len + data.len + heap.len +
stack.len > MAXMEM` (`exec_subr.c:248`) with `text.len` and
`heap.len` both forced to 0 in `exec_aout.c`; `bss.len` never enters
that sum, so utilbox's 67,772 bytes of bss pass through unexamined by
that gate. The number that actually decides pass or fail is `p_dsize`
(`exec_subr.c:264`), the value the core allocator sizes the process's
memory request from once `exec_estab()` returns. utilbox's 68,861
bytes fits the *declared* 96 KB budget with about 27 KB to spare for
heap and stack, but that is arithmetic against the documented
ceiling, not confirmation the allocator's own accounting agrees;
adminbox's 7,876 is nowhere near either number and is not in
question. Per-tool `.tool.o` bss
(`arm-none-eabi-size sbin/utilbox/*.tool.o`) sums to well under 5 KB
across all 19 tools, confirming the 67,772-byte total is dominated by
COMMON symbols that only resolve to real `.bss` addresses at the
final link -- `expr`'s own copy of the old regexp.c engine
(`loc1`, `loc2`, `locs`, `braslist`, `braelist`) and a handful of
large static buffers in `more` and `cmp` account for most of it (see
`arm-none-eabi-nm --size-sort -S` on the linked elf).

Unlike a standalone tool -- one process, one fixed data+bss size, sized
for that tool alone -- utilbox commits the combined bss of all 19
members on every exec, whichever name invoked it: running `tty`
through utilbox costs the same 68,861 bytes as running `more`. This
port's swap is not a partition inside the filesystem image
(`SWAP_KBYTES=0`, `U_KBYTES=0` in `distrib/rp2040/Makefile.inc`); it
is a dedicated 384-kbyte flash region the kernel manages directly, and
`box.c.in`'s comment is about whether that region's free-block map has
one contiguous run big enough for the image being swapped in, not
about SRAM capacity. That is a runtime allocator question this
worktree's static checks cannot answer -- it depends on what else has
run and what the swap map looks like at the time -- so the board
verification below runs every utilbox and adminbox member name back
to back, without a reboot in between, as the actual test of whether
68,861 bytes finds room.

## tool.o against libc: the other place a COMMON symbol can collide

The sed/sort/expr collisions above were tool.o against tool.o. The
final link is tool.o against `-lc`, which pulls in libc members to
resolve remaining undefined references; a tool's own un-localized
COMMON symbol sharing a name with a libc global would merge with it
the same way, silently:

```
arm-none-eabi-nm -g --defined-only *.tool.o | awk '{print $3}' | sort -u > /tmp/tsyms
arm-none-eabi-nm -g --defined-only ../../lib/libc.a | awk '$2 ~ /^[TDBC]$/ {print $3}' | sort -u > /tmp/lsyms
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

```
for f in *.tool.o; do
    arm-none-eabi-nm -g --defined-only "$f" | awk -v f="$f" '{print $3, f}'
done | awk '{print $1}' | sort | uniq -c | sort -rn | awk '$1>1'
```

reports zero duplicate global names in either box. Each `<tool>_main`
appears exactly once per its own `.tool.o` and nowhere else; no bare
`main` symbol survives the `objcopy --redefine-sym` step. Confirmed
with `arm-none-eabi-nm utilbox.o` / `adminbox.o`: the dispatcher
object itself defines exactly one global `T main` and one `r tools`
table, referencing the 19 (or 3) declared `_main` entry points from
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
- `/sbin/adminbox`, `/sbin/shutdown`, `/sbin/reboot`, `/sbin/sysctl`
  and `/sbin/halt` report `Links: 5` and identical size (25,988
  bytes).

`bmake MACHINE=rp2040 distribution` and `bmake MACHINE=rp2040 fs`
both complete with no missing-file warnings and no manifest errors,
installing "17 directories, 44 files, 19 devices, 62 links, 1
symlinks" into a 988-kbyte root partition. `distribution` also
rebuilds the kernel and re-dirties the tracked build products under
`sys/arch/rp2040/compile/PICO*` (version counters, `unix`, `unix.bin`,
`unix.map`, `unix.uf2`) as a side effect of the ordinary build
sequence -- unrelated to this change and reverted with `git checkout
-- sys/arch/rp2040/compile/` before committing.

`ps` reads `p_comm`, which the kernel sets from the exec'd `argv[0]`
in `exec_setupstack()` (`sys/kern/exec_subr.c`), independent of the
program's own dispatch logic; a hard link gives each name its own
directory entry and its own `argv[0]` when invoked by that name, so
`ps` continues to show `id`, `more`, `sysctl`, and so on rather than
`utilbox` or `adminbox`.

## Board verification (not run here -- these commands were not executed against the board)

Flash the rebuilt image, then from the board's shell. Run the whole
block in one session, without a reboot in between: back-to-back execs
of different utilbox and adminbox members is the actual test of the
swap-map contiguity question above.

```
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
ps | grep -E ' id$| more$| head$| tail$'   # confirm p_comm shows the linked name
sysctl -a | head -1
shutdown -h now  # or: reboot ; or: halt   -- run this LAST, it ends the session
```

Each command should behave exactly as its standalone version did
before the fold; `ps` output for any of them while running should
show the invoked name, not `utilbox` or `adminbox`.

## The 96 KB window ceiling (found on the board)

A multicall box links every member's text, data and bss contiguously from
USER_DATA_START, and exec_aout loads the whole image into the 96 KB user
window (USER_DATA_SIZE); the process's globals live at their linked
addresses. So a box's _end symbol must stay below USER_DATA_END
(0x20018000). The first utilbox held tail (a 32769-byte line buffer), tee
(two 8192-byte buffers) and du (an 8000-byte link table); its _end reached
0x2001cd00, 18 KB past the window, and exec of any member wrote a global
into kernel RAM and wedged the kernel with no message. The three
big-buffer tools now ship standalone, where each fits alone, and utilbox's
_end is 0x2000de50. Check a box with `arm-none-eabi-nm box.elf | sort |
tail`: the highest B/D address must be below 0x20018000. box, sysbox,
textbox and gamebox were already within it.
