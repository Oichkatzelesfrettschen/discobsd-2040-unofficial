# RP2040 flat ELF load admission

The RP2040 ELF loader formerly allocated and read the program-header table
before proving that the table fit inside the executable inode. It accepted a
segment without bounding its file bytes by the inode, rejected the valid
zero-BSS case, omitted Thumb entry validation and could reach `exec_estab`
before detecting invalid segment geometry. The loader also ignored short reads
and retained the final segment-read status only in a local variable, so an
incomplete image could reach `exec_clear`, stack publication and a successful
return after the old process image was already unavailable.

`exec_elf_check` supports one flat load-segment form. The form differs from the
two-segment text/data ELF intermediate that `lib/elf32-arm.ld` produces for
shipped programs; `elf2aout` converts those intermediates to a.out before
installation. The compiled ELF loader remains reachable for filesystem
executables with its supported shape. It requires exactly one program header
and admits the complete header span against a nonnegative inode size before
allocation or I/O. A complete header read must then describe one RWX
`PT_LOAD` segment at `__user_data_start` with
`0 < p_filesz <= p_memsz <= USER_WINDOW_MAX`. The addition
`p_offset + p_filesz` must fit in the unsigned ELF field and the resulting
half-open file span must fit inside the inode. The ELF entry retains its Thumb
bit, while the cleared address must lie inside the segment's file-backed bytes.
The loader therefore excludes BSS and bytes outside the segment from the set of
possible first instructions.

The admitted file bytes become the data extent, and `p_memsz - p_filesz`
becomes the BSS extent. A zero difference is valid. The heap starts after BSS.
The stack extent uses the same word-rounded argument and environment byte count
as the a.out loader. `exec_estab` accounts for data, BSS, heap and stack against
the selected 144 KiB base window or the existing conditional extension before
core allocation. Every file, segment and entry check runs before that commit,
so a rejected image preserves the running process.

After `exec_estab` succeeds, a segment read error or nonzero residual describes
an incomplete committed image. The loader sends `SIGKILL`, returns the I/O
error or `EIO` for a short read, and reaches neither `exec_clear` nor
`exec_setupstack`. `SIGKILL` prevents any inherited catchable handler from
running out of overwritten process memory. Successful reads preserve the
existing entry value, ABI and publication sequence.

`tests/kernel/exec_elf_admission_extract.py` extracts the exact production
function and binds generated source to the complete production-file hash.
`exec_elf_admission_test.c` compiles that function at ILP32 and separates inode
length, program-header reads, segment reads, commit, clearing and stack
publication. The fixture covers a valid image, zero BSS, multiple program
headers, negative inode length, segment type, permissions, base and nonempty
file requirements, file bytes larger than memory, a segment beyond the process
ceiling, truncated or wrapped segment bytes, wrapped and truncated
program-header spans, entries below or beyond file-backed bytes, an even Thumb
entry, failed and short header reads, argument and process-window admission
errors, a committed I/O error and a committed short read. The inherited
function fails 34 named assertions; the repaired function passes the complete
fixture.

Two generated mutations calibrate the deciding contracts. One removes the
inode bound from the segment file-span check. The other removes the
post-commit read-failure block. Each mutant must run the same fixture and exit
with status 1. `check-test-execution` owns the run/skip inventory, and
`check-kernel-ilp32` owns the executable and both rejection controls. Exact
PICO and PICO_UART GNU17 Cortex-M0+ commands compile `exec_elf.o` with warnings
treated as errors.

The clean parent at `4dc6be2afee29283b4eb0d4f2230c075e3933784` and the
committed candidate at `07178858fd514ad6a3bc39e6aec1def5fd53c8d6` use Arm
GCC 16.2.0, GNU ld 2.47.20260726 and bmake 20260912. Parent clean and
distribution stages completed in 9.3 and 168.5 seconds; candidate stages
completed in 5.2 and 206.0 seconds. Both closure receipts account for 36
shipped artifact groups, 173 PICO C/header paths, 165 PICO_UART C/header
paths and the same 151-member, 43,010-byte native board `libc.a`.

The parent source, closure and resource receipts have SHA-256 values
`65bf2d9edb9726a37b44a54edf309f25512cab3bd443a1e389690d885dc3d999`,
`2187c98b9e756fe5b54584c962f4e28b96546ff54b42bbef99882c62f0a9b34c`
and `cf3f782b866b55bdef92dbea5415519e8ae6bcf9e62780a175575a163100e485`.
The candidate source, closure and resource receipts have SHA-256 values
`9bc8c9884fbbfc1f1f9b987762ea2e466ae7ba89712acf218c7133253b7c2e7e`,
`599dbcd424d7991757bab97ca9438b89c7da717743ab1c38ea874f7f9921c730`
and `923a84c35e1dfd4a9b80cca437750e5ccb2cb94f45491cb36c1e26d7ba31f017`.
The delta receipt has SHA-256
`5c73ef740ae9535ee641105bf36746144d03f697bf34f25642906dac5d065130`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,806 | 104,862 | +56 | 26,210 bytes |
| PICO SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| PICO_UART loaded flash span | 92,324 | 92,380 | +56 | 38,692 bytes |
| PICO_UART SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

Exact production commands augmented with `-fstack-usage` and reproducible path
maps bind object size, symbol size, disassembly and static-frame results to each
source revision. Repeating the capture produced byte-identical receipts.

| Configuration | Parent object text | Candidate object text | `exec_elf_check` delta | Static-frame delta |
| --- | ---: | ---: | ---: | ---: |
| PICO with SYSTRACE | 573 | 629 | 540 to 588 bytes | 56 to 64 bytes |
| PICO_UART | 557 | 613 | 524 to 572 bytes | 56 to 56 bytes |

The parent and candidate static-resource receipts have SHA-256 values
`965cd676c3b157dc194b96169768c02831c979360d496595f0b2e228a1151100`
and `3dbf02279f42b128ea9a9d7d3bea62c4d2b13b88c2a41de5b92640335807d3fc`.
The resource comparer rejects both positive flash deltas by design pending
review. The candidate requests the bounded flash exception and the eight-byte
PICO static-frame exception because complete geometry admission and committed
read-failure suppression require the added checks. Removing the checks would
restore the reproduced out-of-inode segment admission or incomplete-image
publication defects. Runtime peak stack remains unmeasured, and the retained
220-byte stack-paint remainder belongs to an older image and workload rather
than an available budget.

The host fixture establishes ILP32 geometry, ordering and call effects through
controlled stand-ins. It does not execute `rdwri`, replace a live process,
exercise MPU permissions or run on RP2040 silicon. Runtime kernel stack, user
heap and storage traffic remain unmeasured. Section-header contents, ELF
variants beyond the one flat segment and the remaining translation-unit
semantics retain separate migration obligations. Physical-board execution
requires separate authorization for the exact image and workload.
