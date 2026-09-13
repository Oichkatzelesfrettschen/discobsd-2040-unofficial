# C library size audit for DiscoBSD/RP2040

Worktree `~/worktrees/discobsd/libc-size-audit`, branch `libc-size-audit`,
off `main` at `c5400993`. Every number below is measured on the host with
`arm-none-eabi-size` for ELF objects and `tools/bin/size` for a.out ones;
none is estimated. A process on the RP2040 gets one 96 KB user window
holding text, data, bss and stack together, so bss counts against the
budget exactly as text does and the tables carry it.

## Three artifacts

| artifact | path | built by | size | members |
| --- | --- | --- | --- | --- |
| A, host-linked libc | `lib/libc.a` | `lib/libc/Makefile`, `arm-none-eabi-gcc -Os` | 380678 | 362 |
| B, board libc | `distrib/obj/destdir.rp2040/usr/lib/libc.a` | `distrib/rp2040/mkboardlibc.py` | 51528 | 138 |
| C, RP2040 Boot ROM | `lib/libc/arm/gen/rom_float_*.S` | reference only | -- | -- |

Artifact A totals 43991 bytes of text, 1716 of data and 6723 of bss across
its 362 members. Artifact B totals 13348 / 1104 / 132 across 137 members
plus `__.SYMDEF`.

`bmake MACHINE=rp2040 build` does not produce artifact B. `boardlibc-install`
in `distrib/rp2040/Makefile.inc` hangs off `distrib/rp2040/sdcard.img`, and
`lib/libc_aout` installs its own full archive at the same path during the
build, so a plain `build` leaves a 112544-byte `libc_aout` archive at
`${DESTDIR}/usr/lib/libc.a`. Artifact B therefore needs its own gate:

    ${PYTHON} distrib/rp2040/mkboardlibc.py --toolbindir=tools/bin \
        --gccprefix=/usr/bin/arm-none-eabi --include=include \
        --workdir=distrib/obj/boardlibc.rp2040 --out=OUT.a

`tools/bin/ar x` aborts partway through artifact B with `Invalid argument`,
extracting 103 of 138 members and stopping at `rom_float_cdcmple.o`. The
names are not the cause: `rom_float_resolver.o`, longer than several that
fail, extracts. `tools/bin/ar p` reads every one of the 34 stragglers, and
the tables below merge both reads. `ar` is not fixed here -- `main` already
carries `ar,ranlib,nm,ld: rewrite archives transactionally and share one
source`, so the defect belongs to that work.

## 1a. Artifact A, top 40 members by text

