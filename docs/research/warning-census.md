# What a wider warning set costs this tree

`tools/warning-census.sh` measures it. The numbers below are that script's
output at the tree this document ships with, built with arm-none-eabi-gcc
16.2.0, and they replace a figure of 188 this document carried before,
which was wrong for a reason recorded below beside the three earlier ways
the measurement lied.

## The measurement

Userland, `-Wall -Wextra`, the root Makefile's nine SUBDIR entries in the
root's order, 1392 cross compiles, every one carrying every flag:
**7951 warning instances at 4429 distinct sites**. An instance is one
emission; a site is one file, line and category, so a header diagnostic
that every including translation unit repeats is one site. Both counts
are of the cross compiler's output alone; the ten host compiles a few
Makefiles run for their generators raised 227 instances that the record
keeps apart.

| directory | cross compiles | instances | sites |
| --- | --- | --- | --- |
| share | 0 | 0 | 0 |
| lib | 691 | 96 | 88 |
| bin | 57 | 780 | 723 |
| usr.bin | 341 | 5180 | 2245 |
| sbin | 30 | 197 | 185 |
| libexec | 5 | 62 | 62 |
| usr.sbin | 19 | 634 | 142 |
| games | 243 | 1002 | 984 |
| benchmarks | 6 | 0 | 0 |

The install step recompiles 248 lib/libc_aout members under the tree's
own flags; the census logs that step apart and measures the build step
alone.

| category | instances | sites |
| --- | --- | --- |
| `-Wcomment` | 2868 | 74 |
| `-Wmissing-field-initializers` | 933 | 889 |
| `-Wimplicit-function-declaration` | 743 | 683 |
| `-Wreturn-type` | 611 | 610 |
| `-Wbuiltin-declaration-mismatch` | 478 | 57 |
| `-Wunused-parameter` | 392 | 344 |
| `-Wempty-body` | 324 | 324 |
| `-Wparentheses` | 221 | 216 |
| `-Wmissing-parameter-type` | 182 | 156 |
| `-Wimplicit-fallthrough=` | 165 | 156 |
| `-Wunused-variable` | 131 | 125 |
| `-Wimplicit-int` | 121 | 73 |
| `-Wsign-compare` | 119 | 115 |
| `-Wformat=` | 108 | 84 |
| `-Wchar-subscripts` | 106 | 81 |
| `-Wreturn-mismatch` | 104 | 104 |
| twenty-five categories under 60 each | 344 | 328 |

The two columns answer different questions. `-Wcomment` is the noisiest
category by a factor of three and the cheapest to repair: 2805 of its
instances are 44 comment lines of `usr.bin/uucp/uucp.h`, each repeated
by up to 63 units that include it. `-Wbuiltin-declaration-mismatch` is
the same shape, 57 sites, 48 of them in `include/string.h`,
`include/stdio.h` and `include/unistd.h`, which 452 units repeat. `-Wmissing-field-initializers`, `-Wreturn-type` and
`-Wempty-body` are the opposite: nearly every instance is its own site,
so the instance count is the repair count.

The kernel is measured separately, because its Makefile carries its own
`CWARNFLAGS`:

```sh
bmake MACHINE=rp2040 kernel CWARNFLAGS='-Wall -Wextra -Wno-error'
```

Commit 98ef831e closed the kernel's 231 and moved both kernel
configurations to `-Wall -Wextra -Werror`; `sys/sys/cdefs.h` now carries
`__unused` for the port, and `docs/research/warning-policy-audit.md`
records that repair. The kernel half of this census is history.

## Where the userland warnings are

Sites by program, from the record's `sites.txt`:

| program | sites |
| --- | --- |
| usr.bin/uucp | 1084 |
| games/battlestar | 745 |
| bin/sh | 617 |
| usr.bin/awk | 211 |
| usr.bin/zmodem | 197 |
| usr.bin/yacc | 180 |
| sbin/utilbox | 83 |
| lib/libtcl | 71 |
| libexec/getty | 62 |
| games/atc | 59 |

Three programs hold 55 percent of the sites, and 145 of the 184 program
directories that raise anything raise under ten each. `-Wimplicit-function-declaration` and
`-Wmissing-parameter-type` say why the count is what it is: usr.bin/awk,
usr.bin/uucp and most of games compile with `-ansi` or `-std=gnu89`,
where an undeclared function is legal and only `-Wall` names it, so the
tree's own `-Werror` never sees them.

