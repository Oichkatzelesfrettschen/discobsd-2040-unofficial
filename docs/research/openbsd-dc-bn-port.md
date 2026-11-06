# OpenBSD dc(1) and bc(1) on DiscoBSD: the BN subset and `lib/libbn`

OpenBSD dc(1) and bc(1) need 31 `BN_*` identifiers from libcrypto plus three
`ERR_*` calls in one error-reporting helper, which the port replaces.
`lib/libbn` supplies the `BN_*` set in 3,293 bytes of Thumb-1 text
(`bmake MACHINE=rp2040 -C lib/libbn`, `bn.o`: 3,276 `.text` plus 17
`.rodata`, no data, no bss), written from the manual-page contract and textbook
algorithms with no OpenSSL or LibreSSL source consulted. The host test checks
every exported function against Python integers (63,550 vectors, 1,124,964
checks, zero failures) natively, at `-m32`, under AddressSanitizer plus
UndefinedBehaviorSanitizer, and as a Thumb-1 program under qemu-arm user-mode
emulation; 14 of 14 seeded faults are rejected. The owner-approved
differential run (last section) builds the pinned OpenBSD dc twice, against
the host libcrypto and against `lib/libbn/bn.c`, and feeds both 3,000 random
programs: 2,995 produce identical output and exit status, and the other five
all raise a number to an exponent of four million or more. That run compared
an LP64 dc using 64-bit libcrypto words with an LP64 dc using libbn's 32-bit
`BN_ULONG`; the ILP32 repeat and the run of the ported tree sources are
recorded in the last section. `usr.bin/dc` and `usr.bin/bc` now hold the
OpenBSD sources, ported as the section "The port in the tree" describes: a
lazily allocated register table (dc's extended register array is 771 KB, the
process window is 144 KB), a small compatibility layer, and for bc a name
table in place of its 65,536-entry hash table and a plain line read in place
of libedit.

## Sources and hashes

Fetched on 2026-11-05 from `github.com/openbsd/src` by raw URL at one commit:
`d5983e0c8e9e888c4dc0bb497f4501850db2ec9b`, `master` at fetch time, committed
2026-11-05T17:54:16Z. The GitHub mirror carries no release tags
(`git ls-remote https://github.com/openbsd/src` lists `HEAD`,
`refs/heads/master` and pull-request refs only), so `OPENBSD_7_6` is
unavailable and the commit is the pin. Each file's own `$OpenBSD$` revision
identifies its last change. The files sit in a scratch directory as untrusted
data; no OpenBSD Makefile ran. The two `USD.doc` directories were not
fetched.

| File | `$OpenBSD$` revision | License header | SHA-256 |
| --- | --- | --- | --- |
| `usr.bin/dc/Makefile` | Makefile v1.3 2015/10/10 19:28:54 deraadt | none | `3d6da712e49d70e18b5615089d41935d14266ed4d9847b1147632f08ee7b5ac1` |
| `usr.bin/dc/bcode.c` | bcode.c v1.65 2025/12/15 12:10:46 tb | ISC, Otto Moerbeek 2003 | `fd6c43370a4f50af1cb21bd891d51d443a2523554849df5e36547c8d7809cb64` |
| `usr.bin/dc/bcode.h` | bcode.h v1.8 2015/02/16 20:53:34 jca | ISC, Otto Moerbeek 2003 | `516c6b96a4112eb64de8d08d1ffe35a9e172dcbbee1b02b1914fbd4246e443c9` |
| `usr.bin/dc/dc.1` | dc.1 v1.35 2021/03/08 02:47:27 jsg | Caldera (four-clause) | `d6903008fc5f5655a162fe25c2e3fb7eb650d52ea0d5199fde02c12ec152f9d0` |
| `usr.bin/dc/dc.c` | dc.c v1.21 2024/04/23 13:34:50 jsg | ISC, Otto Moerbeek 2003 | `973fdf34c3f705efc8caa8d369570efa1c0d05fa2eae8450a5972b2483582e25` |
| `usr.bin/dc/extern.h` | extern.h v1.5 2015/10/10 19:28:54 deraadt | ISC, Otto Moerbeek 2003 | `bc0756bf21a7bd1d0eed229b0de800e94c2640477fbd0c9c76d7f4ae828bfb33` |
| `usr.bin/dc/inout.c` | inout.c v1.24 2024/11/07 16:20:00 otto | ISC, Otto Moerbeek 2003 | `2120b5b366861d23358c928a7a99a60f27c4e5d1964da1e721da5d719d974042` |
| `usr.bin/dc/main.c` | main.c v1.1 2015/10/10 19:28:54 deraadt | ISC, Otto Moerbeek 2003 | `b629a101b2679a39ecbef22eb1b640e6b7d39084603f217dc1a849a3b6b69835` |
| `usr.bin/dc/mem.c` | mem.c v1.10 2017/12/17 08:21:10 otto | ISC, Otto Moerbeek 2003 | `fccced875cbc5f642ed186a2be03495be4259716d0330df99e64674b4aa11abf` |
| `usr.bin/dc/stack.c` | stack.c v1.15 2017/12/06 13:48:05 otto | ISC, Otto Moerbeek 2003 | `140f2f51288119f2370ac01ca3ead5ee8df0f5b1d13330686565f4229b4622f6` |
| `usr.bin/bc/Makefile` | Makefile v1.10 2017/07/19 12:50:32 espie | none | `5cf3b7762b0cea2d5dc959be3abea84c7c48cc2520c3717de06f8ca19e805773` |
| `usr.bin/bc/bc.1` | bc.1 v1.36 2024/07/31 05:36:13 jmc | Caldera (four-clause) | `97964114d5cec1de141037ee19f04c6294c913830b62914fd882dd19ad085560` |
| `usr.bin/bc/bc.library` | bc.library v1.4 2012/03/14 07:35:53 otto | Caldera (four-clause) | `7c96cd315b7c5633ca10da401736060cbcc7b9c895ba885ad3e5c26c894690b2` |
| `usr.bin/bc/bc.y` | bc.y v1.53 2023/03/08 04:43:10 guenther | ISC, Otto Moerbeek 2003 | `6b83d0d3ee1f3c383dd3ee693754fa3004c04c5fbec35d0892dc7f1dbc9f8da9` |
| `usr.bin/bc/extern.h` | extern.h v1.12 2014/04/17 19:07:14 otto | ISC, Otto Moerbeek 2003 | `8373f46a5518e7660110b4735ae1bb538d3e77cc8f3776e582c5aab10fad4036` |
| `usr.bin/bc/pathnames.h` | pathnames.h v1.1 2003/09/25 19:32:44 otto | ISC, Otto Moerbeek 2003 | `35bbd2686e470111322b1541c04ffc5b585632b71b7e44425d40e14cfb4d7fb1` |
| `usr.bin/bc/scan.l` | scan.l v1.32 2023/03/08 04:43:10 guenther | ISC, Otto Moerbeek 2003 | `801ddeb48adde8ce0c6343c1ff90ffedc84dea51ee0d08ccd1b4091af78dd145` |
| `usr.bin/bc/tty.c` | tty.c v1.5 2023/03/08 04:43:10 guenther | ISC, Otto Moerbeek 2013 | `f9cbc698cab541d7738e665b7d34688d46bcd9c1e63dd65293cd290082c8ed81` |