| member | text | data | bss | total |
| --- | --- | --- | --- | --- |
| `doprnt.o` | 4373 | 0 | 0 | 4373 |
| `ndbm.o` | 2262 | 0 | 0 | 2262 |
| `crypt.o` | 1704 | 0 | 1130 | 2834 |
| `ctime.o` | 1517 | 164 | 2112 | 3793 |
| `doprnt_float.o` | 1512 | 0 | 0 | 1512 |
| `doscan.o` | 1276 | 256 | 0 | 1532 |
| `setmode.o` | 1212 | 0 | 0 | 1212 |
| `regex.o` | 1131 | 0 | 585 | 1716 |
| `strftime.o` | 1004 | 0 | 18 | 1022 |
| `syslog.o` | 749 | 32 | 12 | 793 |
| `fstab.o` | 735 | 0 | 288 | 1023 |
| `random.o` | 632 | 156 | 0 | 788 |
| `getpwent.o` | 620 | 4 | 344 | 968 |
| `getttyent.o` | 548 | 10 | 288 | 846 |
| `nlist.o` | 536 | 0 | 0 | 536 |
| `strtod.o` | 524 | 0 | 0 | 524 |
| `qsort.o` | 500 | 0 | 16 | 516 |
| `getwd.o` | 490 | 0 | 4 | 494 |
| `atof.o` | 488 | 0 | 0 | 488 |
| `siglist.o` | 486 | 128 | 0 | 614 |
| `ecvt.o` | 476 | 0 | 80 | 556 |
| `flsbuf.o` | 444 | 0 | 0 | 444 |
| `popen.o` | 394 | 0 | 4 | 398 |
| `malloc.o` | 364 | 0 | 24 | 388 |
| `err.o` | 360 | 0 | 0 | 360 |
| `getusershell.o` | 347 | 12 | 12 | 371 |
| `strout.o` | 340 | 0 | 0 | 340 |
| `tmpnam.o` | 339 | 0 | 0 | 339 |
| `getopt.o` | 338 | 12 | 0 | 350 |
| `findiop.o` | 332 | 164 | 16 | 512 |
| `execvp.o` | 331 | 8 | 0 | 339 |
| `ldexp.o` | 312 | 0 | 0 | 312 |
| `setenv.o` | 304 | 0 | 4 | 308 |
| `gcvt.o` | 288 | 0 | 0 | 288 |
| `mktemp.o` | 282 | 0 | 0 | 282 |
| `uname.o` | 280 | 0 | 0 | 280 |
| `timezone.o` | 280 | 144 | 50 | 474 |
| `strtoul.o` | 268 | 0 | 0 | 268 |
| `getpass.o` | 268 | 0 | 9 | 277 |
| `filbuf.o` | 264 | 0 | 0 | 264 |
## 1b. Artifact B, top 40 members by text

| member | text | data | bss | total |
| --- | --- | --- | --- | --- |
| `doprnt.o` | 3900 | 24 | 0 | 3924 |
| `doscan.o` | 1248 | 256 | 0 | 1504 |
| `qsort.o` | 500 | 0 | 16 | 516 |
| `flsbuf.o` | 444 | 0 | 0 | 444 |
| `malloc.o` | 364 | 0 | 24 | 388 |
| `findiop.o` | 332 | 164 | 16 | 512 |
| `strtoul.o` | 268 | 0 | 0 | 268 |
| `fseek.o` | 264 | 0 | 0 | 264 |
| `filbuf.o` | 264 | 0 | 0 | 264 |
| `strtol.o` | 260 | 0 | 0 | 260 |
| `rom_float_resolver.o` | 212 | 0 | 0 | 212 |
| `sysctl.o` | 208 | 40 | 0 | 248 |
| `fputs.o` | 172 | 0 | 0 | 172 |
| `puts.o` | 148 | 0 | 0 | 148 |
| `fopen.o` | 148 | 0 | 0 | 148 |
| `strerror.o` | 144 | 16 | 64 | 224 |
| `scanf.o` | 124 | 0 | 0 | 124 |
| `rom_float_dcmp_core.o` | 112 | 0 | 0 | 112 |
| `fprintf.o` | 100 | 0 | 0 | 100 |
| `perror.o` | 96 | 8 | 0 | 104 |
| `strtok.o` | 92 | 0 | 4 | 96 |
| `getenv.o` | 88 | 0 | 0 | 88 |
| `exit.o` | 88 | 0 | 8 | 96 |
| `strcasecmp.o` | 84 | 256 | 0 | 340 |
| `ftell.o` | 84 | 0 | 0 | 84 |
| `fgets.o` | 84 | 0 | 0 | 84 |
| `aeabi_div.o` | 84 | 0 | 0 | 84 |
| `sleep.o` | 76 | 0 | 0 | 76 |
| `rom_float_fcmp_core.o` | 76 | 0 | 0 | 76 |
| `rom_float_f2ulz.o` | 76 | 4 | 0 | 80 |
| `rom_float_d2ulz.o` | 76 | 4 | 0 | 80 |
| `rom_float_d2iz.o` | 72 | 0 | 0 | 72 |
| `fputc.o` | 72 | 0 | 0 | 72 |
| `atol.o` | 72 | 0 | 0 | 72 |
| `atoi.o` | 72 | 0 | 0 | 72 |
| `sbrk.o` | 68 | 4 | 0 | 72 |
| `rom_float_i2f.o` | 68 | 0 | 0 | 68 |
| `ungetc.o` | 60 | 0 | 0 | 60 |
| `strstr.o` | 60 | 0 | 0 | 60 |
| `rom_float_ui2f.o` | 60 | 0 | 0 | 60 |
## 2. Reachability of the 20 largest members

