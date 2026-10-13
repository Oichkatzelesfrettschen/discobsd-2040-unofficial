# Stdio output wrapper ownership and results

The comparison below retains 1024-byte scratch. The subsequent capacity unit
in `stdio-output-batch-capacity.md` defines the 256-byte implementation and
its independent measurements.

`fprintf()` delegates to `vfprintf()` and returns the formatter's count.
Unbuffered `vfprintf()` and `fputs()` batch through a per-call FILE copy with
an explicitly initialized 1,024-byte automatic buffer. The copy borrows the
descriptor; the original FILE retains its buffer base, pointer and capacity.
Output-mode and error flags propagate back, and the original count becomes
zero. `fputs()` stops at the first failed output and reports a final flush
failure. Both wrappers release automatic storage without publishing an
expired pointer into the original stream.

The baseline is `b8c836de098fa0cef31b2531fef5d32a3452f4df`. Its fprintf
returns zero on success, vfprintf omits temporary capacity initialization,
and all three wrappers leave expired pointers and discard original buffer
metadata. fputs ignores its final flush result and continues after a failed
putc. The exact-source fixture rejects 21 baseline assertions; the corrected
source passes. Buffer size remains fixed while the ownership contract closes.

## Contract gates

`check-libc-output` compiles the exact wrappers, formatter and output core
against the tree's FILE definition. The fixture links the real long-long
helper, matching the printf fixture: Mach-O requires the referenced symbol
where an ELF link accepts the absent weak helper. A write shim records payload and call
count, injects short writes or failures, and supplies the deciding errno.
The fstat shim writes the tree's layout rather than a host struct stat.
Native, ILP32 and ASan/UBSan variants exercise:

- Formatted return counts and buffered output without an early write.
- Single-write small outputs and three-write outputs of 2,049 bytes.
- Final-flush failures and first-failure termination for fputs.
- Preserved stream-owned, caller-owned and heap-owned buffer metadata.
- Update-stream entry from neutral or read/EOF state, on success and failure.
- Cleared read/EOF flags, retained ownership, set output mode and error state.

Five mutations lose counts, capacity initialization, pointer lifetime,
final-flush results or first-failure termination. Each fails its named
assertion. `check-libc-output-cross` compiles the wrappers as strict C17
for Cortex-M0+. The execution inventory and required ILP32 route own the
conditional host receipts.

The Smaller C t18 fixture checks a buffered fprintf count and payload, and
an unbuffered fputs invalid-descriptor failure with pointer cleanup. The link
map must name fprintf.o, vfprintf.o and fputs.o in addition to the existing
input and directory members. The Linux write shim's raw errno representation
remains outside these assertions; the fixture checks return/error flags.

Delegation changes archive closure: the selected board archive needs
vfprintf.o. A target consumer reproduces an undefined-vfprintf link failure
before that member enters `boardlibc-members`. `check-board-libc-output`
compiles the consumer, assembles it with the tree tool and links the selected
a.out archive. Removing vfprintf.o from a temporary archive copy restores
the named failure. The gate establishes a link contract, separate from
consumer execution. Wrapper sources become selected-archive prerequisites.

Select and export PYTHON, then run these commands sequentially:

```
bmake MACHINE=rp2040 check-libc-output check-libc-output-cross check-test-execution
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check-board-libc-output
bmake MACHINE=rp2040 check
```

The focused gates, five mutations, inventory selftests, Ruff, ShellCheck,
clean distribution and selected-archive positive/negative links pass.
Independent review covers the ownership matrix and closure repair.
The final serial non-hardware aggregate passes, including all 18 Smaller C
executions, the damaged/restored filbuf archive control and the selected
board archive link control. Existing host shell/libtcl links retain glibc
diagnostics for tmpnam, getwd and mktemp; production warnings are errors.
An earlier aggregate overlaps an image rebuild and loses kernel intermediate
objects; that interrupted receipt establishes neither a product failure nor
a passing aggregate. Builds and aggregate checks share mutable output trees
and require serial invocation within one checkout.

## Resource accounting

Measurements use arm-none-eabi GCC 16.2.0 and bmake 20260912. Compiler
stack-usage reports describe frames; they do not measure board stack peaks.

| Surface | Before | After |
| --- | ---: | ---: |
| fprintf text/data/BSS | 100/0/0 | 22/0/0 |
| vfprintf text/data/BSS | 92/0/0 | 140/0/0 |
| fputs text/data/BSS | 172/0/0 | 216/0/0 |
| fprintf reported frame | 1052 dynamic | 12 static |
| vfprintf reported frame | 1048 dynamic | 1080 dynamic |
| fputs reported frame | 1056 dynamic | 1080 dynamic |
| Full ELF libc bytes / members | 406440 / 380 | 406384 / 380 |
| Full a.out libc bytes / members | 115682 / 370 | 115662 / 370 |
| Selected board libc bytes / members | 55782 / 151 | 56042 / 152 |
| Root free blocks | 265 | 262 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

fprintf's small wrapper frame does not remove the callee's scratch: its
call chain includes vfprintf. The repaired ownership adds metadata overhead;
a later chunk-size decision needs byte/write-count evidence. The selected
archive adds vfprintf and changes fprintf/fputs; its other 148 object members
compare byte-identically. Archive member counts include the symbol index.

`stdio-output-linked-extents.csv` records all 275 installed OMAGIC programs.
216 executable records change; 59 compare equal. Text changes range from
minus 28 to plus 112 bytes, and every data extent stays equal. BSS stays equal
except vmstat, whose recorded extent falls by four bytes. Paired vmstat linker
maps preserve the 676-byte BSS section and the identical vmstat input object;
the preceding data-to-BSS alignment gap falls from four bytes to zero.
That alignment change explains the a.out extent rather than a removed object.

Packed box/sysbox/adminbox payloads grow by 22/32/42 bytes, login grows by
121 bytes, and selected libc grows by 260 bytes. Aliases share their payloads.
The image loses three free blocks. These costs purchase corrected behavior
and retained ownership; the unit claims neither a RAM reduction nor flash
wear improvement. The unchanged kernel section sizes describe build extents.

## Remaining formatted-output work

The formatter arithmetic and result unit is documented in
`stdio-formatter-count-contract.md`. The scratch unit evaluates smaller chunks
and their write amplification after the formatter contract closes.
Physical-board stack peaks, capacity and persistence remain distinct
measurements.
