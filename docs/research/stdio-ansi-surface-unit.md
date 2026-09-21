# The C17 stdio interface surface as a unit, apart from the stream core

## What the unit is

One migration unit in the sense of `STYLE-GUIDE.md` section 4: the part of
the C17 stdio interface that lives outside the read and write path. It is
the third and last unit taken out of 2.11BSD patch 499, after the stream
core was built, measured and declined in `stdio-torek-evaluation.md` and the
two mechanisms worth keeping landed as `stdio-core-c17-unit.md` and the `r+`
mode discipline.

499's own stdio is 4.4BSD-Lite2's, and what it gets right outside the buffer
machinery is the interface: one reading of the mode string shared by the
three functions that take one, a flush that accepts a null stream, and the
position pair. Those are taken here. The stream core that carries them in
4.4BSD is not, for the reasons the evaluation measured.

| Piece | Where 499 has it | Here |
| --- | --- | --- |
| mode string to open flags | `__sflags` in `flags.c` | `_sflags` in `lib/libc/stdio/flags.c`, returning the stream flag and filling the open flags |
| `fflush(NULL)` | `_fwalk(__sflush)` | `_fwalk(fflush)`, with the walk carrying the status |
| `fgetpos`, `fsetpos` | `fgetpos.c`, `fsetpos.c` over `fpos_t` | `lib/libc/stdio/fgetpos.c`, `fpos_t` a long |
| `FOPEN_MAX`, `FILENAME_MAX`, `TMP_MAX` | `stdio.h` | `include/stdio.h`, each pinned to the value it names |
| `setvbuf` | already 4.4BSD's here, in K&R | prototype form, the size bound stated against `INT_MAX` |

## The defects it removes

**`fflush(NULL)` dereferences a null pointer.** C17 7.21.5.2p3 makes a null
stream the flush of every stream. `lib/libc/stdio/flsbuf.c` read `iop->_flag`
with no null test, so the call faulted. Rebuilding the gate against that
source segmentation faults (signal 11) rather than reporting a count.

**An append stream does not append.** C17 7.21.5.3p6 forces every write on
an append stream to the end of the file. `fopen` seeked to the end once at
open and passed `O_CREAT` alone, so a write after a seek landed at the seek
position. `_sflags` passes `O_APPEND`, which `sys/kern/sys_inode.c:42` turns
into `IO_APPEND` and `rwip` at line 178 turns into `uio->uio_offset =
ip->i_size` before each write to a regular file. The open-time seek stays,
so `ftell` answers the file's size before the first write.

**"rb+" opens read-only.** C17 7.21.5.3p3 puts the update `+` at either side
of the `b`. The three entry points each tested `mode[1] == '+'`, so "r+b"
was an update stream and "rb+" was not. `_sflags` scans the string.

**"wx" is not exclusive.** C11 added the `x` of "wx" and "w+x", which makes
the create fail rather than truncate a file that already exists. It was
unimplemented; `_sflags` turns it into `O_EXCL` and drops `O_TRUNC`.

**`fpos_t` is absent.** C17 7.21.1 requires the type and 7.21.9.1 and
7.21.9.3 the pair that uses it. `usr.bin/smlrc` defines its own
`struct fpos_t_` and declares `fgetpos` and `fsetpos` for itself, which is
why its x86 and MIPS back ends, `cgx86.c` and `cgmips.c`, cannot compile
against this tree's header; the rp2040 build takes `cgthumb.c`, which uses
neither.

## What it does not change

**`rewind` stays as it is.** C17 7.21.9.5 defines rewind as `fseek` to the
start with the error indicator cleared, and writing it that way removes a
second copy of the positioning rules and 26 bytes from `rew.o`. It was
written, measured and reverted: `rewind` without `fseek` is 48 bytes, and
calling `fseek` drags its 262 bytes into every program that rewinds a
password or group file and never seeks. `bin/ls` grew 344 bytes, against 104
for the rest of this unit. The gate's rewind checks pass against both
spellings, which is the other half of the reason: the change buys no
correctness, so it buys nothing.

**`setvbuf`'s size bound is a restatement, not a fix.** `(int) size < 0` and
`size > INT_MAX` decide the same way on every value, because `<stdio.h>`
makes `size_t` an unsigned int at every width this tree builds for. The
prototype form and the named bound are the change; the behavior is not.