No program is built with `-Wl,-Map`, and `elf2aout` deletes the ELF right
after converting it, so the shipped a.outs carry no name list --
`tools/bin/nm` on `/bin/box` answers `no name list`. Each of the 33 shipped
a.outs was therefore relinked with `LDWARN` carrying `-Wl,-Map`, which
`share/mk/sys.mk` line 115 expands inside `LDFLAGS`. Every relink reproduced
its baseline a.out byte for byte, so the maps describe the shipped link.

`/usr/bin/nohup` is a shell script and `/usr/bin/{true,false}` are 7-byte
scripts; `/usr/bin/cc` is `distrib/rp2040/cc`, a shell script. Those four of
the 37 `file` entries in `distrib/rp2040/mi.rp2040` have no a.out to link.

| member | text | data | bss | progs | reached by |
| --- | --- | --- | --- | --- | --- |
| `doprnt.o` | 4373 | 0 | 0 | 27 | adminbox as awk box coremark du fgrep find fsck gamebox getty grep init kilo ld login menu passwd ps sed smlrc sort stevie su sysbox textbox utilbox |
| `ndbm.o` | 2262 | 0 | 0 | 0 | none |
| `crypt.o` | 1704 | 0 | 1130 | 2 | login passwd |
| `ctime.o` | 1517 | 164 | 2112 | 9 | adminbox box find fsck getty init login su sysbox |
| `doprnt_float.o` | 1512 | 0 | 0 | 2 | awk, re (re only through the forced -u, removed in section 3; the branch head leaves awk alone) |
| `doscan.o` | 1276 | 256 | 0 | 4 | adminbox awk kilo utilbox |
| `setmode.o` | 1212 | 0 | 0 | 0 | none |
| `regex.o` | 1131 | 0 | 585 | 1 | utilbox |
| `strftime.o` | 1004 | 0 | 18 | 6 | adminbox getty init login su sysbox |
| `syslog.o` | 749 | 32 | 12 | 6 | adminbox getty init login su sysbox |
| `fstab.o` | 735 | 0 | 288 | 2 | fsck sysbox |
| `random.o` | 632 | 156 | 0 | 1 | passwd |
| `getpwent.o` | 620 | 4 | 344 | 9 | adminbox box find fsck login passwd ps su utilbox |
| `getttyent.o` | 548 | 10 | 288 | 2 | init login |
| `nlist.o` | 536 | 0 | 0 | 0 | none |
| `strtod.o` | 524 | 0 | 0 | 0 | none |
| `qsort.o` | 500 | 0 | 16 | 3 | box menu ps |
| `getwd.o` | 490 | 0 | 4 | 1 | box |
| `atof.o` | 488 | 0 | 0 | 1 | awk |
| `siglist.o` | 486 | 128 | 0 | 1 | sysbox |

Ranked by total footprint times programs reached -- the figure that matters
for a 96 KB window -- the order changes: `doprnt.o` 118071, `ctime.o` 34137,
`findiop.o` 14336, `malloc.o` 12804, `flsbuf.o` 12432, `getpwent.o` 8712,
`getgrent.o` 6590, `strftime.o` 6132.

### What the ROM or a smaller implementation could replace

