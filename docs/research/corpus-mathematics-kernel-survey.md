Host measurements of the linked PICO and PICO_UART kernels and host gates; one Raspberry Pi Pico run read four errno messages and 57 trace lines through the two decoders; no Renode run.

# Mathematics from the owner's document corpora, tested against the kernel

Two document collections were searched for mathematics that a 128 KB kernel
on a Cortex-M0+ could use: a clone of the `openai/math` manuscript
collection at `7ef5c0cc5` (721 manuscripts in 373 result families), and the
owner's research document repositories (6,162 manifest rows: papers, books,
technical references, and the Cayley-Dickson manuscripts and Rust crates of
the same workspace). Documents from those repositories are cited by author,
title and the first sixteen hex digits of the manifest SHA-256.

Each candidate had to name a kernel site and move a scarce quantity there:
flash bytes, SRAM bytes, flash wear, or time with interrupts masked. With
25 process slots, 24 inodes and 4 buffers, asymptotic speed is not scarce,
so a structure that only shortens a search over those tables was declined.

Result: three changes land and remove 1,692 bytes of PICO kernel text
(95,400 to 93,708; PICO_UART 86,411 to 84,695). Two come from the corpora,
the superstring packing of the system call trace names and the grammar
coding of the errno messages. The third, `adjtime()` in 32 bits, comes from
the census of division helpers that the Beatty-sequence candidate required.
One candidate was measured and declined, one refuted, and the rest have no
site.

## Evidence base

- This repository at `694b91ef` plus the three commits of this change.
- `arm-none-eabi-gcc` 16.2.0, `-mcpu=cortex-m0plus -mthumb -Os -std=gnu17`,
  for every size; host `cc` GCC 16.2.1 for the gates.
- Sizes are `arm-none-eabi-size` text of `unix.elf`; symbol sizes are
  `arm-none-eabi-nm -S`.
- The `openai/math` manuscripts are model-written and their README states
  that results without a Lean formalization may have issues. Nothing here
  depends on a theorem from them: the superstring family supplied the
  problem statement, and the generator's output is checked by reading every
  name back.

## Verdicts

| Pattern | Source | Kernel site | Measurement | Verdict |
| --- | --- | --- | --- | --- |
| Shortest common superstring | `openai/math` family 128 (a factor-two approximation); Skiena, *The Algorithm Design Manual*, section 18.9 (`fe1a100b3f30b910`) | `syscall_name_pool`, read by the `kern.systrace` console line | 1,192 bytes to 818 | lands |
| Grammar coding by digram rules | Larsson and Moffat, "Off-line dictionary-based compression" (Re-Pair, 1999; outside the corpora); two-part description length in Li and Vitanyi (`0614d01ce3d11dbd`) | `errno_message_pool`, read by `machdep.errmsg` for strerror(3) | 1,901 bytes to 1,113 with rules, plus 92 bytes of decoder | lands |
| Operand range splitting | the division census below | `adjtime()` | three 64-bit libgcc helpers leave the image; 624 bytes | lands |
| Golden-ratio multiplicative hashing | three-distance theorem (Koksma, *The theory of asymptotic distribution modulo one*, `90d0a152c960f188`); Seltzer and Yigit, "A New Hashing Package for UNIX" (`b8bb4c42eb019021`) | `HASH()` of `kern_synch.c` | better spread, about 16 bytes dearer, no scarce quantity moved | declined |
| Shortest common superstring | as above | `errno_message_pool` | 42 bytes of overlap, below the cost of a length field | refuted |
| Beatty sequences and Bresenham accumulation | Beatty and Wythoff sequence notes (`b8abbe9c3a532caa`) | tick and microsecond conversion | every division sits on a system call or post-erase path | no site |
| Thorp shuffle as a small-domain permutation | `openai/math` family 238 | none: the kernel randomizes nothing | not measured | no site |
| Walsh-Hadamard butterflies; XOR-indexed basis products of the Cayley-Dickson tower | the workspace's `fwht` and `cd_kernel` crates | none: the CRC is a 16-entry nibble table already | not measured | no site |
| Ultrametric (leading-zero of XOR) queues and timer wheels | Murtagh, "On ultrametricity, data coding, and computation" (`ba2571eb4d272c0b`) | callout queue, at most `NCALL` entries on a delta list | not measured | no site |

## Trace names as one overlapping string

A terminated string can share bytes with another only as its suffix,
because the terminator occurs once and last. Suffix sharing takes the pool
from 1,192 to 1,084 bytes. Without terminators a name can lie inside
another (`read` in `readv`) or across the seam of two (`vfork` after
`preadv`), which is the shortest common superstring problem. The 163 names
hold 1,029 characters; the maximum-overlap greedy merge packs them into
818.

