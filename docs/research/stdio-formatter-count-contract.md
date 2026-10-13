# Formatter counts, arithmetic and output failure

The comparison below retains 1024-byte scratch. The subsequent capacity unit
in `stdio-output-batch-capacity.md` defines the 256-byte implementation and
its independent measurements.

Formatted output has two independent quantities: the bytes a destination
stores and the length the format requires. `_doprnt` accounts for logical
length in a checked `int` while bounded string output copies or fills only
available capacity. Exhausted string capacity stays at zero. A zero-capacity
`snprintf` therefore handles a representable INT_MAX field in bounded time;
a following character returns EOF with EOVERFLOW. Real streams stop at the
first failed output and preserve the write error before any later `%n` store.

The field preflight checks `max(width, body_length)` against the remaining
count. Numeric body length includes precision zeros, sign and hexadecimal
prefix exactly once. Decimal parsing checks the next digit before the multiply
and groups digit subtraction before addition. The expression
`n * 10 + c - '0'` overflows at the last digit of 2147483647 even when its
mathematical result fits; `n * 10 + (c - '0')` preserves the checked bound.
INT_MIN star width fails before negation. Character width includes the
character, negative star width sets left adjustment, and octal alternate form
adds only a missing leading zero. Integer precision disables zero padding.

`printf` and `vprintf` return `vfprintf` results. `sprintf` and `vsprintf`
return formatter failures, terminate the produced prefix, and use INT_MAX
capacity instead of the historical 32767-byte ceiling. The caller continues
to own sufficient destination storage. Public headers and the FILE layout
retain their interfaces.

## Execution contracts

Select and export PYTHON, then run sequentially:

```
bmake MACHINE=rp2040 check-libc-output check-libc-output-cross check-test-execution
bmake MACHINE=rp2040 distribution
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 check
```

The output gate exercises exact source at native and ILP32 widths, under
ASan/UBSan, and through five wrapper mutations. A separate arithmetic runner
executes integer and floating formatter fixtures under ASan/UBSan with a
15-second timeout. Its known-bad mutation restores the ungrouped decimal
expression and requires the signed-overflow UBSan diagnostic and exit 1.
The runner sets `abort_on_error=0:exitcode=1` explicitly; an abort-policy
control produces the diagnostic through SIGABRT and fails the exit contract.
Unexpected control failures retain status, stdout and stderr for diagnosis.
Four finite pre-change counterexamples reject the baseline before huge-width
cases run: character width, negative star width, alternate octal precision,
and alternate octal zero. Maximal literal width/precision, cumulative count,
sign/prefix additions, float precision, truncation and overflow exercise the
positive and failing arithmetic boundaries.

The output fixture checks printf/vprintf counts, sprintf/vsprintf errors,
a 32768-byte sprintf result, stream failure before `%n`, and read-only output
failure. Portable host fixtures link the real long-long helper because Mach-O
requires that weak reference to resolve. Those fixtures establish the enabled
helper path; helper-absent target behavior remains a separate coverage boundary.

The Smaller C t18 consumer checks overflow errno, maximal bounded output,
alternate octal precision and printf's returned count. Its execution starts
after host preprocessing and uses the repository's target libc archive.
On-device preprocessing and board execution retain separate evidence rows.

## Resource and integration evidence

The comparison base is the wrapper ownership implementation at `67587250`.
The wrapper fixture portability follow-up changes host test linkage only.
Paired production archives, all installed OMAGIC extents, packed-root payloads,
kernel sections and compiler frames supply the resource account. Object text
alone establishes neither process peak RAM nor board stack usage. Scratch
batch capacity remains 1024 bytes during the arithmetic comparison.

Paired GCC 16.2.0 production-flag compiles report:

| Member | Before text | After text | Before frame | After frame |
| --- | ---: | ---: | ---: | ---: |
| doprnt | 4107 | 2927 | 448 | 448 |
| emit_bytes (within doprnt.o) | included above | included above | absent | 32 |
| printf | 40 | 28 | 16 | 16 |
| vprintf | 32 | 20 | 8 | 8 |
| sprintf | 56 | 52 | 44 | 44 |
| vsprintf | 44 | 44 | 32 | 32 |

Each member has zero data/BSS. The shared emission helper adds a 32-byte
callee frame; the reduction in text does not imply a reduction in stack.
The compiler frame figures exclude callers, interrupt demand and hardware
measurement. `printf`/`vprintf` now reach vfprintf's unbuffered batching path,
so their call chains require the retained 1024-byte scratch where `_IONBF`
is set. The scratch unit must evaluate that complete call chain.

The syslog host fixture owns a separate doprnt object bound to
`syslog_test_errno`. Reusing printf's object leaves a reference to
`printf_test_errno` once formatter arithmetic reports errno; a dedicated
object makes the syslog formatter and consumer share the tested error state.

`stdio-formatter-linked-extents.csv` records all 275 installed OMAGIC
programs. The paired executable receipts change 248 records and preserve
27; text deltas range from -1208 to +124 bytes. Every data extent stays equal.
vmstat's a.out BSS extent grows from 676 to 680 bytes, while paired ELF links
retain a 676-byte BSS section and identical vmstat.o hashes. The intervening
alignment gap grows from zero to four bytes; the extent change represents
layout padding rather than an additional BSS object.

The selected archive changes only doprnt.o (6960 to 5248 bytes), printf.o
(152 to 132) and sprintf.o (144 to 140). Its other 148 object members compare
byte-identically. Archive counts include the symbol index. Centralizing
emission removes repeated inlined putc machinery, while the helper supplies
one checked counting and failure boundary. Smaller object text, packed-root
savings and added helper stack are separate consequences of that mechanism.

The native, ILP32, ASan/UBSan, mutation, Cortex-M0+ compile, Clang fixture,
syslog and complete serialized aggregate gates pass. All 18 Smaller C
programs execute successfully; the copied-archive filbuf mutation fails and
the restored archive passes. The aggregate's first attempt exposes the
syslog fixture errno dependency described above. Existing host shell/libtcl
links retain glibc diagnostics for tmpnam, getwd and mktemp. Physical-board
execution remains open. These receipts establish the exercised interfaces,
rather than complete ISO C or SUS conformance.

| Final distribution surface | Before | After |
| --- | ---: | ---: |
| Full ELF libc bytes / members | 406384 / 380 | 405160 / 380 |
| Full a.out libc bytes / members | 115662 / 370 | 114326 / 370 |
| Selected and staged board libc bytes / members | 56042 / 152 | 54306 / 152 |
| Root free blocks | 262 | 280 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

The selected and staged archive hashes match after final distribution.
The build target installs full libc into the staging tree, so the comparison
regenerates distribution after the build/check sequence. Packed box, sysbox
and adminbox payloads shrink by 548, 529 and 538 bytes; packed login shrinks
by 573 bytes. The selected libc payload shrinks by 1736 bytes. Hard-linked
aliases share payloads. The image gains 18 free blocks; kernel section sizes
stay equal. The comparison measures storage and linked extents, while runtime
stack peaks and flash traffic remain physical-board questions.