The RP2040 Boot ROM exports `memcpy`, `memset`, `memcpy44`, `memset4`,
`popcount32`, `clz32`, `ctz32` and `reverse32` (datasheet 2.8.3.1). None of
the 20 largest members is a memory or bit routine, so for every row the
honest answer is that the ROM offers no replacement. The routines the ROM
does cover are already smaller than any call into it can be, measured
below.

## 3. Wins landed

### `_doprnt`: the 4.4BSD kernel conversions

`lib/libc/stdio/doprnt.c` carried `%b` (register bit decode), `%r`
(saturated counter) and `%z`/`%Z` (signed hexadecimal). All three are
kernel printf conversions; the kernel has its own copy in
`sys/kern/subr_prf.c`, which keeps them.

Deleting a `case` from `_doprnt`'s switch is safe only when no caller can
reach it, because the `default:` label prints the character literally and
consumes no `va_arg`: a stray `%b` would desynchronize every later
conversion in the same call, not merely misprint one. Three checks
established that no caller can:

- A literal scan of every shipped program's C, header, yacc and lex sources
  finds `%D` and none of `%b`, `%r`, `%z`, `%Z`. The scan covers the 65 tool
  source directories the six multicall a.outs compile from as well as the 33
  program directories: `sbin/{box,sysbox,adminbox,textbox,utilbox}` and
  `games/gamebox` build each tool from `bin/<name>` or `usr.bin/<name>`, so
  scanning only the box directories would have missed some 60 linked names.
  The `%D` users are `usr.bin/find`, `usr.bin/grep`, `bin/dd` (in sysbox) and
  `bin/expr` (in utilbox), and `%D` is kept. Tree-wide, `%b` appears only as
  a strftime month abbreviation and `%r`, `%z` only in `usr.bin/{lccom,lcpp,
  ccom}`, none of which ships.
- The three shipped programs that could route a runtime-built format into
  libc do not. `usr.bin/printf` accepts only `c s d i o u x X` and the float
  conversions and rejects everything else with `illegal format character`.
  awk's `format()` in `usr.bin/awk/run.c` gives an unrecognized item
  `flag = 0` and emits it through `sprintf(p, "%s", fmt)`, as text, with no
  argument consumed. `/bin/sh` has no printf builtin.
- `%n` in `usr.bin/as/as-thumb.c` is `strcmp("%nobits", name)` and friends,
  a section-attribute name, not a format.

Per-conversion cost, each measured by compiling `doprnt.c` alone with the
block removed:

| conversion | text after | saves | disposition |
| --- | --- | --- | --- |
| `%b` | 3969 | 404 | removed |
| `%r` | 4081 | 292 | removed |
| `%z`/`%Z` | 4293 | 80 | removed |
| `%n` | 4329 | 44 | kept |
| `%D` | 4369 | 4 | kept, used by find and grep |

All three removals together, plus the now-unused `q` pointer they left
behind, take `doprnt.o` from 4373 to 3517 bytes of text.

The header comment described `%D` as a hexdump taking a pointer while the
code makes it a long decimal, which is the conversion find and grep rely
on. It now states what the code does.

### `re`: the forced floating-point conversion

`usr.bin/re/Makefile` set `PRINTF_FLOAT=yes`, which makes `share/mk/sys.mk`
pass `-Wl,-u,__doprnt_cvt` and force `doprnt_float.o` into the link.
`__doprnt_cvt` has exactly one caller, `_doprnt`, and re's link map shows
neither `doprnt.o` nor any other printf member: re's only `printf` call
sites sit under `#ifdef TEST` in `r.gettc.c` and `r.edit.c`, no object in
the program defines `printf`, and re uses no `double` or `float` at all.
The conversion and the double arithmetic it dragged in were unreachable.

awk keeps `PRINTF_FLOAT=yes`: it links `doprnt.o` and formats floats
through `sprintf` with `%f`, `%e` and `%g` in `format()`.

### Bytes off each shipped program