A span needs an offset and a length. The pool is below 1,024 bytes and the
longest name is 17, so both fit the 16-bit entry the table already had: ten
bits of offset, six of length. `syscall_name()` returns pointer and length
and the trace line prints with `%.*s`. `gen_kernel_metadata.py` refuses a
name longer than 63 bytes, a pool beyond 1,024 and a name that is not
printable ASCII or holds `"`, `\` or `?`. The dispatch contract test reads
all 163 names back through the generated C; narrowing the length field or
the offset mask in that C fails it.

PICO text: 95,400 to 95,048.

## Errno messages as a grammar

The 83 messages share words (`not`, `supported`, `Operation`) and no
suffixes, so the superstring gains 42 bytes. Re-Pair replaces the most
frequent adjacent pair of symbols by a new symbol and repeats. Bytes below
128 are characters, bytes from 128 name a rule, and a rule is two symbols
that name a character or an earlier rule, so expansion terminates. A rule
costs two bytes, so the generator stops when no pair occurs three times or
128 rules exist.

| Rule depth allowed | Pool plus rules, bytes | Deepest first symbol |
| --- | --- | --- |
| 2 | 1,120 | 2 |
| 3 | 1,113 | 3 |
| 4 and above | 1,112 | 4 |

(The table counts message bytes and rules; the shipped pool adds its
closing terminator.) The decoder recurses on a rule's first symbol and
loops on the second, so the depth of first symbols bounds its stack; the
generator caps it at four, which costs nothing on this text.
`kernel_errmsg()` expands into a `KERNEL_ERRMSG_MAX` (48) byte buffer that
the sysctl handler holds on its stack, and a static assertion ties that
constant to the longest message. `errno_message_expand` is 36 bytes and
`kernel_errmsg` 56.

`tests/kernel_metadata/errmsg_contracts.py` compiles the generated file for
the host under the address and undefined-behavior sanitizers, reads every
message back between guard bytes and rejects six mutated decoders.

PICO text: 95,048 to 94,332. The archived PIC32 port under `legacy/` keeps
its call to the earlier interface; that tree is outside the maintained
build.

## The division census and `adjtime()`

A Beatty sequence, floor(n * alpha), is what an integer accumulator
computes when it converts one rate to another without dividing, and the
kernel converts between ticks, microseconds and seconds. The census of
32-bit division helpers in the PICO image found 38 call sites in 19
functions. The
ones that run without a system call are `clock_catchup` and
`monoclock_advance`, after a flash erase closes its masked window, and
`schedcpu` and `vmmeter`, once a second or less. None runs per tick, so an
accumulator has nothing to save.

The census did show that `adjtime()` alone called `__divdi3`,
`__aeabi_ldivmod` and `__aeabi_lmul`. It formed the delta in ticks as a
64-bit product and divided it by `hz`. Splitting the microseconds at one
second first gives a tick remainder below `hz` and a whole number of
seconds; the slew test then needs `seconds * hz` only when the seconds are
within 0x8001 of zero, where it fits 32 bits for any `hz` up to 0x7fff, and
the step needs one conditional carry. Where `hz * usechz` is one second,
which holds on both ARM ports, the results are those of the 64-bit form.

`time_io_test.c` compares 1,596 deltas (21 second values, 19 microsecond
values and 4 tick phases, reaching both ends of each field) against 64-bit
reference arithmetic, and `check_correctness.py` rejects four `adjtime`
mutants.

PICO text: 94,332 to 93,708.

## The sleep-queue hash, declined

`HASH(x)` is `(x >> 5) & 15`. A wait channel is a kernel address: an
element of `proc[]` (104 bytes each), `inode[]` (84) or `buf[]` (36), or a
field a few bytes into one. The three-distance theorem says the multiples
of an irrational step leave at most three gap lengths on the circle, and
the golden ratio makes them most even, which is why multiplying by
2^32 / phi and keeping the top bits spreads any arithmetic progression.

Measured over every table element of the linked image and five scalar
channels (106 channels, 16 queues):

| Hash | `proc[]` queues used | `inode[]` queues used | Longest queue | `ip`, `ip+2`, `ip+4` share a queue |
| --- | --- | --- | --- | --- |
| shift and mask | 10 | 10 | 16 | 45 of 48 |
| `x * 0x9e3779b9 >> 28` | 15 | 16 | 9 | 0 of 48 |

The multiplier is better on every column and is declined. A queue holds
sleeping processes, at most 25 and a handful in practice, so the shorter
scan saves no measurable time; the change costs about 16 bytes of flash and
moves none of the four scarce quantities. It becomes worth landing if
`NPROC` grows past the point where `wakeup()` shows in a profile.

## Falsifiers

- A kernel whose text does not fall by the stated amounts with the same
  compiler refutes the size claims.
- A trace line or a strerror(3) message that differs from its master on a
  board refutes the two decoders. One Pico run of the PICO kernel printed
  the master text for ENOENT, EEXIST, ENOTEMPTY and ENOTDIR and 57 trace
  lines naming 14 system calls, each in `syscalls.master`; the other 79
  messages and 149 names rest on the host gates.
- An `hz` that does not divide one million moves `adjtime()` results by the
  rounding of `usechz`; the equality claim is limited to the ARM ports.