The license column was read from each file's header. Every `.c`, `.h`, `.y`
and `.l` file carries the same ISC permission notice with "Copyright (c) 2003,
Otto Moerbeek" (`bc/tty.c`: 2013). The two manual pages and `bc.library`
carry the Caldera International ancient-UNIX license, which includes the
advertising clause; `NOTICE` section 3 already reproduces that license for the
tree's other ancient-UNIX code. The shipped `usr.bin/dc` and `usr.bin/bc`
are different programs (each directory holds a Plan 9 Foundation MIT
`LICENSE`), so replacing them changes the NOTICE entries.

## License boundary

libcrypto's `bn` directory is not ISC. The header of
`lib/libcrypto/bn/bn_add.c` at the pinned commit (58 lines, read up to the
first preprocessor line and no further, SHA-256
`2343bdb3ed094333fa49886d7244bebfcd495aa28a4bc2eff0fa4bc5acafaa79`) is the
SSLeay license: Eric Young's 1995-1998 copyright, and clause 3, "All
advertising materials mentioning features or use of this software must display
the following acknowledgement". That clause is incompatible with the tree's
BSD 3-clause and ISC files and would force an acknowledgement into every
advertisement for a DiscoBSD image, so no libcrypto bn text enters the tree.

The contract came from the OpenBSD manual pages `BN_add(3)`, `BN_add_word(3)`,
`BN_bn2bin(3)`, `BN_cmp(3)`, `BN_copy(3)`, `BN_CTX_new(3)`, `BN_new(3)`,
`BN_num_bytes(3)`, `BN_set_bit(3)`, `BN_set_negative(3)` and `BN_zero(3)` from
`man.openbsd.org`, and from reading the ISC-licensed dc and bc sources for the
semantics they depend on. The algorithms are schoolbook addition, subtraction
and multiplication, Knuth volume 2 section 4.3.1 Algorithm D, short division,
left-to-right square-and-multiply and decimal conversion by repeated division.
The new files carry no license header and inherit the repository `LICENSE`
(BSD 3-Clause), by the owner's ruling. Residual risk: independent implementations
of the same textbook algorithms resemble each other, and the tests were not
compared to libcrypto source; the license position rests on that process
record, not on a legal opinion. `docs/research/legal-memo-redistribution.md`
is the tree's legal reference.

## The BN subset

Counts are call sites in `usr.bin/dc/*.c` (bc calls no `BN_*` itself: it
links the dc sources and drives them through `dc_main`). Counts exclude
comments and the `DEBUGGING` helpers `pn` and `pbn`. The compiled dc objects
reference 29 distinct `BN_*` symbols (`BN_mod` expands to `BN_div` and
`BN_num_bytes` to `BN_num_bits`), and `nm -u` over the six objects against
`nm` of `libbn.a` shows every one defined and `BN_CTX_free` the only exported
function dc never calls.