Both changes together, measured on the a.out each program installs into the
root. The 9 shipped a.outs absent from the table are unchanged: /bin/sh,
/bin/ed, /usr/bin/tail, /usr/bin/tee and /usr/sbin/update link no _doprnt,
and /usr/bin/{nohup,true,false} and /usr/bin/cc are shell scripts.

| program | before | after | saved |
| --- | --- | --- | --- |
| `/bin/ps` | 14870 | 14034 | 836 |
| `/bin/box` | 35856 | 35000 | 856 |
| `/bin/sysbox` | 34620 | 33784 | 836 |
| `/sbin/fsck` | 30000 | 29144 | 856 |
| `/sbin/init` | 16488 | 15652 | 836 |
| `/sbin/adminbox` | 25316 | 24460 | 856 |
| `/usr/bin/awk` | 52724 | 51888 | 836 |
| `/usr/bin/coremark` | 16816 | 15980 | 836 |
| `/usr/bin/sed` | 15896 | 15040 | 856 |
| `/usr/bin/grep` | 10472 | 9636 | 836 |
| `/usr/bin/fgrep` | 9596 | 8760 | 836 |
| `/usr/bin/find` | 18900 | 18064 | 836 |
| `/usr/bin/sort` | 13452 | 12616 | 836 |
| `/usr/bin/login` | 22513 | 21677 | 836 |
| `/usr/bin/passwd` | 14660 | 13824 | 836 |
| `/usr/bin/su` | 15588 | 14752 | 836 |
| `/usr/bin/re` | 27532 | 25118 | 2414 |
| `/usr/bin/kilo` | 16716 | 15880 | 836 |
| `/usr/bin/stevie` | 19648 | 18792 | 856 |
| `/usr/bin/menu` | 11416 | 10580 | 836 |
| `/usr/bin/textbox` | 37444 | 36608 | 836 |
| `/usr/bin/utilbox` | 51177 | 50321 | 856 |
| `/usr/bin/du` | 8644 | 7808 | 836 |
| `/usr/bin/as` | 31721 | 30865 | 856 |
| `/usr/bin/ld` | 22161 | 21305 | 856 |
| `/usr/libexec/getty` | 18664 | 17828 | 836 |
| `/usr/libexec/smlrc` | 49724 | 48868 | 856 |
| `/usr/games/gamebox` | 12564 | 11728 | 836 |

Total 25166 bytes off a 1078 KB root. Artifact B falls from 51528 to 50008,
its `doprnt.o` from 3900 to 3088 bytes of text.

## 4. Candidates rejected, with the number