**The pushback slot is not discarded by a positioning call.** C17
7.21.7.10p2 says a successful positioning call discards pushback, and
`fseek` does not clear `_IOUNGET`. `ungetc` sets that flag inside the
`_IOSTRG` branch alone, and a string stream is the one `sscanf` builds
around a caller's array, with no descriptor to seek. The invariant that
makes `fseek` correct is that only a string stream parks a byte, and it is
structural in `ungetc.c` rather than enforced at the seek. `setvbuf` does
clear it, because replacing the buffer costs nothing to extend by one bit
and the parked count lives in `_bufsiz`, which `setvbuf` reassigns.

## Identity

| Surface | Value |
| --- | --- |
| Baseline | `cc8d14c4`, main after #198 |
| Cross compiler | `arm-none-eabi-gcc` 16.2.0, `-Os`, the tree's flags |
| Host compiler | `gcc` 16.2.1, host width and `-m32` |
| Build | `bmake MACHINE=rp2040 clean` then `distribution`, in a worktree of its own beside a clean build of the baseline; exit 0, zero warnings both ways |
| Board | not run; every number here is a build or host-gate result |

## Cost

`arm-none-eabi-size` over the changed members:

| Object | Baseline | This unit |
| --- | ---: | ---: |
| `flags.o` | absent | text 148 |
| `fgetpos.o` | absent | text 30 |
| `fopen.o` | text 148 | text 76 |
| `freopen.o` | text 148 | text 76 |
| `fdopen.o` | text 104, data 4 | text 92, data 4 |
| `findiop.o` | text 348, data 196 | text 356, data 196 |
| `flsbuf.o` | text 488 | text 500 |
| `setvbuf.o` | text 184 | text 180 |
| stdio members | text 13183, data 200, bss 32 | text 13195, data 200, bss 32 |

The 277 programs the rp2040 manifest ships, 264 of which change:

| Delta | Programs | What they link |
| ---: | ---: | --- |
| +20 | 112 | `findiop.o` and `flsbuf.o`: the walk's status and the null-stream branch, which every program using stdio pays |
| +96 | 96 | the above plus `_sflags` net of what `fopen.o` gave back |
| +156 | 1 | `bin/rmail`, which links `fopen`, `freopen` and `fdopen` |
| +8 to +92 | 55 | programs linking some but not all of those members |

Sum over the shipped set: **+14796 bytes**. The packed root filesystem loses
three blocks, 102 free to 99, and `sbin/adminbox` stays inside
`MAX_PACKED_ROOT_BLOCKS` at 23.

`_sflags` compiles to 126 bytes of Thumb-1 and a 20-byte literal pool; the
`+76` a single-caller program pays is the mode-string vocabulary itself, and
is the same whether the logic is shared or inlined into each of the three
callers, which is why it is shared.

## Gate and calibration

`check-libc-ansi` runs `tests/libc_contracts/ansi_contract_test`, 92 checks,
at host width and at ILP32. The tree's sources compile against the tree's
headers, so the `FILE` layout and the `getc` and `putc` macros under test
are the target's. `open(2)` is renamed to a shim, because the tree's
`O_APPEND` is `0x0008` and its `O_TRUNC` is `0x0400`, neither of which is
the host's bit; the shim translates for the host kernel and records what
`_sflags` asked for. The ILP32 link adds the tree's own `findiop.c`, whose
`_Static_assert(sizeof(FILE) == 24)` holds only where pointers are four
bytes, so `fflush(NULL)` runs over the real stream table there and over the
test's own at host width.

Built against each defect in turn:

| Source under test | Gate |
| --- | --- |
| unperturbed | 92 checks, 0 failures |
| `fflush` without the null-stream case | segmentation fault, no count |
| append as one lseek, no `O_APPEND` | 92 checks, 7 failures |
| the update `+` read at index 1 alone | 92 checks, 12 failures |
| no `O_EXCL` for the `x` modes | 92 checks, 5 failures |
| `setvbuf` without the bound or the pushback discard | 92 checks, 1 failure |
| `rewind` spelled with `lseek` rather than `fseek` | 92 checks, 0 failures |
| restored | 92 checks, 0 failures |

The last row is the measurement that reverted the `rewind` change, and the
one before it is why the `setvbuf` bound is recorded above as a restatement:
the single failure is the pushback discard, not the bound.

## Not run

No board execution. `fgetpos` and `fsetpos` have no caller in this tree, so
they are exercised by the gate alone; they cost nothing in a program that
does not name them, because libc is an archive and the member is not
linked.