| Identifier | Sites | What dc relies on | libbn behavior |
| --- | ---: | --- | --- |
| `BIGNUM`, `BN_CTX`, `BN_ULONG` | types | pointer types; `BN_ULONG` stored in `u_long`, compared with `(BN_ULONG)-1` | opaque structs; `BN_ULONG` is `uint32_t` |
| `BN_new` | 14 | zero value; `NULL` on failure checked by `bn_checkp` | zero, `NULL` on allocation failure |
| `BN_free` | 14 | releases a value | accepts `NULL` |
| `BN_dup`, `BN_copy` | 2, 1 | `dup_number`, `bsqrt`, `split_number`; return checked | `NULL` on failure; `BN_copy(a, a)` is a no-op |
| `BN_set_word` | 23 | every register, scale and count store, including 0 | 0 to `2^32-1`, clears sign |
| `BN_get_word` | 5 | `(BN_ULONG)-1` means "does not fit", tested by callers | magnitude; `-1` above 32 bits |
| `BN_one` | 4 | constants 1 | `BN_set_word(a, 1)` |
| `BN_is_zero`, `BN_is_one`, `BN_is_negative` | 14, 1, 7 | sign and zero tests drive divide-by-zero, `sqrt` and `printnumber` | zero is never negative |
| `BN_set_negative` | 1 | `negate()` flips the sign with `!BN_is_negative` | sets negative only when the value and flag are nonzero |
| `BN_cmp`, `BN_ucmp` | 2, 1 | signed ordering for comparisons; magnitude ordering in `count_digits` | -1, 0, 1 |
| `BN_num_bits`, `BN_num_bytes` | 3, 1 | `count_digits` estimate, `bsqrt` seed, `print_ascii` byte count | 0 for zero; bytes are `(bits+7)/8` |
| `BN_is_bit_set` | 3 | parity test in `bexp`, bit walk in `print_ascii` | bit of the magnitude; false out of range or negative index |
| `BN_mask_bits` | 1 | `to_ascii` keeps the low 8 bits after checking `BN_num_bits > 8` | keeps low `n` bits of the magnitude, fails when the value has fewer than `n` bits |
| `BN_rshift`, `BN_rshift1` | 1, 4 | `bsqrt` seed, `bexp` loop on a non-negative exponent | shifts the magnitude, keeps the sign, result may alias the operand |
| `BN_add`, `BN_sub` | 2, 4 | scaled add/sub, `bsqrt` Newton step, fraction digit removal | signed; result may alias either operand |
| `BN_mul` | 2 | `bmul_number` calls it with result equal to both operands (`a*a`) | signed; any aliasing |
| `BN_div`, `BN_mod` | 6, 1 | quotient `NULL`-able, remainder `NULL`-able; truncation toward zero; remainder takes the dividend's sign; divisor and quotient may be the same object (`BN_div(r, NULL, one, r)`) | exactly that; fails on a zero divisor |
| `BN_exp` | 3 | `10^k` with `a` and `r` the same object | non-negative exponent, `0^0 = 1`, negative exponent fails |
| `BN_add_word`, `BN_mul_word` | 1, 3 | digit accumulation in `readnumber`, powers of ten | signed in place; any 32-bit word |
| `BN_div_word` | 4 | quotient in place, remainder returned; digit extraction for output bases; negative input in `scale_number` | truncates toward zero, returns the non-negative remainder of the magnitude, `(BN_ULONG)-1` for a zero divisor |
| `BN_bn2dec`, `BN_bn2hex` | 1, 1 | `free()`able string; `-` prefix for negatives; hex with leading zeros stripped by the caller | `malloc`ed; decimal has no leading zeros; hex is uppercase with two digits per byte; `"0"` for zero |
| `BN_CTX_new` | 1 | one context in `init_bmachine`, never freed | holds one reusable scratch area; `BN_CTX_free` added for the tests |

Four behaviors rest on a reading of the manual pages and of dc's call
sites, not on libcrypto itself: `BN_get_word` reports the magnitude of a
negative value (`BN_get_word(3)` says only "the value"; `set_obase`,
`set_ibase`, `to_ascii` and `not` could reach it with a negative number);
`BN_rshift` acts on the magnitude and keeps the sign; `BN_mask_bits` acts on
the magnitude and keeps the sign; `BN_div_word` returns the remainder of the
magnitude. A libcrypto-linked dc run on negative inputs to those commands
falsifies each; the differential run in the last section did so and found all
four to hold over its corpus.

## Other dependencies and their mapping onto this tree

| Dependency (where) | State in this tree | Action |
| --- | --- | --- |
| `<openssl/bn.h>` (`bcode.h`) | absent | include `<bn.h>` with `-I${TOPSRC}/lib/libbn` |
| `<openssl/err.h>`: `ERR_load_BN_strings`, `ERR_reason_error_string`, `ERR_get_error` (`mem.c` `bn_check`, `bn_checkp`) | absent; libbn has no error queue | print `errx(1, "BN failure")` |
| `FILE` and `free` reached through OpenSSL's header (`bcode.h`, `extern.h`, `inout.c`) | libbn's header is minimal | add `<stdio.h>` and `<stdlib.h>` includes |
| `__dead` (`dc.c`) | undefined | `_Noreturn` or a compat define |
| `uint` (`bcode.c` `count_digits`) | undefined | `unsigned int` |
| `pledge`, `setproctitle` (`main.c`, `dc.c`, `bc.y`) | no such calls | delete |
| `asprintf` (`dc.c` `-e`, four sites in `bc.y`) | absent from `lib/libc` | add to libc or build the string with `snprintf` and `reallocarray` |
| `errc` (`dc.c`) | `include/err.h` has `err`, `errx`, `warn`, `warnx` and the `v` forms only | `errno = EISDIR; err(1, ...)` |
| `optreset` (`dc.c`) | defined in `lib/libc/stdlib/getopt.c`, undeclared in headers | declare `extern int optreset` |
| `extern char *__progname` (`dc.c`) | `include/unistd.h` declares `const char *` | include `<unistd.h>`, drop the extern |
| `reallocarray`, `strlcpy`, `err`/`warn` family, `getopt`, `signal`, `setvbuf`, `fstat`, `snprintf` with `%0*lu` | present (`lib/libc/stdio/doprnt.c` handles `*`) | none |
| 64-bit multiply in `count_digits` (`c * bits`) and Thumb-1 switch tables | `__aeabi_lmul`, `__gnu_thumb1_case_uqi` come from libgcc, which the cross link supplies (`NOTICE` section 14) and the board does not carry | none for a cross-linked program; replace the multiply if the program must also build with the on-board toolchain |

