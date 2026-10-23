# RP2040 flat ELF load admission

The RP2040 ELF loader formerly allocated and read the program-header table
before proving that the table fit inside the executable inode. It accepted a
segment without bounding its file bytes by the inode, rejected the valid
zero-BSS case, omitted Thumb entry validation and could reach `exec_estab`
before detecting invalid segment geometry. The loader also ignored short reads
and retained the final segment-read status only in a local variable, so an
incomplete image could reach `exec_clear`, stack publication and a successful
return after the old process image was already unavailable.

The shared stack publisher also subtracted a byte count from `char **`, so C
pointer scaling multiplied the argument-vector reservation by the pointer
width. A sufficiently large valid argument vector could therefore publish
pointers and the saved stack pointer below the stack extent admitted by
`exec_estab`. Successful ELF replacement also retained the preceding a.out
image's `p_tip` and `p_tsize`; swapout then omitted that stale text length and
swapin restored the preceding executable over the new ELF image.

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

`exec_setupstack` subtracts argument-vector bytes through `char *` arithmetic
and derives the saved stack pointer from `sizeof *u.u_frame`. The publisher's
lowest address therefore uses the same byte units as the admitted stack extent.
After `exec_estab` commits an ELF image, the loader releases the preceding
file-backed text owner and sets `p_tsize` to zero. The flat RWX ELF segment is
fully swap-backed, so swapout writes the complete image and swapin restores no
prefix from an executable inode.

After `exec_estab` succeeds, a segment read error or nonzero residual describes
an incomplete committed image. The loader marks `EXEC_COMMITTED_FAILURE`,
returns the I/O error or `EIO` for a short read, and reaches neither
`exec_clear` nor `exec_setupstack`. The flag survives recursive script
execution in the private `exec_params` contract. The syscall owner releases
the argument spool, allocator buffers and outer executable inode before it
calls `exit(SIGKILL)` directly.

Queuing `SIGKILL` through `psignal` does not establish that terminal contract.
A traced process can let its tracer replace the pending signal with zero, and
the generic signal path discards default actions for PID 1. Direct exit clears
tracing and makes an ordinary process a zombie; PID 1 reaches the kernel's
existing `init died` panic. Neither path can return to the overwritten user
image. Raw a.out and packed a.out loaders use the same common terminal state
for their post-commit read or expansion failures. Successful reads preserve
the existing entry value, ABI and publication sequence.

`tests/kernel/exec_elf_admission_extract.py` extracts the exact production
loader, admission, stack-publication, text-release, swap-text-selection and finalizer
functions and binds each generated source to its complete production-file
hash. `exec_elf_admission_test.c` compiles the loader and finalizer at
ILP32 and separates inode length, program-header reads, segment reads, commit,
clearing, resource release, termination and stack publication. The fixture
covers a valid image, zero BSS, multiple program
headers, negative inode length, segment type, permissions, base and nonempty
file requirements, file bytes larger than memory, a segment beyond the process
ceiling, truncated or wrapped segment bytes, wrapped and truncated
program-header spans, entries below or beyond file-backed bytes, an even Thumb
entry, failed and short header reads, argument and process-window admission
errors, a committed I/O error and a committed short read. Traced-process and
PID 1 cases require allocation and inode cleanup before direct termination.
The inherited function fails 34 named assertions; the repaired functions pass
the complete fixture.

`exec_elf_surrounding_contract_test.c` executes the production loader with the
PICO_UART branch of `exec_estab`, the production `exec_setupstack` and the
production `swaptext` selector. A boundary image carries 200 empty arguments,
uses the complete admitted process window and preserves sentinel bytes below
the stack. The published argument vector and saved stack pointer must remain at
or above the admitted stack base. A replacement case begins with a 64-byte
file-backed a.out text owner and requires its release, a zero `p_tsize` and a
zero swapin executable-text selection after successful ELF publication.

