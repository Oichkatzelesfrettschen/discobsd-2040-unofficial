# DiscoBSD/rp2040 root audit, 2026-09-15

The shipped root on the Raspberry Pi Pico carries 111 program names, 20
device nodes and 12 files in `/etc` in 665 of 979 one-kilobyte blocks.
This audit matches every one of them against `distrib/rp2040/mi.rp2040`,
`distrib/rp2040/md.rp2040`, the kernel device switch in
`sys/arch/rp2040/rp2040/conf.c`, the seven multicall dispatch tables, and
the documentation that describes them.

Board evidence is `research/discobsd-rp2040/board-inventory-2026-09-15.txt`
in this repository, captured today from a running board. Source evidence is
the port checkout `../discobsd-pico-unofficial` at HEAD `c2a196f7`; paths
below are relative to that tree unless they name this repository. The
README audited is `README.md` in the port tree, read from the
`host-readme` worktree before that worktree was removed mid-session and
spot-verified identical at lines 31 to 41 against the merged copy.

No command in this audit touched the board or any serial device, and the
port tree was read only.

## 1. Device tree

Twenty nodes, all of them accounted for by a manifest entry and all but
one of them by a live driver. `mi.rp2040` supplies every character node
and `/dev/swap`; `md.rp2040` supplies the three flash block nodes.

| node | kind | major | minor | board mode | manifest | switch slot | driver | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `console` | char | 0 | 0 | `crw--w----` `operator` | `mi.rp2040`, default 0664 | `cdevsw[0]` | `cnopen`, `sys/kern/cons.c`, redirected to major 7 by `CONS_MAJOR=UARTUSB_MAJOR` | live; owner and mode are getty and login at open, not the manifest |
| `mem` | char | 1 | 0 | `crw-rw-r--` | 0664 | `cdevsw[1]` | `mmrw`, `sys/dev/mem.c` | live |
| `kmem` | char | 1 | 1 | `crw-rw-r--` | 0664 | `cdevsw[1]` | `mmrw` | live |
| `null` | char | 1 | 2 | `crw-rw-rw-` | `mode 666` | `cdevsw[1]` | `mmrw` | live; octal parse confirmed |
| `zero` | char | 1 | 3 | `crw-rw-rw-` | `mode 666` | `cdevsw[1]` | `mmrw` | live; octal parse confirmed |
| `tty` | char | 2 | 0 | `crw-rw-rw-` | `mode 666` | `cdevsw[2]` | `syopen`, `sys/kern/tty_conf.c` | live; octal parse confirmed |
| `stdin` | char | 3 | 0 | `crw-rw-r--` | 0664 | `cdevsw[3]` | `fdopen`, `sys/kern/kern_descrip.c` | live |
| `stdout` | char | 3 | 1 | `crw-rw-r--` | 0664 | `cdevsw[3]` | `fdopen` | live |
| `stderr` | char | 3 | 2 | `crw-rw-r--` | 0664 | `cdevsw[3]` | `fdopen` | live |
| `temp0` | char | 4 | 0 | `crw-rw-r--` | 0664 | `cdevsw[4]` | `swcopen`, `sys/dev/swap.c` | live; `fsck` uses it every boot |
| `temp1` | char | 4 | 1 | `crw-rw-r--` | 0664 | `cdevsw[4]` | `swcopen` | live; no shipped consumer |
| `temp2` | char | 4 | 2 | `crw-rw-r--` | 0664 | `cdevsw[4]` | `swcopen` | live; no shipped consumer |
| `klog` | char | 5 | 0 | `crw-rw-r--` | 0664 | `cdevsw[5]` | none: `NOCDEV` | dead; `open` returns ENXIO |
| `tty0` | char | 6 | 0 | `crw-rw-r--` | 0664 | `cdevsw[6]` | `uartopen`, `sys/arch/rp2040/dev/uart.c` | live |
| `ttyUSB0` | char | 7 | 0 | `crw-rw-r--` | 0664 | `cdevsw[7]` | `usbopen`, `sys/arch/rp2040/dev/usb.c` | live; the console |
| `fl0` | block | 0 | 0 | `brw-rw-r--` | `md.rp2040`, 0664 | `bdevsw[0]` | `flopen`, `sys/arch/rp2040/dev/flash.c` | live; whole Dhara region |
| `fl0a` | block | 0 | 1 | `brw-rw-r--` | `md.rp2040`, 0664 | `bdevsw[0]` | `flopen` | live; root, mounted per `/etc/fstab` |
| `fl1` | block | 0 | 8 | `brw-rw-r--` | `md.rp2040`, 0664 | `bdevsw[0]` | `flopen` | live; raw swap |
| `swap` | block | 4 | 64 | `brw-rw-r--` | `mi.rp2040`, 0664 | `bdevsw[4]` | `swopen`, `sys/dev/swap.c` | live; minor 64 is the magic unit `swopen` forwards to `swapdev` |

### Modes

