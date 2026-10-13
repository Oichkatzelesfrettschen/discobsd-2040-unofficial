# ASCII case folding without a writable table

`strcasecmp()` and `strncasecmp()` fold ASCII uppercase letters and compare
unsigned byte values. Typed C17 definitions replace the writable 256-byte
character map with a bounded arithmetic operation. The fixed POSIX byte
locale preserves bytes outside ASCII uppercase, and both functions preserve
errno. The bounded function checks the remaining count before reading.

The source baseline is `0aeee4386e6c0ba452da75435c951021cfaed4c0`.
The inherited map additionally folds 0xC1 through 0xDA to 0xE1 through 0xFA.
Its plain-char entries also make ordering depend on compiler signedness.
The replacement deliberately removes that historical high-byte equivalence;
callers that rely on an eight-bit regional encoding need a separate locale
contract. The repository's fixed byte locale supplies the bounded contract.
Issue 7 and Issue 8 references reside in `sus-edition-contracts.md`.

## Exhaustive finite evidence

The independent oracle looks up letters in explicit upper/lower alphabets.
Every one-byte pair, including NUL, exercises both APIs: 65,536 pairs per API
under each plain-char signedness. Separate cases cover zero count, exact
one-byte objects, first-NUL termination, prefixes, and SIZE_MAX with empty
strings. Every valid comparison checks unchanged errno. Native, ILP32 and
ASan/UBSan variants pass. Independent GCC and Clang strict C17 builds also
pass with both signedness selections, including leak detection.

Running the same domain against the baseline changes 1,014 comparison signs
under unsigned char and 33,782 under signed char. The mapping contribution
is `52 + 312 + 650 = 1014`: 26 bidirectional equivalences, 26 letters crossing
six intervening bytes in both directions, and 26 times 25 cross-order pairs.
The signedness contribution is `2 * 128 * 128 = 32768`, the ordered pairs
that cross the high-bit boundary. The two causes explain the measured total;
a source-line reduction alone cannot establish that semantic result.

Four calibrated mutations restore high-byte folding, introduce signed
ordering, truncate the count, or clobber errno. Each fails its named assertion.
The Cortex-M0+ gate compiles exact source and inspects symbols and sections.
Its calibrated inspector accepts the two code exports and rejects imports,
permanent storage, malformed listings and failed inspection commands.

Select and export PYTHON, then run:

```
bmake MACHINE=rp2040 check-libc-casefold check-libc-casefold-cross
bmake MACHINE=rp2040 check-test-execution check-lint
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check
```

The root host/cross gates and required ILP32 execution own the checks.
Production measurements use arm-none-eabi GCC 16.2.0 and bmake 20260912.
The clean distribution and complete serial non-hardware aggregate pass,
including all 18 Smaller C cases and the damaged/restored filbuf archive
control. Existing host shell/libtcl links emit glibc diagnostics for tmpnam,
getwd and mktemp; production C compilation treats warnings as errors.

## Linked resource accounting

| Surface | Before | After |
| --- | ---: | ---: |
| case-folding object text/data/BSS | 84/256/0 | 100/0/0 |
| strcasecmp / strncasecmp static frames | 16 / 16 | 12 / 16 |
| Full ELF libc bytes / members | 406816 / 380 | 406440 / 380 |
| Full a.out libc bytes / members | 115934 / 370 | 115682 / 370 |
| Selected board libc bytes / members | 56058 / 151 | 55782 / 151 |
| Root free blocks | 265 | 265 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

`libc-ascii-casefold-linked-extents.csv` records all 275 installed OMAGIC
executables. Twelve consumers each gain 16 text bytes, shed 256 initialized
data bytes and retain equal BSS: sysbox, mount, calendar, chfn, chpass, chsh,
crontab, login, pdc, vi, monop and cron. Each flat process load image therefore
shrinks by 240 bytes. The other 263 executable records compare equal.
Of 150 extracted selected-board archive objects, only strcasecmp.o changes.
The target object imports no symbols.

The packed sysbox payload shrinks by 227 bytes, login by 235 bytes and the
selected libc by 276 bytes. Sysbox aliases share their packed payload and
must not multiply its storage charge. Block quantization leaves the measured
free-block count equal. The 144 KB flat process window makes code part of
process RAM: the 256-byte data reduction yields a 240-byte image reduction,
rather than an additive 256-byte whole-process saving. Stack frames remain a
separate account. Kernel capacity and flash-write traffic require separate
measurements; these builds establish neither wear nor runtime high-water.

Fresh clean builds at the baseline and candidate revisions reproduce the
comparison. Inspect OMAGIC headers, ELF sections and extracted archive
members separately; container metadata does not identify object changes.
Physical-board execution and locale-aware multibyte comparison stay outside
this fixed-byte implementation's evidence boundary.