That shape decides the approach. A tree-wide `-Wextra -Werror` is a
question about vendored programs first and about this port's own code
second, and the two deserve different answers: the port's own files are
worth fixing, while uucp, battlestar and the Bourne shell are worth
either a scoped warning level or an upstream-shaped patch, not a local
rewrite that makes the next import harder. `-Wcomment` in uucp.h and the
`-Wbuiltin-declaration-mismatch` sites in three tree headers are the two
header repairs that remove 3257 instances at 92 sites.

## Reconciling this with the audit's 8,559

`docs/research/warning-policy-audit.md` reports that "the full
expanded-warning build emitted 8,559 compiler-warning instances,
including repeated headers and both kernel configurations." That figure
names no flag set and the evidence bundle behind it carries no log that
produces it, so it cannot be reproduced from what is recorded; the census
is a script anyone can rerun. Its order of magnitude is now confirmed:
7951 userland instances plus the kernel's 231 at the time is 8182, and
the audit counted both kernel configurations. The reconciliation lives
here rather than in the audit, because the audit is a ledger: it names
the tree at 4197a91f, and `docs/INDEX.md` files it where a correction
goes in a new document instead of in the ledger.

## What the census records

A run leaves a directory, named on the command line with `-o` or printed
at the end: `commands.txt` with the exact make invocation per directory,
`record.tsv` with each directory's exit status, compile count, the
compiles that carried every flag and the ones that did not, host
compiles, warning instances, install-step compiles and distinct sites,
`<dir>.log` and `<dir>.install.log`, `uncovered.txt` listing any cross
compile that lacked a flag, `cross-warnings.txt` with every cross warning
prefixed by the directory its compile ran in, `sites.txt`, and
`summary.txt`. The exit status is 0 only when every directory built and
every cross compile carried every flag; otherwise the numbers are a
floor and the record says which directory or compile made them one.

## Five ways the measurement lied

Recorded because each produced a confident number that was wrong. The
first three predate the script; the fourth and fifth are counts this
document itself carried.

**A parallel build cannot attribute a warning to a source file.** Reading
back from a warning to the nearest preceding compile line gives whichever
of twelve jobs printed last. That method credited 174 warnings to
`usr.sbin/cron/entry.c`, which raises none when compiled by itself. The
census compiles serially for this reason alone.

**Raising the warning set without demoting errors measures the first few
directories.** The roughly ten directories that already set `-Werror`
stop on the first new warning. The census appends `-Wno-error`, and puts
it where `-Werror` itself lives: on the compiler command through
`WARNERR`, which `share/mk/sys.mk` composes into `CC` so that a Makefile
that assigns `CFLAGS` outright, as usr.bin/smlrc and games/btlgammon do,
still receives it. The first script passed the flags through `COPTS`,
which such a Makefile discards, and counted those programs as clean. The
census now reads every cross compile line back and names any that lacks
a flag; `check-warning-policy-cross` compiles the same override on the
policy gate's routes, smlrc among them, and requires the `-Wextra` probe
to warn and still produce an object.

**Counting a parallel build's warning lines inflates the total.** The
same header diagnostic is emitted once per translation unit, and a
parallel run that recompiles more than it needs to counts each emission.
The census reports instances and sites side by side so the header
multiplier is visible rather than hidden or inflated.

**Counting the working tree counts the build.** An earlier count of
`(void)parameter;` statements under `sys/` included a disassembly the
build writes. A question about the source is asked of `git ls-files`.

**Building the directories in the wrong order measures the ones that
survive.** The first script cleaned every directory and then built them
in the order it was given, bin before lib, so bin linked against a
libc.a the clean had removed; ten directories stopped, 718 of 1392
compiles ran, and the census reported 188 warnings with a note that the
count was a floor. It was a floor by a factor of forty. The script now
takes the directories from the root Makefile's SUBDIR in the root's
order and cleans, builds and installs each before the next, as the root's
build target does.

The record also caught a second ordering defect the build itself
carried. Twice in three runs lib's make never entered lib/startup-arm,
so lib/crt0.o stayed missing and every program after lib failed to link.
bmake judges an existing target by mtime unless it is phony, and a
subdirectory is a directory that exists: `$(SUBDIR): FRC` compares each
directory against FRC, which bmake stamps with the second the make began
once the first subdirectory has made it, and a directory the preceding
clean touched in that same second is not older, so it is up to date and
skipped. The recursive Makefiles now declare their subdirectory targets
`.PHONY`, and `check-build-failure` dates the scratch subdirectories an
hour ahead and requires every one to be entered, a case the old
lib/Makefile fails by visiting libc_aout alone.