The probe behind this table compiled the six dc sources with the tree's
`arm-none-eabi-gcc` flags and a throwaway compatibility header; the
diagnostics above are the complete set it raised. `bcode.c` defines a static
`dup(void)`; with glibc's `<unistd.h>` included ahead of it the compile
failed on a conflicting declaration, and this tree's `include/unistd.h`
declares `dup(int)` as well, so a compatibility header must not include
`<unistd.h>` ahead of `bcode.c`.

## Design

**Limb width.** Limbs are 16 bits. The Cortex-M0+ has `MULS`, which returns the
low 32 bits of a product, and no divide instruction. A 16-bit limb makes each
inner-loop product a single `MULS` and each division step one 32/16 divide
(`__aeabi_uidivmod` from `lib/libc/arm/gen/aeabi_div.S`) with no 64-bit
intermediate. `arm-none-eabi-nm -u bn.o` lists `__aeabi_uidiv`,
`__aeabi_uidivmod`, `malloc`, `realloc`, `free`, `memcpy`, `memmove` and
`memset` and nothing else; a 32-bit limb written with `uint64_t` intermediates references
`__aeabi_lmul` and `__aeabi_uldivmod`, which the symbol gate in `tests/libbn/check_object.sh`
rejects (calibrated on `bad_lmul.c.in`, one 64-bit multiply and divide that
references both). The cost is
multiplication throughput: the disassembled `mmul` inner loop is 13 Thumb-1
instructions per 16x16 product, about 18 cycles by the M0+ timing rules
(2-cycle loads and stores, 2-cycle taken branch), so a 2,000-bit square costs
about 281,000 cycles, 2.2 ms at 125 MHz. A 32-bit limb built from four `MULS`
would need roughly half the cycles per bit of product by the same
arithmetic, but it needs a 64/32 divide for output conversion and `sqrt`.
Both figures are estimates from instruction counts, not measurements on
silicon; `not run` for any board timing.

**Memory.** A `BIGNUM` is 16 bytes (pointer, `top`, `dmax`, sign) plus
`2 * dmax` bytes of limbs; storage grows by `top + 4` limbs so dc's digit
loop reallocates once per about 20 digits. `BN_mul` forms a product in place
when the result shares no storage with an operand and in scratch otherwise;
`BN_div` always works in scratch (`2*na + nd + 2` limbs for an `na`-limb
dividend and `nd`-limb divisor) before writing any destination, which makes every aliasing legal. A `BN_CTX` keeps one scratch
buffer, grown to the largest request and reused, so dc's single context
avoids a `malloc` per operation; a `NULL` context allocates per call. No
function recurses: `-fstack-usage` reports 104 bytes for the largest frame
(`divide`, with Algorithm D inlined) and the longest call chain is `BN_exp`,
`BN_mul`, `mulraw`, `mmul`. A 100-digit number occupies about 80 bytes before
allocator overhead.

**Error model.** Functions return 1 or 0, pointers return `NULL`, and
`BN_div_word` and `BN_get_word` return `(BN_ULONG)-1`, as the manual pages
specify. A failed call leaves its destinations numerically unchanged: scratch
is computed first and destinations are expanded before any is written. A
size above `INT_MAX/16 - 2` limbs fails, so a bit count always fits an `int`.
There is no error queue.

**Representation invariants.** `d[top-1] != 0`, `top <= dmax` and zero is
never negative. The test reads limbs directly and checks all three after
every operation.

## The port in the tree

`usr.bin/dc` and `usr.bin/bc` hold the pinned sources with the edits below; every
file keeps its upstream header and `$OpenBSD$` line. The Plan 9 programs stay
in git history. `lib/Makefile` builds `lib/libbn` ahead of both, and each
program's Makefile links `-lbn`. `usr.bin/bc/Makefile` compiles the dc sources
into the bc directory and runs `bison -y` (the invocation `usr.bin/awk` uses)
and `flex` on `bc.y` and `scan.l`; the host carries bison 3.8.2 and flex 2.6.4.
`bison` and byacc 2.0 20260126 (a generator OpenBSD builds bc with) both report
one shift/reduce and 16 reduce/reduce conflicts in `bc.y`; a bc built from the
tree sources through byacc passes the same 560 cases as the bison build.