Every node's board mode equals what the manifest asks for. The three
entries carrying an explicit `mode 666` -- `null`, `zero` and `tty` --
read `crw-rw-rw-` on the board, so the octal parse fix landed in the
shipped image; the remaining nodes inherit `filemode 0664` and read
`crw-rw-r--` or `brw-rw-r--`. `/dev/console` is the sole exception and
is not a manifest question: `/usr/libexec/getty` and `/usr/bin/login`
set the owner to `operator` and the mode to 0620 when the line opens.

### Majors with no driver, and drivers with no node

`cdevsw[5]` is compiled as `NOCDEV`. `sys/arch/rp2040/rp2040/conf.c`
guards the log entry with `#ifdef LOG_ENABLED`, and the generated
`sys/arch/rp2040/compile/PICO/Makefile` carries 24 `PARAM` lines, none of
them `-DLOG_ENABLED`; `subr_log.o` is not in the compile directory.
`/dev/klog` therefore answers `noopen`, which returns ENXIO. No shipped
program opens it, so the defect is silent.

Character majors 8 through 18 are `NOCDEV` placeholders (pty, gpio, adc,
spi, glcd, pwm, picga, gpanel, skel, sdio) with no nodes, which is
consistent: `PTY_ENABLED` and `SDIO_ENABLED` are undefined and the RP2040
has no SDIO controller. Block majors 1, 2, 3 and 5 are `NOBDEV`
placeholders with no nodes. Every live switch entry has at least one node,
so no driver is stranded.

### The three `/dev/tempN` devices

`sys/dev/swap.c` defines `NTMP 3` and a `struct swtemp td[NTMP]` of three
16-bit fields: start block, size, and an append high-water mark. A
`TFALLOC` ioctl carrying a positive `off_t` frees any prior extent and
allocates a new one from `swapmap`, the same map the pager draws from; a
zero argument releases the extent. A temporary device with no allocation
refuses reads and writes and prints `temp%d: attempt to write with no
allocation`. Writes in strict block order from zero starting at `t_next`
carry `B_SWAPIMAGE`, the erase-once append path; any partial, duplicate
or out-of-order write sets `t_next` to `TD_APPEND_DISABLED` permanently
and the extent falls back to copy-through-scratch rewrites for its
lifetime.

Major 4 names two different drivers: `bdevsw[4]` is the swap proxy
(`swopen`, `swstrategy`) and `cdevsw[4]` is the temporary allocator
(`swcopen`, `swcread`). They share a source file, not a switch slot.

Of the three nodes, only `temp0` has a consumer on this root:
`sbin/fsck/setup.c:112` opens `/dev/temp0` as its scratch file and
`sbin/fsck/utilities.c:184` sizes it with `TFALLOC`, and `/etc/rc` runs
`fsck -p` on every autoboot. `usr.sbin/talloc` is the only other `TFALLOC`
caller in the tree and it is not in the manifest, so `temp1` and `temp2`
ship unused.

## 2. Program tree

The manifest and the board agree exactly: 111 names in the six program
directories on each side, with no name in one and not the other.

Provenance: the inventory captured `ls` without `-l` for these six
directories, so the board evidence is the set of names. The `kind` and
`dispatcher` columns below are what `distrib/rp2040/mi.rp2040` installs,
not what was read off the flash. The single test that closes the gap is
`ls -li` across the six directories on the board, comparing inode numbers
and link counts against each manifest `link`/`target` pair, plus
`tools/bin/hsaout -s` on each `pack` target.

### Dispatch agreement

Seven multicall executables ship. For each, the Makefile's `TOOLS` and
`ALIASES` generate the dispatch table, and the manifest hard-links the
names; the three columns are compared here.

| box | source | `TOOLS` + `ALIASES` | table entries | manifest links | agreement |
| --- | --- | --- | --- | --- | --- |
| `box` | `sbin/box` | 18 + 1 | 19 | 19 (`chown` lands in `/sbin`) | complete |
| `sysbox` | `sbin/sysbox` | 7 + 0 | 7 | 7 | complete |
| `adminbox` | `sbin/adminbox` | 3 + 3 builtin + 1 alias | 7 | 7 | complete |
| `textbox` | `sbin/textbox` | 15 + 0 | 15 | 15 | complete |
| `utilbox` | `sbin/utilbox` | 19 + 3 | 22 | 21 | `logname` has no directory entry |
| `grepbox` | `sbin/grepbox` | 2 + 0 | 2 | 2 | complete |
| `gamebox` | `games/gamebox` | 3 + 0 | 3 | 3 | complete |

`logname` is the one dispatch-table name with no directory entry, the
same shape as the `whoami` case fixed yesterday: `sbin/utilbox/Makefile`
lists `logname:id` in `ALIASES`, `sbin/utilbox/utilbox.c:62` carries
`{ "logname", id_main }`, and neither `distrib/rp2040/mi.rp2040` nor the
board has `/usr/bin/logname`. The table entry costs eight bytes of text
and reaches nothing.

