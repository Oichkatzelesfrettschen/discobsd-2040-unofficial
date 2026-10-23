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

The host fixture establishes ILP32 geometry, ordering and call effects through
controlled stand-ins. It does not execute `rdwri`, replace a live process,
exercise MPU permissions or run on RP2040 silicon. Runtime kernel stack, user
heap and storage traffic remain unmeasured. Section-header contents, ELF
variants beyond the one flat segment and the remaining translation-unit
semantics retain separate migration obligations. Physical-board execution
requires separate authorization for the exact image and workload.
