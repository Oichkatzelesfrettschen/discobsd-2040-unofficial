# What a wider warning set costs this tree

`tools/warning-census.sh` measures it. The numbers below are that script's
output, and they replace three earlier estimates of mine that were wrong in
ways worth recording, because each failure mode is easy to repeat.

## The measurement

Userland, `-Wall -Wextra`, 718 compiles: **188 warnings**, and 10 build
errors that stop their directories, so 188 is a floor. The same run against
98ef831e reports the same 718, 188 and 10, with the same category counts.

| category | count |
| --- | --- |
| `-Wunused-parameter` | 75 |
| `-Wmissing-field-initializers` | 52 |
| `-Wimplicit-fallthrough=` | 22 |
| `-Wsign-compare` | 14 |
| `-Wformat=` | 5 |
| `-Wunused-variable` | 4 |
| `-Wbuiltin-declaration-mismatch` | 4 |
| `-Wparentheses`, `-Wold-style-declaration` | 3 each |
| `-Wcomment` | 2 |
| `-Wtype-limits`, `-Wmissing-braces`, `-Wchar-subscripts`, `-Warray-bounds=` | 1 each |

The kernel was measured separately, because its Makefile carries its own
`CWARNFLAGS`:

```sh
bmake MACHINE=rp2040 kernel CWARNFLAGS='-Wall -Wextra -Wno-error'
```

That reported 231 warnings: 108 `-Wunused-parameter`, 100 `-Wsign-compare`,
11 `-Wimplicit-fallthrough=`, 8 `-Wclobbered`, 2 `-Wtype-limits`, 2
`-Wempty-body`. Commit 98ef831e closed all 231 and moved both kernel
configurations to `-Wall -Wextra -Werror`, so the kernel half of this census
is now history rather than a backlog. `docs/research/warning-policy-audit.md`
records that repair.

## The header levers

98ef831e silenced the kernel's unused parameters one at a time, adding 60
`(void)parameter;` statements. The tree carried 99 of them across 24 tracked
files under `sys/`, because it had no `sys/cdefs.h`: `__unused` was
`#define`d separately in `sys/arch/pic32/pic32/conf.c`,
`sys/arch/stm32/stm32/conf.c`, `sys/arch/rp2040/rp2040/conf.c` and
`sys/arch/rp2040/dev/flash.c`, each marked `XXX`, and three of those four
comments already named the duplication they were working around.

`sys/sys/cdefs.h` now carries `__unused`, reached through `sys/types.h`,
and the four duplicate definitions are gone. Of the 99 statements, 62 in
16 files become an attribute on the parameter that is actually unused. The
remaining 37 stay, for reasons rather than by omission: 34 are in vendored
source -- TinyUSB, the ST HAL, heatshrink and Dhara -- where a local
rewrite makes the next import harder, and 3 are in
`sys/arch/rp2040/test/swapram`, which compiles against its own
`shim/sys/param.h` and so never sees the tree's headers.

The attribute also says something the statement could not. `sorw` and
`soctl` in `sys/kern/sys_generic.c` use their parameters under `#ifdef
INET`, so `(void)fp;` claimed unconditionally that a parameter was unused
while `__unused` says it may be. Nothing changes in the compiled output:
with the debug sections stripped, every one of the 79 PICO and 74
PICO_UART objects is byte-identical to a build of 98ef831e.

The second lever is narrower and already spent: all 12 of the kernel's
`-Wsign-compare` warnings reported against `param.h:100` came from the one
`MIN` macro there, which a single definition would have fixed.

Userland has no such lever. Macros contribute almost nothing there --
`ATOI2` three times, `SELECT` and `CREATE` twice each.

## Where the userland warnings are

The 25 noisiest files carry 180 of the 188. They are concentrated in
vendored programs rather than spread through the tree: `usr.bin/ccom` and
`usr.bin/lccom` (the C compilers, `init.c` alone raising 53) and
`usr.bin/tclsh` (the Tcl interpreter, about 61 across its `tcl*.c` files).

That shape decides the approach. A tree-wide `-Wextra -Werror` is a
question about vendored third-party source first and about this port's own
code second, and the two deserve different answers: the port's own files
are worth fixing, while a vendored interpreter is worth either a scoped
warning level or an upstream-shaped patch, not a local rewrite that makes
the next import harder.

## Reconciling this with the audit's 8,559

`docs/research/warning-policy-audit.md` reports that "the full
expanded-warning build emitted 8,559 compiler-warning instances, including
repeated headers and both kernel configurations." That figure and this
document's 188 measure different things and neither corrects the other.

The audit's number names no flag set, and the evidence bundle behind it
carries no log that produces it, so it cannot be reproduced from what is
recorded; the census is a script anyone can rerun. The audit also counts
instances, and the three failure modes below are exactly how an instance
count inflates: one header diagnostic is emitted once per translation unit,
and a parallel build recompiles more than it needs to. Treat 8,559 as an
order-of-magnitude statement that the broader warning groups are a large
migration, and 188 as the measured cost of `-Wall -Wextra` over userland.

The reconciliation lives here rather than in the audit, because the audit
is a ledger: it names the tree at 4197a91f, and `docs/INDEX.md` files it
where a correction goes in a new document instead of in the ledger.

## Four ways the measurement lied

Recorded because each produced a confident number that was wrong. The first
three predate the script; the fourth is a count this document itself carried.

**A parallel build cannot attribute a warning to a source file.** Reading
back from a warning to the nearest preceding compile line gives whichever
of twelve jobs printed last. That method credited 174 warnings to
`usr.sbin/cron/entry.c`, which raises none when compiled by itself. The
census compiles serially for this reason alone.

**Raising the warning set without demoting errors measures the first few
directories.** The roughly ten directories that already set `-Werror` stop
on the first new warning, and the run ends having compiled 37 files rather
than 718 and reported 1 warning rather than 188. The census appends
`-Wno-error`, which lands after a directory's own `-Werror`.

**Counting a parallel build's warning lines inflates the total.** The same
header diagnostic is emitted once per translation unit, and a parallel run
that recompiles more than it needs to counts each emission. That put
userland at 674 rather than 188, and
`-Wbuiltin-declaration-mismatch` at 178 rather than 4.

**Counting the working tree counts the build.** This document first reported
138 `(void)parameter;` statements across 25 files, from a `grep -r` over
`sys/`. A built tree holds more than its sources: the match list included
`sys/arch/rp2040/compile/PICO/unix.dis`, a disassembly the build writes.
Restricting the same search to `git ls-files` gives 99 across 24 files. A
question about the source is asked of the index, not of the directory.

A warning's own `file:line` is the compiler's output and survives the first
three failures. Only inference from a log's ordering does not, and only a
tracked-file list answers the fourth.