No directory entry lacks a dispatcher. Two hard-link pairs dispatch
outside a box and are correct: `uncompress` and `zcat` link to
`/usr/bin/compress`, whose own `argv[0]` test at
`usr.bin/compress/compress.c:1040` handles both. `vi` and `vim` link to
`/usr/bin/stevie`, which runs no `argv[0]` test at all -- the names are
aliases for one editor rather than dispatch entries, which is the
intended behavior and not a defect.

### Every name

### `/bin`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `[` | hard link | `/bin/box` | yes | present |
| `box` | packed a.out | -- | dispatcher | present |
| `cat` | hard link | `/bin/box` | yes | present |
| `chgrp` | hard link | `/bin/box` | yes | present |
| `chmod` | hard link | `/bin/box` | yes | present |
| `cp` | hard link | `/bin/box` | yes | present |
| `date` | hard link | `/bin/sysbox` | yes | present |
| `dd` | hard link | `/bin/sysbox` | yes | present |
| `df` | hard link | `/bin/sysbox` | yes | present |
| `echo` | hard link | `/bin/box` | yes | present |
| `ed` | packed a.out | -- | -- | present |
| `expr` | hard link | `/usr/bin/utilbox` | yes | present |
| `hostname` | hard link | `/bin/box` | yes | present |
| `kill` | hard link | `/bin/box` | yes | present |
| `ln` | hard link | `/bin/box` | yes | present |
| `ls` | hard link | `/bin/box` | yes | present |
| `md5` | hard link | `/usr/bin/utilbox` | yes | present |
| `mkdir` | hard link | `/bin/box` | yes | present |
| `mv` | hard link | `/bin/box` | yes | present |
| `ps` | packed a.out | -- | -- | present |
| `pwd` | hard link | `/bin/box` | yes | present |
| `rm` | hard link | `/bin/box` | yes | present |
| `rmdir` | hard link | `/bin/box` | yes | present |
| `sh` | packed a.out | -- | -- | present |
| `sleep` | hard link | `/bin/box` | yes | present |
| `stty` | hard link | `/bin/sysbox` | yes | present |
| `sync` | hard link | `/bin/box` | yes | present |
| `sysbox` | packed a.out | -- | dispatcher | present |
| `tar` | packed a.out | -- | -- | present |
| `test` | hard link | `/bin/box` | yes | present |

### `/sbin`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `adminbox` | packed a.out | -- | dispatcher | present |
| `chown` | hard link | `/bin/box` | yes | present |
| `fsck` | packed a.out | -- | -- | present |
| `halt` | hard link | `/sbin/adminbox` | yes | present |
| `init` | packed a.out | -- | -- | present |
| `mknod` | hard link | `/bin/sysbox` | yes | present |
| `mount` | hard link | `/bin/sysbox` | yes | present |
| `reboot` | hard link | `/sbin/adminbox` | yes | present |
| `shutdown` | hard link | `/sbin/adminbox` | yes | present |
| `sysctl` | hard link | `/sbin/adminbox` | yes | present |
| `umount` | hard link | `/bin/sysbox` | yes | present |