| candidate | measurement | reason |
| --- | --- | --- |
| `memcpy`, `memset`, `bzero`, `bcopy` to ROM | `memcpy.o` 18, `memset.o` 16, `bzero.o` 18, `bcopy.o` 38 | A `ROM_FLOAT_SINGLE_TARGET` cell is a 12-byte thunk, a 14-byte descriptor and a 4-byte data cell, about 30 bytes -- more than `memcpy` and `memset` cost outright, before `rom_float_resolver.o`'s 212 bytes. Routing costs size even where the resolver is already linked. |
| `memmove` to ROM `memcpy` | `memmove.o` 48 | The ROM `memcpy` promises nothing about overlapping regions, which is the whole contract of `memmove`. |
| `ffs`, popcount, clz to ROM bit ops | `ffs.o` 22, reached by zero shipped programs | Smaller than one ROM cell, and nothing links it. |
| `doprnt` `%n` | 44 bytes | Non-conforming (it prints a number rather than storing a count), but removing it changes behavior for a caller that names it, and 44 bytes across 27 programs does not buy that risk. |
| `doprnt` `%D` | 4 bytes | `usr.bin/find` and `usr.bin/grep` both print block counts with it. |
| `doprnt_float` in awk | 1512 bytes | awk formats floats through `sprintf("%f")` and links `doprnt.o`; the conversion is reached. |
| smaller `malloc` | `malloc.o` 364 text, 24 bss, reached by 33 of 33 shipped a.outs | `lib/libc/gen/malloc.c` is already the V7 circular first-fit allocator: one word of header per allocation, one busy bit in the low pointer bit, no size classes and no free lists. A first-fit rewrite cannot beat 4 bytes of per-allocation overhead, and 364 bytes leaves nothing to cut. |
| ctype tables | `ctype_.o` 257 bytes of data, reached by 20 programs | The 257-byte table is `_ctype_` itself, one byte per character plus the EOF slot. It is the data, not overhead. |
| `strerror`, `sys_errlist` | `strerror.o` 160 text, 64 bss, reached by 21 programs | This tree carries no `sys_errlist`; `strerror` reads the message from the kernel through `sysctl(CTL_MACHDEP, CPU_ERRMSG)` into a 64-byte static buffer and formats `Unknown error: N` by hand when that fails. The table already lives in the kernel. |
| `siglist` | `siglist.o` 486 text, 128 data, reached by `sysbox` alone | One program, and it prints the names. |
| timezone and ctime data | `ctime.o` 1517 / 164 / 2112 in 9 programs, `timezone.o` 280 / 144 / 50 | See below: a real cost, but no safe removal. |
| `ndbm`, `setmode`, `nlist`, `strtod`, `ecvt` | 2262, 1212, 536, 524, 476 bytes of text | Reached by zero shipped programs and absent from `distrib/rp2040/boardlibc-members`, so they cost nothing in either artifact that reaches the board. |
| `index` versus `strchr`, `rindex` versus `strrchr` | 16, 16, 22, 20 bytes | Eight programs link both `index.o` and `strchr.o`. Aliasing one to the other saves 16 bytes in each, 128 bytes across the root -- below the noise of a behavior-visible change to the string API. |
| `bzero` calling `memset` | 18 bytes standalone against about 12 as a tail call | Saves 6 bytes in the 11 programs that link both. |

### The `ctime` finding, which stands without a code change

`ctime.o` is the second-largest lever in the tree by window footprint:
34137 bytes of total footprint times programs, behind only `_doprnt`.
`static struct state s` in `lib/libc/gen/ctime.c` is 2036 bytes of bss --
`ats[370]` at 4 bytes each, `types[370]`, `ttis[10]` and `chars[51]`, sized
by `TZ_MAX_TIMES`, `TZ_MAX_TYPES` and `TZ_MAX_CHARS` in `include/tzfile.h` --
and it is linked into 9 shipped programs.

Sharper than the bss: `tzload` calls `alloca(sizeof s)`, a 2036-byte stack
spike inside libc, and `alloca(MAXPATHLEN)` on top of it, in a process whose
text, data, bss and stack share one 96 KB window.

`distrib/rp2040/mi.rp2040` ships neither `/etc/localtime` nor
`/usr/share/zoneinfo`, so `tzload` always fails at `open` on a fresh board
and `tzset` falls back to GMT. That does not license removing it: the flash
is writable and the board carries a native compiler, so a user can create
`/etc/localtime`, and a removal justified by a runtime-mutable condition is
a permanent feature loss. Shrinking the `TZ_MAX_*` limits is worse -- it
leaves `tzload` succeeding against a truncated transition list, so the board
reports wrong times instead of honest GMT -- and `include/tzfile.h` is
shared by every machine the tree builds. `ctime` is absent from
`boardlibc-members`, so none of this reaches artifact B.

## Gate

`bmake MACHINE=rp2040 build` exits 0. The top level defines no `cleandir`,
so the gate runs from a worktree checked out fresh at the branch head, which
reproduces the baseline's 1811 compiler invocations exactly rather than an
incremental subset. The baseline captured before the first change emitted
214 warnings; the tree after both changes emits 214, and no warning text
appears that the baseline did not already contain. `mkboardlibc.py` runs clean and produces
artifact B at 50008 bytes, down from 51528.