| File | Edit |
| --- | --- |
| `dc/compat.h` (new) | `__dead` as `_Noreturn`, the `optreset` declaration, the `basprintf` declaration; included first by `dc/extern.h` and `bc/extern.h` |
| `dc/bcode.h` | `<bn.h>` for `<openssl/bn.h>`, plus `<stdio.h>` for `FILE` |
| `dc/mem.c` | `bn_check` and `bn_checkp` call `errx(1, "BN failure")`; `basprintf` replaces `asprintf` with two `vsnprintf` calls |
| `dc/dc.c` | `basprintf`; `pledge` and the `__progname` declaration removed; `errc` becomes `errno = EISDIR; err(...)` |
| `dc/main.c` | `setproctitle` and `pledge` removed |
| `dc/bcode.c` | `uint` is `u_int`; the register table is paged (below); the sign, unused-parameter and `calloc` argument order diagnostics of `-Wall -Wextra` repaired with casts and a `(void)` use; the jump-table range test, always false for a `u_char`, removed |
| `dc/inout.c`, `dc/stack.c` | one signedness cast each and an unused-parameter use |
| `bc/bc.y` | `stdint.h` added for the name table's hash; `histedit.h`, `search.h`, `pledge`, `WCONTINUED`, the libedit setup, the dummy prompt and `__progname` removed; `asprintf` is `basprintf`; `hcreate`/`hsearch` are the name table below |
| `bc/scan.l` | the `EditLine` input path, `bc_eof` and the `SIGTSTP` handler removed; `abort_line` keeps the `[\n]P` it writes to dc; `%option noinput` |
| `bc/extern.h` | the `EditLine`, `termios` and `tty.c` declarations removed |
| `bc/pathnames.h` | `_PATH_LIBB` can be overridden at compile time, which lets the host gate point `bc -l` at the tree's library |
| `bc/tty.c` | not imported: it exists for libedit's terminal save and restore |

**Register table.** `regstack(idx)` in `bcode.c` is the one accessor: it indexes a
directory of `REG_ARRAY_SIZE / 256` page pointers (1 for plain dc, 257 for
`-x`), allocates a 256-stack page (3,072 bytes at ILP32) with `bmalloc` the first
time a register in it is named, and initializes each stack to empty. The seven
sites that read `bmachine.reg[idx]` call it. A program that names one register
under `-x` holds 4,413 requested bytes at `-m32`; the eager table was 789,504
bytes for the stacks alone, 5.4 times the 144 KB process window, so `dc -x`
and bc's child dc could not have started before this change (inferred from
the sizes; no board run). A page that fails
to allocate ends the program through `bmalloc`'s `err(1, NULL)`.

**Name table.** `names_find` and `names_enter` in `bc.y` keep the (name plus
type letter) to code map in an open-addressed table of power-of-two size: 64
slots at start, doubling when half full, with a ceiling of 131,072 slots, the
smallest power of two above twice the 63,504-variable limit `lookup` already
enforces. A bc program with 40 names holds 64 slots (512 bytes at ILP32).

**Interactive input.** `bc` reads the terminal through the kernel's line
discipline with `getc`; the libedit history and editing are gone, and `^C`
still writes `[\n]P` to the child dc. `lib/libreadline` was not used: it
adds a library to a program the PicoCalc image already holds at 31 KB.

**Fork.** bc forks before it parses and the child runs `dc_main` on the pipe,
as upstream does. `fork1()` swaps the parent image out to make the child
(`sys/kern/kern_fork.c`), so a fork needs room in swap for the image the process
holds at that point. The bc a.out is 39,344 bytes and the heap at fork time
holds the 512-byte name table and the argument vector, so the copy is about
40 KB; the 384 KB raw swap holds two windows of 144 KB with 96 KB to spare
(arithmetic, not a measurement). The fork is kept. No board run has forked the
image: `board_dcbc_smoke.py` is the check, and `bc -c | dc -x` is the
fallback that needs no fork.

Sizes, from the build in this tree (`arm-none-eabi-size` on the linked ELF,
a.out bytes, and packed bytes from `tools/bin/rp2040/fsutil --verbose
--partition=1` on `distrib/rp2040/sdcard.img`):

| Program | text | data | bss | a.out | packed in the image | Plan 9 program (packed) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `dc` | 21,779 | 244 | 1,212 | 22,060 | 18,644 | 16,189 |
| `bc` | 38,783 | 524 | 1,960 | 39,344 | 31,176 | 17,042 |
| `bc.library` | | | | 5,125 | 5,125 | 2,742 (`lib.b`) |

Libc members and libgcc's `__aeabi_lmul`, `__aeabi_uidiv`, `__aeabi_idiv` and the
`__gnu_thumb1_case_*` helpers are linked into both. The pair costs 59 blocks
where the Plan 9 pair cost 38: the `picocalc` profile leaves 189 free blocks
(210 before), and `full`, which excludes the closure, still leaves 280. bc holds
a second copy of the dc evaluator; a multicall binary that dispatched on
`argv[0]` would drop about 18 KB of packed text, at the price of one binary
and no separate `dc` image.

## Evidence

Environment: Linux 6.18.54.1-microsoft-standard-WSL2 x86_64; gcc 16.2.1, clang
23.1.4, arm-none-eabi-gcc 16.2.0 with newlib 4.6.0, qemu-arm 11.1.3, Python
3.14.9, bmake. Source revision: `main` at `076675a8` plus the uncommitted
files named here.

    : "${PYTHON:?}"; export PYTHON
    bmake -C tests/libbn check check-cross check-qemu