### `/usr/bin`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `as` | packed a.out | -- | -- | present |
| `awk` | packed a.out | -- | -- | present |
| `basename` | hard link | `/usr/bin/utilbox` | yes | present |
| `cc` | script (`file`) | -- | -- | present |
| `cksum` | hard link | `/usr/bin/textbox` | yes | present |
| `cmp` | hard link | `/usr/bin/utilbox` | yes | present |
| `comm` | hard link | `/usr/bin/textbox` | yes | present |
| `compress` | packed a.out | -- | -- | present |
| `coremark` | packed a.out | -- | -- | present |
| `cpio` | packed a.out | -- | -- | present |
| `cut` | hard link | `/usr/bin/textbox` | yes | present |
| `deroff` | hard link | `/usr/bin/textbox` | yes | present |
| `dirname` | hard link | `/usr/bin/textbox` | yes | present |
| `du` | packed a.out | -- | -- | present |
| `env` | hard link | `/usr/bin/utilbox` | yes | present |
| `expand` | hard link | `/usr/bin/textbox` | yes | present |
| `false` | hard link | `/sbin/adminbox` | yes | present |
| `fgrep` | hard link | `/usr/bin/grepbox` | yes | present |
| `find` | packed a.out | -- | -- | present |
| `fold` | hard link | `/usr/bin/textbox` | yes | present |
| `grep` | hard link | `/usr/bin/grepbox` | yes | present |
| `grepbox` | packed a.out | -- | dispatcher | present |
| `groups` | hard link | `/usr/bin/utilbox` | yes | present |
| `head` | hard link | `/usr/bin/utilbox` | yes | present |
| `id` | hard link | `/usr/bin/utilbox` | yes | present |
| `ld` | packed a.out | -- | -- | present |
| `login` | packed a.out | -- | -- | present |
| `look` | hard link | `/usr/bin/textbox` | yes | present |
| `md` | hard link | `/usr/bin/utilbox` | yes | present |
| `menu` | packed a.out | -- | -- | present |
| `more` | hard link | `/usr/bin/utilbox` | yes | present |
| `nl` | hard link | `/usr/bin/textbox` | yes | present |
| `nohup` | hard link | `/sbin/adminbox` | yes | present |
| `passwd` | packed a.out | -- | -- | present |
| `paste` | hard link | `/usr/bin/textbox` | yes | present |
| `printf` | hard link | `/usr/bin/utilbox` | yes | present |
| `resize` | hard link | `/usr/bin/utilbox` | yes | present |
| `rev` | hard link | `/usr/bin/textbox` | yes | present |
| `sed` | packed a.out | -- | -- | present |
| `seq` | hard link | `/usr/bin/textbox` | yes | present |
| `sort` | hard link | `/usr/bin/utilbox` | yes | present |
| `stevie` | packed a.out | -- | -- | present |
| `su` | packed a.out | -- | -- | present |
| `tail` | packed a.out | -- | -- | present |
| `tee` | packed a.out | -- | -- | present |
| `textbox` | packed a.out | -- | dispatcher | present |
| `touch` | hard link | `/usr/bin/utilbox` | yes | present |
| `tr` | hard link | `/usr/bin/utilbox` | yes | present |
| `true` | hard link | `/sbin/adminbox` | yes | present |
| `tty` | hard link | `/usr/bin/utilbox` | yes | present |
| `uname` | hard link | `/usr/bin/utilbox` | yes | present |
| `uncompress` | hard link | `/usr/bin/compress` | argv[0] in compress | present |
| `unexpand` | hard link | `/usr/bin/textbox` | yes | present |
| `uniq` | hard link | `/usr/bin/utilbox` | yes | present |
| `utilbox` | packed a.out | -- | dispatcher | present |
| `uudecode` | hard link | `/usr/bin/textbox` | yes | present |
| `uuencode` | hard link | `/usr/bin/textbox` | yes | present |
| `vi` | hard link | `/usr/bin/stevie` | argv[0] in stevie | present |
| `vim` | hard link | `/usr/bin/stevie` | argv[0] in stevie | present |
| `wc` | hard link | `/usr/bin/utilbox` | yes | present |
| `whoami` | hard link | `/usr/bin/utilbox` | yes | present |
| `xargs` | hard link | `/usr/bin/utilbox` | yes | present |
| `zcat` | hard link | `/usr/bin/compress` | argv[0] in compress | present |

### `/usr/sbin`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `update` | packed a.out | -- | -- | present |

### `/usr/libexec`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `getty` | packed a.out | -- | -- | present |
| `smlrc` | packed a.out | -- | -- | present |

### `/usr/games`

| name | manifest kind | dispatcher | dispatch entry | board |
| --- | --- | --- | --- | --- |
| `bubble` | hard link | `/usr/games/gamebox` | yes | present |
| `fifteen` | hard link | `/usr/games/gamebox` | yes | present |
| `gamebox` | packed a.out | -- | dispatcher | present |
| `keen` | hard link | `/usr/games/gamebox` | yes | present |

## 3. Size comparison

Counting rule: an entry is a name in `/bin`, `/sbin`, `/usr/bin`,
`/usr/sbin`, `/usr/libexec` or `/usr/games`. Seven of the 111 entries are
the dispatchers themselves (`box`, `sysbox`, `adminbox`, `textbox`,
`utilbox`, `grepbox`, `gamebox`), which a user does not invoke by those
names, so the invocable count is 104.

| root | entries | scope | source |
| --- | --- | --- | --- |
| DiscoBSD/rp2040, this board | 111 (104 invocable) | six program directories | board inventory and `distrib/rp2040/mi.rp2040` |
| Sixth Edition Unix | about 80 | `/bin` | knowledge, not fetched |
| Seventh Edition Unix | about 200 | `/bin` and `/usr/bin` | knowledge, not fetched |
| Upstream DiscoBSD SD-card root | 243 | six program directories | `distrib/base/mi` |

The V6 and V7 figures are from knowledge of the Unix Heritage Society
trees; nothing was fetched over the network for this audit.

Space, from `df` on the board today:

| measure | value |
| --- | --- |
| root file system | 979 one-kilobyte blocks |
| used | 665 |
| available | 314 |
| capacity | 67 percent |

### What makes this root larger than V6 and V7

Four additions account for the gap over V6's roughly 80 commands, and
they are all things V6 and V7 did not ship in `/bin`:

- The sbase text utilities `textbox` imports from suckless.org under
  MIT/X, named in `sbin/textbox/Makefile`: `cut`, `paste`, `seq`,
  `dirname`, `nl`, `cksum`, `expand`, `unexpand`, `uuencode` and
  `uudecode`. `textbox` adds the tree's own `fold`, `rev` and `comm` and
  the Seventh Edition `look` and `deroff`.
