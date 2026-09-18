# What a wider warning set costs this tree

`tools/warning-census.sh` measures it. The numbers below are that script's
output, and they replace three earlier estimates of mine that were wrong in
ways worth recording, because each failure mode is easy to repeat.

## The measurement

Userland, `-Wall -Wextra`, 718 compiles: **188 warnings**, and 10 build
errors that stop their directories, so 188 is a floor.

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

The kernel is measured separately, because its Makefile carries its own
`CWARNFLAGS`:

```sh
bmake MACHINE=rp2040 kernel CWARNFLAGS='-Wall -Wextra -Wno-error'
```

That reports 231 warnings: 108 `-Wunused-parameter`, 100 `-Wsign-compare`,
11 `-Wimplicit-fallthrough=`, 8 `-Wclobbered`, 2 `-Wtype-limits`, 2
`-Wempty-body`.

## Where they are

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

Macros contribute almost nothing in userland -- `ATOI2` three times,
`SELECT` and `CREATE` twice each -- so there is no header lever there. The
kernel has exactly one: all 12 `-Wsign-compare` warnings reported against
`param.h:100` come from the `MIN` macro, and one definition fixes all of
them. `__unused` is a second: it is `#define`d separately in
`sys/arch/pic32/pic32/conf.c`, `sys/arch/rp2040/rp2040/conf.c` and
`sys/arch/rp2040/dev/flash.c`, each marked `XXX`, because the tree has no
`sys/cdefs.h` to hold it.

## Three ways the measurement lied before the script existed

Recorded because each produced a confident number that was wrong.

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

A warning's own `file:line` is the compiler's output and survives all
three failures. Only inference from a log's ordering does not.