- Vectors. `tests/libbn/gen_vectors.py` writes 63,550 vectors with
  Python integers, from a `Random` seeded `0x44495343` and only `getrandbits`:
  exhaustive small operands (add, sub and mul for -40..40, division for
  -30..30 with zero divisors), 900 random signed pairs of 0 to 2,000 bits,
  carry and borrow chains up to 129 limbs, 1,500 random and 2,500 limb-pattern
  divisions, 300 dividends constructed to take the Algorithm D add-back branch
  (found by simulating the algorithm in the generator), multiples of the
  divisor plus -1, 0 and +1, powers up to `10^257`, comparisons, bit
  counts, masks, shifts, word operations at 24 edge words, and decimal and
  hex strings through `10^600`. The generator asserts the C truncation
  identity on every division vector.
- Native, `-m32` (ILP32) and sanitized (`-fsanitize=address,undefined
  -fno-sanitize-recover=all`): each reports `vectors=63550 checks=1124964
  failures=0 injected_failures=42` and `PASS libbn`. The compile line is
  `-std=c17 -pedantic-errors -Wall -Wextra -Werror -Wold-style-definition
  -Wshadow -Wconversion`. clang 23.1.4 also builds and passes the native and
  sanitized runs.
- Aliasing and dirty destinations. Each add, sub, mul, div, mod and exp
  vector runs with a pre-filled result and with every legal result-operand
  aliasing, each aliasing both with and without a `BN_CTX`; division also
  writes the quotient and the remainder over the two operands at once, in
  both assignments. Each rshift vector runs in place and into a separate
  result.
- Allocation failure. The test replaces `malloc`, `realloc` and `free` with
  counting wrappers and fails the k-th allocation of 18 operations, plus
  `BN_bn2dec`, `BN_bn2hex` and `BN_dup`, for k = 1, 2, ... until the operation succeeds: each failing run returned the
  failure value, left every destination unchanged and released everything
  (42 injected failures). The live-allocation count is zero at exit.
- Mutants. `tests/libbn/bn_mutate.py` writes 14 single-site faults, each
  checked to occur once in `bn.c`: carry out of `madd` dropped, borrow
  in `msub` dropped, carry-in of the `mmul` inner loop dropped, off-by-one in
  the dividend's funnel shift during normalization, normalization overflow
  limb zeroed, Algorithm D add-back removed, estimate test loosened by one,
  negative zero allowed, remainder sign taken from the divisor, exponent loop
  one bit short, in-place multiply aliasing allowed, decimal chunk 9999
  instead of 10000, mask width off by one, right-shift cross-limb bits
  dropped. `check-mutants` requires each to exit 1 with a `FAIL` line; all 14
  do. The unmutated test passing serves as the known-good control. One
  further fault, no normalization at all (`s = 0`), passed all vectors and
  400,000 random Python trials of the algorithm: the two-limb estimate test
  and the add-back absorb a missing normalization, which then costs only
  estimate-loop iterations, so the output is unchanged on every input tried.
  That mutant is not in the set because the test cannot distinguish it.
- Thumb-1 execution. `check-qemu` links the same test with newlib for
  `-mcpu=cortex-m0plus -mthumb` and runs it under qemu-arm user-mode
  emulation (`tests/libbn/qemu_support.c` supplies the Linux system calls
  newlib needs): the same counts, in 8.5 seconds. `objdump` of that binary
  contains no `umull`, `smull`, `udiv`, `sdiv`, `mla` or `mls`; its
  attributes read `Tag_CPU_arch: v6S-M`, `Tag_THUMB_ISA_use: Thumb-1`. This
  executes the instruction sequence under emulation; it does not measure
  silicon, and the RP2040 boot ROM divider path is not involved.
- Gates run. `check-comment-hygiene` passes; `shellcheck -S error` and
  `ruff check` pass on the new scripts. `check-c17-source-denominator` passes
  only while the new files are untracked; once tracked, `bn.c`, `bn.h`,
  `bn_test.c`, `qemu_support.c` and the three `dc-differential` headers need
  rows in `tools/c17/source-denominator.json` (finding INT-1 of
  `docs/research/calculator-and-games-review.md`).
- Execution receipts. `tests/libbn/Makefile` runs the native, ILP32,
  sanitized and mutant variants through `tools/test-execution.mk`
  (`libbn.native`, `libbn.ilp32`, `libbn.sanitized`, `libbn.negative`), which
  `tools/test-execution-inventory.json` lists, so `REQUIRE_ILP32=yes` turns a
  skipped ILP32 or sanitizer run into a failure. The mutant runner bounds each
  mutant at 120 seconds (`timeout 120`) and reports a hang as a rejection
  failure.

Cross size, strict C17 object (`-Os -ffreestanding -mcpu=cortex-m0plus
-mthumb -mfloat-abi=soft -nostdinc`, `check-cross`): text 3,303, data 0, bss 0.
Tree build line (`bmake MACHINE=rp2040 -C lib/libbn`, gnu17 base flags plus
`-std=c17 -pedantic-errors`): text 3,293, data 0, bss 0; `lib/libbn.a` 6,586
bytes. The six dc objects built with `-std=gnu17 -Os -fno-common
-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft` and a throwaway compatibility
header measure text 10,219
(`bcode.o` 6,674, `inout.o` 1,688, `stack.o` 1,154, `dc.o` 464, `mem.o` 231,
`main.o` 8), data 32, bss 1,088. Together with libbn that is 13,512 bytes of
text, against 12,176 text and 4,092 bss for the shipped Plan 9 `usr.bin/dc/dc.o`
compiled the same way. Libc members linked in addition are not counted;
`size` on the linked `dc.elf` is the measurement that settles the total.

