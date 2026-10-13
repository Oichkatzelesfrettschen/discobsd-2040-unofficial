# Unbuffered stdio batch capacity

Unbuffered `vfprintf` and `fputs` borrow a descriptor through a temporary FILE
and flush before returning. Their private `STDIO_OUTPUT_CHUNK` is 256 bytes;
public BUFSIZ remains 1024. The distinction bounds per-call stack demand
without resizing persistent stream buffers or changing the FILE ABI.
`fprintf`, `printf` and `vprintf` reach the same vfprintf implementation.

For a successful unbuffered output of L bytes with no line-buffer flag,
the write-call count is `ceil(L / C)`, where C is the private capacity and
an empty output makes zero writes. For large outputs the syscall count scales
as `L / C`; wrapper scratch scales as C. The measured Cortex-M0+ wrapper frame
is `C + 56` bytes with the selected compiler flags. These coefficients describe
software storage and batching, rather than physical flash transactions.

| Candidate C | Wrapper frame | Writes for 2049 bytes |
| ---: | ---: | ---: |
| 1024 | 1080 | 3 |
| 512 | 568 | 5 |
| 256 | 312 | 9 |
| 128 | 184 | 17 |

The 256-byte choice removes 768 bytes from either wrapper frame and preserves
a single write for outputs up to 256 bytes. The 128-byte candidate saves
another 128 bytes but nearly doubles the long-output call count. The
512-byte candidate preserves longer single-write outputs while saving only
512 bytes. The selection favors bounded stack demand on the flat 144 KiB
process window. The existing alloca remains conditional on unbuffered mode.

The formatter adds its 448-byte frame and the emission helper's 32-byte
frame on relevant call paths. These compiler reports describe individual
frames, not interrupt demand or measured runtime high-water marks. The
capacity decision never counts a callee frame reduction as a whole-process
RAM measurement.

## Contract and failure evidence

The exact-source output fixture exercises 12 lengths around one/two chunks,
1024 bytes and 2049 bytes across fprintf, vfprintf and fputs. Every possible
failing write position runs as both a failed write and a short write. The
fixture checks return/error state, write count, maximum write length, exact
delivered prefix, and release of temporary pointers. A short write contributes
its delivered prefix and terminates output; the implementation retains the
existing failure policy rather than retrying or duplicating bytes.

Six mutations independently remove return counts, capacity initialization,
pointer lifetime, final-flush failure, first-failure termination or the selected
chunk bound. The capacity mutation restores both allocation and capacity to
BUFSIZ in fputs, so the test rejects a behavioral regression without creating
an artificial undersized allocation. Native, ILP32, sanitizer and Cortex-M0+
gates exercise the configuration. The execution inventory names all six
controls. The fixture uses the private constant as its configuration input;
paired production compilation independently measures the resulting frames.

`output.h` is internal to libc. ELF, a.out and selected-archive dependencies
include it, and mutation compiles resolve it through an explicit private include
path. Smaller C sees the unchanged public stdio header and the cross-built
archive members. Existing buffered/update-stream/ownership cases remain in
the same fixture.

Select and export PYTHON, then run sequentially:

```
bmake MACHINE=rp2040 check-libc-output check-libc-output-cross check-test-execution
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check
```

Distribution depends on build and establishes the aggregate's build
prerequisite. Build and aggregate invocations share mutable outputs and run
serially. The comparison base is formatter commit `e269c916`.

Paired GCC 16.2.0 production compiles reduce vfprintf text from 140 to 134
bytes and fputs text from 216 to 210 bytes. Both objects retain zero data/BSS;
both reported dynamic frames fall from 1080 to 312 bytes. Physical-board
process/kernel peaks, syscall latency, storage traffic and persistence stay
separate measurements. Smaller batches can increase write traffic; the
change claims stack-frame savings and retains that cost explicitly.

## Integrated build and storage measurements

The fresh distribution and complete serialized non-hardware aggregate pass,
including all 18 Smaller C programs and the damaged/restored archive control.
GCC and Clang output fixtures pass, as do Ruff, ShellCheck and inventory
calibration. Existing host shell/libtcl link diagnostics name tmpnam, getwd
and mktemp. The aggregate includes target builds; physical-board execution
remains open.

| Surface | Before | After |
| --- | ---: | ---: |
| Full ELF libc bytes / members | 405160 / 380 | 405116 / 380 |
| Full a.out libc bytes / members | 114326 / 370 | 114318 / 370 |
| Selected/staged libc bytes / members | 54306 / 152 | 54258 / 152 |
| Root free blocks | 280 | 281 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

The selected/staged archive hashes match. `stdio-batch-linked-extents.csv`
records all 275 installed OMAGIC programs; 246 records change and 29 stay
equal. Text deltas are -16, -12, -8, -4 or zero bytes; all data extents stay
equal. Three BSS extents change by four bytes: vmstat shrinks, iostat and
whetstone-double grow. Paired ELF links retain BSS sections of 676, 172 and
460 bytes respectively. The data-to-BSS gaps explain those changes; paired
vmstat/iostat input objects and whetstone source hashes match.

Packed box grows by four bytes, while sysbox, adminbox and login shrink by
22, 19 and 18 bytes. Compression does not preserve the ordering of raw text
sizes. The selected libc shrinks by 48 bytes and the image gains one free
block; hard-linked aliases share payloads. The build establishes these
storage extents separately from the 768-byte per-wrapper frame reduction.