- The tree's own `utilbox` members that V6 and V7 did not have in
  `/bin`: `xargs`, `printf`, `basename`, `env`, `touch`, `id`, `whoami`,
  `groups`, `resize` and `md`.
- Games: `fifteen`, `keen` and `bubble` in one `gamebox`.
- A native C toolchain on the board: `/usr/bin/cc` driving
  `/usr/libexec/smlrc`, `/usr/bin/as` and `/usr/bin/ld` against
  `/usr/lib/libc.a` and `crt0.o`. V6 shipped `cc` too, but this chain is
  a Smaller C with a Thumb-1 back end rather than a PDP-11 compiler.
- Packed multicall boxes, which is what makes the count affordable: 78
  of the 111 entries are hard links, so the 104 invocable commands cost
  seven a.out images plus the standalone programs rather than 104 copies
  of libc. Without an MMU there is no shared library, so every standalone
  a.out carries its own libc; the boxes are the only way this many names
  fit 979 blocks.

### What it lacks

160 of upstream DiscoBSD's 243 names are absent. Every name below is
from that difference, grouped: no networking or uucp (`telnet`, `uucp`
and its ten companions, `mail`, `rmail`, `write`, `wall`), no `cron` or
`crontab`, no documentation tools (`man`, `apropos`, `whatis`,
`whereis`, `pr`, `col`), no development beyond the C chain (`make`,
`m4`, `yacc`, `ar`, `ranlib`, `nm`, `size`, `strip`, `diff`, `od`), no
alternative languages (`bc`, `dc`, `basic`, `forth`, `scm`, `tclsh`,
`picoc`), no accounting or system statistics (`vmstat`, `iostat`,
`pstat`, `fstat`, `uptime`, `w`, `who`, `last`), and none of upstream's
38 `/usr/games` entries -- this root's three games are its own.
`egrep` and `split` are absent too, and the tree has no `dmesg` at all.

## 4. Documentation audit

### `README.md`, the RP2040 sections

| claim | verdict | evidence or correction |
| --- | --- | --- |
| 128 KB kernel, 1.5 MB root, 384 KB swap in 2 MB QSPI flash | VERIFIED | `sys/arch/rp2040/doc/BOOT-MAP.md` section 2 memory map; `sys/arch/rp2040/conf/RP2040.ld` |
| 264 KB of SRAM | VERIFIED | inventory `hw.physmem=270336` |
| kernel is `sys/arch/rp2040`, configs PICO and PICO_UART | VERIFIED | both directories exist under `sys/arch/rp2040/compile/` |
| root is 1.5 MB wear-leveled by Dhara, about 980 KB of blocks | VERIFIED | `BOOT-MAP.md` 1536K region, 989K logical; `df` reports 979 |
| swap is 384 KB raw plus a 16 KB compressed RAM tier | VERIFIED | `Config` options `SWAPRAM` and `SWAPRAM_KB=16` |
| user program window is 144 KB | VERIFIED | `sys/arch/rp2040/include/machparam.h:126` `USER_DATA_SIZE (144 * 1024)` |
| "160 KB for a program that asks" | WRONG | `USER_DATA_SIZE` is a compile-time constant with no request path; the only 160 in the tree is a comment in `sys/arch/rp2040/conf/RP2040.ld:16` about a different chip's RAM. Delete the parenthesis. |
| console is USB CDC-ACM at 115200 8N1 | VERIFIED | `sys/arch/rp2040/dev/usb.c` `usbopen` sets `B115200`; `USER-ACCESS.md` |
| console opens 80x24 | VERIFIED | `usb.c` `usbopen` sets `ws_row 24`, `ws_col 80` |
| login `operator`, no password | UNVERIFIED | `/etc/passwd` gives operator password reference 139 and `/etc/shadow` was not captured. Test: log in as `operator` on the console and record whether a password is asked. |
| `su` to root without a password | UNVERIFIED | `/etc/group` puts operator in `wheel`, which is necessary but not sufficient. Test: as operator, run `su` and record the prompt. |
| 111 names across the six directories | VERIFIED | board inventory, counted above |
| most of them hard links into seven multicall executables | VERIFIED | 78 of 111 are `link` entries; seven dispatchers |
| `cc` drives Smaller C, `as` and `ld` against `/usr/lib/libc.a` | VERIFIED | manifest ships `/usr/bin/cc` as a script and packs `as`, `ld`, `smlrc`, plus `libc.a` and `crt0.o`; inventory shows all six |
| the `uname -a` and `df` transcript | VERIFIED | identical to the inventory, character for character |
| `sys/arch/rp2040/doc/` holds BOOT-MAP, STORAGE, USER-ACCESS and `research/` | VERIFIED | directory listing; `DATASHEET-INDEX.md` and `MULTICALL-BSS-OVERLAY.md` are there too and go unmentioned |
| the board boots within a few seconds to a login prompt | UNVERIFIED | Test: power-cycle with a terminal already attached and time the prompt. |
| run `sync` or `halt` before unplugging | VERIFIED | both ship; `halt` links to `adminbox`, `sync` to `box` |
| the ten `check-*` make targets | VERIFIED | all ten are rules in the port tree's `Makefile` |
| "The console prints nothing after attaching: press Enter once. The board printed its prompt before you connected." | WRONG, and it contradicts the design | `sys/arch/rp2040/dev/usb.c:33` states the ring exists so "a terminal opened after boot sees the boot messages rather than nothing". Today's capture saw neither the full messages nor nothing, but a truncated stream. See below. |
| stevie assumes 80x24 and `resize` fixes it | VERIFIED | `usbopen` default and `USER-ACCESS.md` `resize` description |
| `discobsd-term`, `discobsd-console`, the udev rule, the packages | UNVERIFIED here | host-side; `distrib/rp2040/host/` carries the sources and packaging, but nothing in this audit exercised them |