Calibration of the symbol gate: `bad_lmul.o` (a 64-bit multiply and divide)
is rejected, naming `__aeabi_lmul` and `__aeabi_uldivmod`.

## Not run

- The differential corpus on the board image over the console, and any other
  run on the board. `tests/rp2040/dcbc_smoke/board_dcbc_smoke.py` is written
  and closed unless `DISCOBSD_BOARD_DCBC` is exactly `run`; no board result
  exists for dc, bc, the fork of the bc image, the wall time of `2 1000 ^ p`
  or `dc -x` near the window limit.
- bc under qemu-arm: newlib in user mode has no `fork`, so the Thumb-1 run
  covers dc only. bc's host runs cover the grammar, scanner, name table and
  the fork on the host's `fork`.
- `tests/dcbc` on a macOS host: the gate prints a SKIP there.
- `renode`: the boot of a kernel that execs dc or bc cannot run on the host
  that built this tree (the host terminates processes that listen on
  0.0.0.0).

## Steps taken to port dc and bc

1. `lib/libbn` is in `lib/Makefile` `SUBDIR` after `libutil`, and both
   programs link `-lbn`.
2. `usr.bin/dc` holds the OpenBSD sources: one compatibility header
   (`compat.h`), the deleted `pledge` and `setproctitle` calls, `basprintf` for
   `asprintf`, `errno = EISDIR; err(...)` for `errc` and the `bn_check`
   rewrite. NOTICE section 16 is now the ISC entry for Otto Moerbeek.
3. The register table is paged through `regstack()`. Falsifier for the paging
   (`dc -x` with the process window at its limit) is not run on a board; the
   host stand-in is `check-peak`, where one register under `-x` peaks at 4,413
   bytes against 790,845 for the eager table, and the mutant that allocates
   every page at start fails it.
4. `usr.bin/bc` holds `bc.y`, `scan.l`, the headers and `bc.library`, with
   the name table, the plain line read, `pledge` removed and the fork kept
   (reasons in the section above).
5. Differential check: the ILP32 repeat, the ported-source run and the
   Thumb-1 run are recorded in the last section; the board run of the corpus
   is the remaining part. Predicted result of the board run: identical
   output; a divergence names the manual-page assumption that was wrong.
6. Re-measured: linked sizes and packed sizes are in the sizes table above;
   the peak heap of a 2,001-digit factorial printed by `p` is 9,694 requested
   bytes at `-m32` (6,799 without the print), measured by `tests/dcbc/peak_alloc.c`
   (a lower bound: target allocator headers add to it). The wall time of
   `2 1000 ^ p` on the board is not measured.

Open decision for the owner: the bc image carries a second copy of the dc
evaluator (about 18 KB packed). A single multicall binary would recover it.

## Differential run against a libcrypto-linked dc

Run with the owner's approval on 2026-11-05 on the development host
(OpenSSL 3.6.5 libcrypto, host cc), with the pinned dc sources above built
twice through `tests/libbn/dc-differential/run.sh`: once against the host
libcrypto and once against `lib/libbn/bn.c`, both binaries named `dc` so
error text matches. The corpus is 3,000 programs from
`gen_corpus.py` (seed 20261105): random operands up to 60 digits with
fractions and negative signs, and the operators `+ - * / % ~ ^ v Z X p f c d
r o i a P | k`, with `o` and `i` fed negative, zero, one, oversized and
2^32+1 bases.

Result: 2,995 of 3,000 programs produce byte-identical output and exit
status, including every negative-operand case for `o`, `i`, `a`, `P`, `/`
and `%`, so the four manual-page assumptions recorded above (`BN_get_word`,
`BN_rshift` and `BN_mask_bits` on negative values, and `BN_div_word` on a
negative dividend) hold over this corpus. The five remaining programs all
raise a number to an exponent of four million or more: in three, one side
exceeds the 60-second limit; in one, both fail and only the error text
differs ("bignum too long" from libcrypto, "bignum error" from the host
shim of `ERR_reason_error_string`); in one (`4 _57170890 ^`), libbn returns
0 at scale 0 where libcrypto runs out of time. A direct check of the last
case, `4 57170890 ^ Z p`, gives 34,420,306 digits from libbn, which is
ceil(57,170,890 x log10 4), the exact length. No semantic divergence was
found. The board run of the same corpus remains open.

Scope of the first comparison. Both dc binaries were LP64 host builds, so dc's
own `u_long` held 64 bits on both sides. The libcrypto side used 64-bit
`BN_ULONG` words and the libbn side used libbn's 32-bit `BN_ULONG`. The run
therefore covers libbn's semantics and its 32-bit `BN_ULONG` word arithmetic
(over 16-bit limbs) through
dc's operators, and leaves dc's 32-bit `u_long` of the target unexercised; the
ILP32 test of libbn itself (`bn_test32`) covers libbn but not dc's own
`u_long` uses. The repeat below covers those.