Five generated mutations calibrate the deciding contracts. One removes the
inode bound from the segment file-span check. Another returns a committed-read
error without marking the terminal state. The third replaces direct exit with
the suppressible queued-signal behavior. The fourth restores the pointer-scaled
stack calculations, and the fifth retains the preceding executable's text
owner. Each mutant must run its control fixture and exit with status 1.
`check-test-execution` owns the run/skip inventory, and `check-kernel-ilp32`
owns both executables and all five rejection controls. Exact PICO and PICO_UART
GNU17 Cortex-M0+ commands compile `exec_elf.o` and `exec_subr.o` with warnings
treated as errors.

The measurements below bind the corrected candidate at
`20055ae0edff157a457414fe6f8010ea24e03230` to the clean parent at
`4dc6be2afee29283b4eb0d4f2230c075e3933784`. Both captures use Arm GCC
16.2.0, GNU ld 2.47.20260726 and bmake 20260912. Parent clean and distribution
stages completed in 9.3 and 168.5 seconds; corrected-candidate stages completed
in 8.9 and 223.7 seconds. Both closure receipts account for 36 shipped artifact
groups, 173 PICO C/header paths, 165 PICO_UART C/header paths and the same
151-member, 43,010-byte native board `libc.a`.

The parent source, closure and resource receipts have SHA-256 values
`65bf2d9edb9726a37b44a54edf309f25512cab3bd443a1e389690d885dc3d999`,
`2187c98b9e756fe5b54584c962f4e28b96546ff54b42bbef99882c62f0a9b34c`
and `cf3f782b866b55bdef92dbea5415519e8ae6bcf9e62780a175575a163100e485`.
The candidate source, closure and resource receipts have SHA-256 values
`564510b1ea9fb32a71da6d1a9826529023bd40d786f2eeb2ae5d7f00fbfcbf43`,
`80bee21c280a15208ec4a44649e8c33b15567020b61ae9c78368575eb6fab50d`
and `31ebc24ce6d6663575728eab4774c37cea5b0cc6e6dd67a036c46a752ae5a514`.
The delta receipt has SHA-256
`b008167c42c90c4bbb8e82c07323e904eb4270f36e694544c6d62ee9d41cd8ed`.
The candidate clean and distribution receipts have SHA-256 values
`698745ecaa518622b06e80b2da21a54e092617e2de338dd1b6c29eec219b0a4c`
and `7762e5a273e80d1a9822e8a310d0b485ec699237dcdaf82e2a43676a38244837`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,806 | 104,902 | +96 | 26,170 bytes |
| PICO SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| PICO_UART loaded flash span | 92,324 | 92,412 | +88 | 38,660 bytes |
| PICO_UART SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

Exact production commands augmented with `-fstack-usage` and reproducible path
maps bind object size, symbol size, disassembly and static-frame results for all
six changed exec translation units to each source revision. Repeating the
capture produced byte-identical receipts. `exec_aout.o` and `exec_script.o`
retain their parent text, data, BSS and static-frame sizes in both
configurations.

| Configuration | Changed-object text delta | Changed symbol sizes | Static-frame changes |
| --- | ---: | --- | --- |
| PICO with SYSTRACE | +100 | `exec_hsaout_check` +4, `exec_elf_check` +48, `exec_save_args` +16, `execve` +24 | `exec_elf_check` 56 to 64; `execve` 392 to 400 bytes |
| PICO_UART | +88 | `exec_elf_check` +44, `exec_save_args` +12, `execve` +24 | `execve` 392 to 400 bytes |

The parent and candidate static-resource receipts have SHA-256 values
`494cbe57ce0689485e66b95e94f5dfc01aae908074a12166b975cdf94b15bfcd`
and `960e5254f7c169f4f1de07e032da7dccb442b381fedb9010f12c5a35e28fd2d8`.
Their comparison receipt has SHA-256
`7283ad08400d19300039a6aec26a84123ec9be9bdb24771d33a78ad6be4600e9`.
The resource comparer rejects both positive flash deltas by design pending
review. The candidate requests bounded exceptions for 96 PICO flash bytes, 88
PICO_UART flash bytes, eight static `execve` frame bytes in both configurations
and eight static PICO `exec_elf_check` frame bytes. The deepest changed PICO
call chain can therefore use 16 additional static bytes; PICO_UART can use
eight. Complete geometry admission and terminal handling of incomplete
committed images require the added checks and state. Removing them would
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