### `sys/arch/rp2040/doc/USER-ACCESS.md`

| claim | verdict | evidence or correction |
| --- | --- | --- |
| no network interface, so the console is the only access | VERIFIED | `conf.c` configures no network device, and `sys/arch/rp2040/compile/PICO/Config` declares none |
| line is 115200 8N1, no flow control | VERIFIED | `usbopen` sets `B115200`; CDC line coding is accepted and stored, not enforced |
| the kernel reports USB serial `rp2040` | VERIFIED | `sys/arch/rp2040/dev/usb.c` `usb_strings[3]` is `"rp2040"` |
| USB vendor 2e8a, product 000a | VERIFIED | `usb_device_desc` bytes `0x8a, 0x2e` and `0x0a, 0x00` |
| `71-discobsd-pico.rules` adds `/dev/discobsd` | VERIFIED | the file is in `distrib/rp2040/host/` |
| `discobsd-connect` wraps tio, picocom, minicom, cu | VERIFIED | the script is in `distrib/rp2040/host/` |
| operator is in `wheel` | VERIFIED | `/etc/group` line `wheel:*:0:root,operator` |
| direct root login on the console is refused | VERIFIED | `/etc/ttys` marks `console` `insecure` and `usr.bin/login/login.c:241` refuses uid 0 on a terminal `rootterm` rejects |
| `/etc/ttys` sets the console TERM to xterm | VERIFIED | the shipped `/etc/ttys` console line ends `xterm on insecure` |
| `/etc/termcap` carries xterm, vt100, vt102 and ansi | WRONG | the shipped termcap has `cons25` with the `ansi` alias, `vt100`, `xterm`, `xterm-basic` and `xterm-color`; there is no `vt102` entry. Drop vt102 from the sentence or add the entry. |
| the kernel opens the console at 80x24 (`dev/usb.c`, `dev/uart.c`) | VERIFIED | `usbopen`; `uart.c` carries the same default |
| `resize` uses ESC [ 6 n and TIOCSWINSZ | UNVERIFIED | Test: on the board, resize the emulator, run `resize`, and check `stty size`. |
| one session holds the console at a time | VERIFIED by design | `usbopen` honors `TS_XCLUDE`; the host tools serialize on the serial device |
| `picotool reboot -u -f` reaches BOOTSEL through the console reset interface | VERIFIED in source | `usb.c` declares the vendor-class reset interface with `RESET_INTERFACE_SUBCLASS`; the round trip is a board test |

### The truncated boot replay, and why there is no `dmesg`

Today's capture, with the host opening the port during boot, read
`DiscoBSD 2.7 (  oec0,1)` and then jumped to
`/dev/fl0a: 91 files, 664 used, 315 free`. The banner the kernel prints
is `sys/arch/rp2040/compile/PICO/vers.c`: `DiscoBSD 2.7 (PICO) #1 1085:`
followed by the build date, the build directory, the three `cpu:` lines
from `machdep.c:449`, and the `fl0:` lines from `flash.c:491`.

The mechanism is in `sys/arch/rp2040/dev/usb.c`. Output is a FIFO, not a
replay log: `usb_tx_put` appends one byte and, when the ring is full,
discards the oldest by advancing `tx_tail`, so the ring holds the last
`USB_TXRING` bytes and nothing is ever sent twice. `usb_tx_kick` gates on
`usbd.configured`, copies at most 64 bytes into DPSRAM and advances
`tx_tail` past them immediately. A late opener therefore receives
whatever survives in the ring at the moment the host starts draining the
bulk IN endpoint, and two paths drop bytes that have already left the
ring: `usb_configure` clears `tx_busy` on every SET_CONFIGURATION and bus
reset, and `REQ_SET_INTERFACE` does the same, each discarding the up-to-
64-byte packet sitting in DPSRAM. Enumeration while the kernel is
printing costs one such packet per event.

The loss is expected and bounded by that design. Reordering is not
occurring: `usbintr` runs `usb_service` under `splhigh`, which on ARMv6-M
is PRIMASK and masks every interrupt, so a second `usb_tx_kick` cannot
enter between the DPSRAM copy loop and `tx_tail += n`; and `usbinit`
runs `bzero(&usbd, sizeof(usbd))` at `usb.c:880`, so `tx_head` and
`tx_tail` start equal and the uncleared `.scratch.usb_tx_ring` bytes can
never be sent. A subsequence test supports that: every character
of `DiscoBSD 2.7 (  oec0,1` appears in order within a stream
reconstructed from the banner in `vers.c` and the `printf` format
strings at `machdep.c:449` and `flash.c:491`, whose runtime argument
values are not captured and were supplied by hand. Only the final `)`
runs past that reconstruction. The capture is consistent with pure loss
of long runs and not with duplication or reordering, measured against a
reconstructed reference.

The check is partial because the full expected stream was not captured.
The test that closes it: boot the PICO_UART kernel with a terminal
already attached to `/dev/tty0`, capture the complete stream, then boot
the PICO kernel opening the USB port at a known offset into boot and diff
the two, which separates what the ring drops from what the host misses.

There is no on-board way to recover what was lost. The tree contains no
`dmesg` in any directory, and `/dev/klog`, the node a `dmesg` would read,
has no driver in this kernel. The USB transmit ring is the only path boot
messages take, and once drained they are gone.

### `sys/arch/rp2040/doc/STORAGE.md`

The document is dated 2026-09-11 and most of its numbers are now wrong.

| claim | verdict | correction |
| --- | --- | --- |
| "`bin/box` and `bin/sysbox`" | WRONG | the directories are `sbin/box` and `sbin/sysbox` |
| two multicall binaries, "a third box for the text tools is the next step" | WRONG | seven ship: box, sysbox, adminbox, textbox, utilbox, grepbox, gamebox |
| "the 29 programs in `/bin`" | WRONG | 30 names in `/bin` today |
| "the manifest hard-links the twenty-six names to it" | WRONG | 78 of 111 entries are hard links |
| "51 files and 66 links, 175 KB free (df: 796 of 971 KB used)" | WRONG | `df` today: 979 blocks, 665 used, 314 free |
| "the `re` screen editor" | WRONG | the shipped editor is `stevie`, linked as `vi` and `vim` |
| "textbox carries ... fold, rev and comm" | INCOMPLETE | `look` and `deroff` joined it |
| "tar (24 KB)" listed as left out | WRONG | `/bin/tar` ships |
| "the games" listed as left out | WRONG | `gamebox` ships fifteen, keen and bubble |
| Dhara yields 989 KB of blocks from 1536 KB | VERIFIED | matches `BOOT-MAP.md` |

### `sys/arch/rp2040/doc/BOOT-MAP.md`

| claim | verdict | correction |
| --- | --- | --- |
| memory map: boot2 256 B, kernel 128 K, root 1536 K, swap 384 K, user 144 K | VERIFIED | `sys/arch/rp2040/conf/RP2040.ld` and `machparam.h` |
| "the port lives in this DiscoBSD tree on branch `rp2040-port`, branch `rp2040-port`" | WRONG | the phrase is duplicated, and the port tree's HEAD is `main` |
| "datasheets are under `docs/rp2040/`, with `INDEX.md`" | WRONG for the port tree | no `docs/` directory exists there; the datasheets are `docs/rp2040/` in this notes repository, and the port carries `sys/arch/rp2040/doc/DATASHEET-INDEX.md` |
| boot2 assembles byte-identical to pico-sdk `bs2_default.bin` | UNVERIFIED here | Test: `bmake MACHINE=rp2040` and compare `boot2.bin` against the SDK artifact |

### `distrib/rp2040/host/README.md`

Not audited against a board: it documents the host package's
development, tests and packaging, none of which the board inventory
touches. The commands it names (`discobsd-term`, `discobsd-web`,
`discobsd-link`, `discobsd-console`, `discobsd-connect`) all have
sources in `distrib/rp2040/host/`.

### Manifest and startup files

| claim | verdict | correction |
| --- | --- | --- |
| `mi.rp2040` header: "a 1078-kbyte root" | WRONG | `df` reports 979 blocks; `md.rp2040` and `etc/rc.rp2040` say 795 kbytes. Three numbers describe one root. |
| `mi.rp2040`: "See `bin/box/Makefile`" | WRONG | `sbin/box/Makefile` |
| `mi.rp2040`: "halt and fasthalt are names reboot's own argv[0] check answers to" | WRONG | `adminbox`'s `ALIASES` is `halt:reboot` alone; no `fasthalt` entry or node exists |
| `mi.rp2040`: adminbox and gamebox each described as "a fourth a.out", textbox as "a third" | WRONG | the ordinals date from a three-box root; there are seven |
| `/etc/passwd` gives operator the home `/operator` | WRONG | no manifest `dir` creates it; the manifest creates `/home` and `/root`. Login lands in a directory that does not exist. |
| `/etc/shells` lists `/bin/csh` | WRONG | `csh` is not in the manifest and not on the board |
| `/etc/ttys` enables only `console`; `tty0` and `ttyUSB0` are off | VERIFIED | shipped `/etc/ttys`; both nodes exist so either can be turned on |
| `/etc/rc` is `etc/rc.rp2040`, without the motd rewrite and without `cron` | VERIFIED | the shipped 1068-byte `rc` matches `etc/rc.rp2040`; the generic `etc/rc` calls `cron`, which this root does not ship |

## 5. Findings, by severity

1. `/dev/klog` has no driver and the tree has no `dmesg`, so boot
   messages have no on-board reader and nothing recovers what the USB
   ring drops. `sys/arch/rp2040/rp2040/conf.c` guards `cdevsw[5]` with
   `LOG_ENABLED`, which the generated
   `sys/arch/rp2040/compile/PICO/Makefile` never defines, and
   `subr_log.o` is not built. Fix in
   `sys/arch/rp2040/compile/PICO/Config` by configuring the log device,
   which also makes a `dmesg` worth adding; the alternative,
   dropping the node from `distrib/rp2040/mi.rp2040`, saves an inode and
   keeps the board blind. Prefer the Config change.
2. `README.md` claims a 160 KB user window "for a program that asks".
   `sys/arch/rp2040/include/machparam.h:126` fixes `USER_DATA_SIZE` at
   144 KB with no request path, and the kernel checks it against the
   linker at boot. Fix in `README.md`.
3. `logname` dispatches in `sbin/utilbox/utilbox.c` with no directory
   entry, the shape of yesterday's `whoami` defect. Fix in
   `distrib/rp2040/mi.rp2040` by linking `/usr/bin/logname` to
   `/usr/bin/utilbox`, or in `sbin/utilbox/Makefile` by dropping
   `logname:id` from `ALIASES`.
4. `/etc/passwd` gives operator the home directory `/operator`, which
   the root does not contain. Fix in `distrib/rp2040/mi.rp2040` with a
   `dir /operator`; `etc/passwd` is shared with the STM32 and PIC32
   ports and changing it there moves the problem.
5. `sys/arch/rp2040/doc/STORAGE.md` is stale in ten places, including
   the directory names of the boxes, the box count, the file and link
   counts, the `df` figures, and the name of the shipped editor. Fix in
   `sys/arch/rp2040/doc/STORAGE.md`.
6. `README.md` tells a user who sees nothing to press Enter because the
   board printed its prompt before they connected, while
   `sys/arch/rp2040/dev/usb.c:33` promises the ring shows a late opener
   the boot messages. Today's capture shows the truncated middle case.
   Fix in `README.md` by describing what the ring does and does not
   preserve.
7. Three root sizes describe one filesystem: 1078 kbytes in
   `distrib/rp2040/mi.rp2040`, 795 kbytes in `distrib/rp2040/md.rp2040`
   and `etc/rc.rp2040`, 979 blocks from `df`. Fix in
   `distrib/rp2040/mi.rp2040` and the two others, citing the `df` figure.
8. `sys/arch/rp2040/doc/USER-ACCESS.md` claims a `vt102` termcap entry
   the shipped `/etc/termcap` does not have. Fix in
   `sys/arch/rp2040/doc/USER-ACCESS.md`, or add the entry to
   `etc/termcap`.
9. `sys/arch/rp2040/doc/BOOT-MAP.md` names a branch twice in one clause
   and points at a `docs/rp2040/` directory that exists in the notes
   repository rather than the port tree. Fix in
   `sys/arch/rp2040/doc/BOOT-MAP.md`.
10. `distrib/rp2040/mi.rp2040` comments cite `bin/box/Makefile`, a
    `fasthalt` name that does not exist, and ordinals from a three-box
    root. Fix in `distrib/rp2040/mi.rp2040`.
11. `/etc/shells` lists `/bin/csh`, which the root does not ship. Fix in
    `etc/shells` for the port, or ship a per-port copy through
    `distrib/rp2040/mi.rp2040`.
12. `/dev/temp1` and `/dev/temp2` have no consumer on this root: only
    `fsck` issues `TFALLOC`, and only on `/dev/temp0`. They cost two
    inodes and are the allocator's declared capacity, so keeping them is
    defensible; the note belongs in `distrib/rp2040/mi.rp2040`.

## Checks not run

- Inode kind and link identity for the 111 program entries: the
  inventory captured names only. Run `ls -li` in the six directories and
  compare inode numbers and link counts against the manifest.
- `/usr/bin/su` mode 04751: unobservable in today's capture, and the one
  manifest mode with a security consequence. Run `ls -l /usr/bin/su`.
- Passwordless `operator` login and passwordless `su`: `/etc/shadow`
  content was not captured.
- The complete boot stream, which the truncation analysis needs as its
  reference.