### Repeat at ILP32 and on the ported sources

Run on 2026-11-06 on the same host and corpus (`gen_corpus.py 3000`, seed
20261105), side a being the pinned sources against libcrypto at LP64, with
three variants of side b through `run.sh`:

| Variant | Command prefix | Programs | Differ |
| --- | --- | ---: | ---: |
| pinned sources, libbn at `-m32` (ILP32) | `M32=yes SIDE_B=pinned` | 3,000 | 14 |
| tree sources `usr.bin/dc`, libbn at `-m32` | `M32=yes SIDE_B=ported` | 3,000 | 14 |
| tree sources `usr.bin/dc`, libbn at LP64 | `SIDE_B=ported` | 3,000 | 14 |

The three runs differ on the same 14 programs (lines 508, 809, 936, 956, 1312,
1509, 1601, 1771, 1879, 1988, 2291, 2410, 2720 and 2873 of the corpus), so
building dc at 32-bit `u_long` and `size_t`, and replacing the register table,
the compatibility layer and the error helper, changes no output over the other
2,986. Each of the 14 raises a number to an exponent of four million or more,
and each was rerun on its own with a 100-second limit on both sides:

- 10 programs (508, 936, 956, 1312, 1601, 1879, 1988, 2291, 2410, 2873):
  libcrypto stops at once with "BN failure: bignum too long" and exit 1, and
  `lib/libbn` keeps squaring past 100 seconds. libbn checks a size limit only
  as a result grows, and the host has memory for the squarings; the board has
  144 KB, where the same program ends in an allocation failure ("BN failure",
  exit 1) after the squarings that fit, which at 18 cycles per 16-bit product is
  minutes of CPU (an estimate from instruction counts, not run). A size test
  on `bits(a) x p` at the head of `BN_exp` would refuse these at once like
  libcrypto; that changes `lib/libbn` and its 14-mutant gate, so it is an open
  item for the owner and not part of this port.
- 1771: both sides exit 1 with "stack empty" and a BN failure; libcrypto adds
  "bignum too long" to the line.
- 809: libcrypto exceeds the limit and libbn exits 1 with "BN failure" at once.
- 1509: both sides exceed the limit.
- 2720 (`_57170890 10 i ^ p`): libcrypto exceeds the limit; libbn prints 0
  and exits 0, the case the first run recorded for `4 _57170890 ^`.

The first run recorded 5 differing programs, with no record of the host's
load; this run found 14 with the host at a load average of 14 to 15 on 12
cores, and the three variants agree on all 14. Load is the likely reason for
the larger count, since the libcrypto side's early refusal does not depend on
CPU while the runaway libbn programs and the slow libcrypto ones cross the
60-second limit as CPU gets scarce; the first run's program list was not kept,
so the two counts are not reconciled program by program (inferred, low
confidence). No semantic divergence appears: every differing program
is a refusal point of a huge exponent.

### Evidence for the tree port

Environment: the host of the first run, bison 3.8.2, flex 2.6.4, byacc
2.0 20260126, GNU bc 1.08.2, gcc 16.2.1, clang 23.1.4, arm-none-eabi-gcc
16.2.0 with newlib 4.6.0, qemu-arm 11.1.3, Python 3.14.9; source revision
`main` at `97a5e3b1` plus the two commits that port the programs and gate them.

- `bmake MACHINE=rp2040 check-dcbc` (exit 0): 560 cases at native width, at
  `-m32` and under ASan/UBSan, each with 0 failures; the peak-heap ceilings
  hold (one register 3,389 bytes plain and 4,413 under `-x`, three pages
  10,677, 808! printed 9,694); 13 of 13 mutants rejected (the register table's
  page index, slot index, initialization, allocation, eagerness and range, and
  the name table's key compare, rehash, growth, probe, key, count and type
  letter). The unfaulted programs pass first, as the known-good control.
- `bmake MACHINE=rp2040 -C tests/dcbc check-qemu` (exit 0): dc as a Thumb-1
  program under qemu-arm, 303 cases, 0 failures. `arm-none-eabi-size` of the
  newlib test binary is 73,704 text and 1,768 data; it is not the board
  binary.
- Cross-check of the bc cases against GNU bc 1.08.2 (`bc -q`, with `-l` where
  the case uses it): 62 of the 71 comparable cases agree byte for byte,
  including the `-l` functions `e`, `l`, `j` and the arithmetic, array and
  control-flow cases. The 9 that differ are dialect differences between the
  two programs and not defects: `||` returns the operand's value in OpenBSD
  bc (3) and 1 in GNU bc; zero prints with the current scale in OpenBSD
  (`0.00`) and as `0` in GNU; string escapes stay literal in GNU; division by
  zero prints a message and no value in GNU; a call of an undefined function
  prints 0 in OpenBSD and a message alone in GNU; and the library functions
  differ in the last digit or in the format of zero (`s(0)` at scale 20,
  `c(3.14159)` at scale 5).
- `bmake MACHINE=rp2040 logged-check` (exit 0, 2,283.6 s for the check stage):
  includes `check-dcbc`, the cross builds of both programs, `check-fs-profiles`,
  `check-c17-source-denominator` and `check-qemu`.
- The `picocalc` image leaves 189 free blocks (210 before) and `full` 280
  (unchanged), read with `fsutil --verbose --partition=1` on images from
  `logged-distribution`.
