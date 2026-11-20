# Component placement across PicoCalc memory domains

The PICOCALC_SD image selects a backing store, an execution address, and a recovery source separately. An executable can occupy sectors on the card, execute in on-chip SRAM, and recover its clean text from the same inode after a process switch. Counting its SD size alone misses both the SRAM peak and the work of reconstruction.

## From reset to recovery

| Transition | Owner and representation | Deciding observation |
| --- | --- | --- |
| ROM to boot2 to kernel | The immutable boot ROM enters the QSPI boot path. The SD map reserves the first 256 bytes for boot2 and the remainder of 160 KiB for the kernel. | Compare the load addresses and entry points in the linked ELF with the boot trace. A callable ROM routine has an independent ABI and startup-order requirement. |
| Kernel to SD root | The resident SD transport reads sector zero, bounds the candidate extents and selects the first type-b7 root. Raw swap starts at 0x101a0000. | A missing root takes the configured failure path. Check the card identity and the flash boundary in the configured Renode case. |
| Filesystem to process | A raw or packed executable occupies a 192 KiB window together with its data, BSS and stack. Packed exec validates its streams before replacing the old image. A read failure after commitment terminates the process through the exec cleanup path. | Inject failure separately on each side of exec_estab. Check inode references, exclusion and runnable process state. |
| Process to swap and back | The swapper excludes the clean text prefix when it retains the executable inode. Mutable data, stack and user area require preservation. A packed text restore and a SwapRAM decode can share a workspace under the existing owner protocol. | Count restore reads, decoder acquisitions and peak simultaneously live storage. A blocked inode read permits other activity while the workspace remains owned. |
| Graphics command to visible pixels | The application owns its drawing policy. The driver owns the panel transfer and restoration of the text console. Panel GRAM retains pixels while the device state persists. | Compare the visible pixel result and kill the foreground process during a transfer. A redraw after controller reset needs a surviving source of pixels. |
| Card or PSRAM loss | The card holds persistent files. PIO-attached PSRAM serves explicit transfers and loses contents with power. Scratch RAM, USB endpoint memory and panel GRAM each have distinct consumers and access rules. | Trace the complete return or failure path with the affected device absent. Record independent storage for any state needed during that path. |

Source owners are `sys/arch/rp2040/conf/RP2040-sd.ld`, `dev/sd.c`, `rp2040/exec_hsaout.c`, `rp2040/swapram.c` and `dev/st7365p.c` under the RP2040 architecture, plus `sys/kern/vm_swap.c`. The map also assigns 1,504 KiB to filesystem flash, 58 KiB to ordinary kernel SRAM, 3 KiB to each user area and 8 KiB to scratch. A change to the boot medium alone releases none of those allocations.

## Measurements and falsifiers

* **Identity in the install tree.** A clean RP2040 build followed by construction of the PICOCALC SD root at source revision 0600b89cd (with staged configuration write checks) contains 154 regular-file hard-link groups. Group the regular paths under the installed root by the pair of lstat device and inode numbers. Remove paths named by the composed base manifest. Of the original groups, 152 still contain multiple extra names. Keeping one extra file per such group would replace 374 additional file entries representing 1,599,796 logical bytes. One group spans both manifest classes. These are counts from the source tree. Sparse blocks, directory entries, link-count limits and per-name image policy determine the allocation in the target image. An image-level comparison must check inode identity as well as free blocks.
* **Packing in the SD profile.** The SD manifest inherits flash-profile packing. The stream inspector counts two passes at exec and one text-only pass per clean-text restoration. For executable i, measure the packed-minus-raw time per successful exec and separately per text restore. Multiply each paired difference by that event's workload count. Measure the combined workload to detect an interaction from contention or cache state. The bare-board seq measurements in rp2040-compiler-debug-optimization.md are controls for experimental design. They supply no SD timing coefficient.
* **Shared SRAM.** Applet-private BSS has disjoint lifetimes because the dispatcher selects one applet per process. Codec workspace sharing requires ownership across calls that may sleep. An interference edge connects two objects whenever a reachable schedule keeps both live. Verify address alignment, interrupted execution and core-1 activity before assigning a shared extent. Read the final map and measure the peak stack separately.
* **ROM substitution.** The historical forward-copy veneer saved 128 PICOCALC text bytes and cost eight permanent BSS bytes. Its overlapping pathname move fails the memmove contract. Repeat the paired link with the overlap-correct implementation, earliest boot caller and XIP-off path before accepting any saving. The rounded image size in the original PICOCALC experiment stayed constant.
* **Cold services.** Keep partition admission, root discovery and swap recovery resident. Test the read-only partition report against the accepted extents available through DIOCGETPART. Compare the old boot observation and the proposed user command with an unreadable card and a mounted root. Source-side deletion earns a kernel-size claim only if the final linked span shrinks after all interface costs.
* **Code overlays.** The 2.11BSD executable header admits 15 overlays, backed by hardware segmentation on that target. On RP2040, a suspended return address and an interrupt may both require resident native instructions. The existing LZMA experiment already exceeds its flash target after pinning three USB functions in the 7,040-byte page case. An alternative must demonstrate safe returns and a smaller combined decoder, metadata, flash and peak-SRAM footprint.

## Donor contracts and edition boundaries

The [source inventory](sd-placement-sources.tsv) records the SHA-256, PDF extent and search terms for 23 local references. Page columns are one-based PDF indices found by pdftotext after whitespace and case normalization. Early matches include contents pages. A missing text match records extraction coverage.

* Thompson's swapping account and Lions describe which process state must survive loss of core. Bach and the 4.3BSD and 4.4BSD books provide comparison points for buffer ownership and scheduling. The present swap_image_span_geometry rounds the dirty data, stack and user area separately. Compare the implementation at each rounding boundary before borrowing a historical space estimate.
* Ritchie's buffer handoff on PDF pages 6 and 7 makes completion ownership explicit. The 4.4BSD architecture manual describes descriptor operations. NetBSD's v7fs user I/O backend at 1e8e727e461b separates sector access from filesystem interpretation. The fsck paper's duplicate-block and inode checks on pages 6 and 12 motivate an independent image reader. The xv6 commit-point discussion on page 90 supplies a way to locate the irrevocable transition in exec and image publication.
* The BSD config and PMake texts separate configuration selection from dependency execution. Compare Config directives, generated Makefiles, final symbols and installed aliases. The BusyBox reference lists command behavior. For an applet's actual storage lifetime, inspect DiscoBSD's dispatcher and linker script.
* Arnold and Amir's curscr distinguishes the displayed state from the desired screen. Haerr's screen-driver interface separates drawing from physical blitting. Evaluate dirty rectangles against the ST7365P stream and the driver's console recovery cost. Device state, process state and reconstructible assets have different lifetimes.
* K&R describes the historical C idiom. Spinellis measures changes across UNIX source history. N2683 distinguishes overflow, truncation and sign interpretation. Use those distinctions for C17 admission of sector spans and a.out lengths. N3096 and the C23 text describe later facilities. Select a C17-compatible implementation and verify the native compiler separately. Nelson and the ARM7TDMI manuals supply embedded and Thumb context. The RP2040 datasheet and ARMv6-M contract own target exception and peripheral behavior.
* Issue 8 owns the selected utility and interface requirements. Compare the exact clause, installed name, target implementation and test separately. The literal-tab cut fixture addresses one list-separator rule. Existing shell and recovery-utility witnesses own their exercised command, getopts and uudecode facets.

## Terminology coverage

The supplied technical terminology index has SHA-256
`0869e983b96ff3ae8bae55254e0ba16d474874472a43cfbddb872d26ada60f09`.
Matching numbered rows within its 26 domain headings gives 1,358 labels.
NFKC normalization, case folding and joining whitespace leave 1,358 keys.
The footer attributes two repeated uses to the inventories from which the
author reconstructed the index. The numbered rows are the reproducible
denominator for this edition.

| Domains | Applicable method | Condition or limit |
| --- | --- | --- |
| 1, 2, 23: architecture, compilation and source operations | Trace both the direct caller and the generated selection that admits its object. | A linked interrupt entry, function pointer or return edge can invalidate a source-only dependency cut. |
| 3, 6, 12, 26: invariants, ABI, C and standards | Relate byte ranges, type conversions and externally visible behavior to the target-width representation. | An implementation test covers its exercised values. A proof requires the actual conversion and overflow rules. |
| 4, 5: optimization and linking | Compare paired ELF spans and inspect Thumb code, literal pools and alignment. | A smaller object can have zero effect after garbage collection or padding. |
| 7, 9: ownership and concurrency | Derive simultaneous lifetimes from interrupt, scheduling and cleanup paths. | Exclusive use observed in a single trace cannot justify an overlap across every schedule. |
| 8, 10: peripherals, performance and recovery | Identify the bus owner, maximum transfer, completion signal and recovery medium. | PSRAM throughput, XIP-off execution and SD removal each need their own deciding measurement. |
| 11, 16, 17, 24: measurement and inference | Use paired trials, alternate order and record competing mechanisms for latency changes. | Repeated runs under one cache or card state share a source of variation. |
| 13: numerical analysis | Measure rounding and exceptional inputs at an actual ROM math substitution. | Floating-point accuracy metrics add no information to inode identity. |
| 14, 15: algorithms and mathematics | Use reachability for recovery dependencies and a weighted interference graph for workspace placement. | An optimal graph coloring still needs bank and alignment checks. Advanced mathematical analogies require a concrete map to program states. |
| 18: quantum computing | The SD workload has no quantum measurement or channel in its execution path. | General experimental design transfers under domains 11 and 17. A quantum-specific result needs a separate target. |
| 19 through 22, 25: component placement, overlays and prospective architecture | Rank a user executable, ROM routine, pinned kernel function and overlay by availability, resource cost and failure closure. | A privileged module needs an enforceable interface and an interrupt-safe return mechanism. The current user-mode path already supplies credentials, signals and exit cleanup. |

## Integration order

1. Preserve explicit manifest metadata and executable representation while reconstructing compatible hard links in the SD additions. Check names, inode numbers, reference counts, file bytes and allocation against the final image.
2. Exercise raw and packed binaries through fresh exec and a forced text restore. Record card bytes, blocks, timing and failures with the same input and kernel. Keep the smaller and faster alternatives visible where neither dominates.
3. Measure the complete kernel and user cost of the partition report, filesystem inspection and display presentation separately. Preserve boot diagnostics that remain necessary before a user process can run.
4. Modernize one shared representation at a time. Pair a strict C17 compile with an independent format observer, target-width arithmetic tests and cross-linked size and stack measurements.
5. Revisit a ROM veneer, a new lifetime overlay or an executable overlay when its admission and recovery contract has a witness. The existing memory map, measured peak and negative controls form the baseline.

[Kernel flash budget](picocalc-kernel-flash-budget.md) · [ROM copy experiment](noreturn-and-rom-copy-footprint.md) · [Filesystem components](tiny-rump-components.md) · [C17 migration](c17-semantic-migration-plan.md)

## Raw execution and clean-text restoration on the SD profile

A fresh packed exec validates and then loads both streams. A clean-text
restore reads only the text stream. For E successful execs and R successful
clean-text restores, body input is (2E + R) times packed text plus 2E times
packed data. Raw input is (E + R) times text plus E times data.
The two multipliers follow the calls in exec_hsaout.c and exec_aout.c.
The input stage occupies 480 bytes on ARMv6-M, measured by compiling an
array of HSX_STAGE bytes and reading its ELF symbol size.

| Installed program | Raw file bytes | Packed file bytes | Raw body input, E=R=1 | Packed body input, E=R=1 |
| --- | ---: | ---: | ---: | ---: |
| /bin/sh | 37,348 | 31,788 | 72,200 | 93,767 |
| /sbin/fsck | 27,488 | 23,217 | 54,500 | 69,232 |
| /usr/bin/cal | 5,996 | 5,181 | 11,640 | 15,187 |
| /usr/bin/textbox | 38,936 | 31,762 | 77,524 | 94,945 |

Inputs come from the RP2040 installation at 9a87d032a, compressed with
its hsaout. Reproduce each row with inspect_packed_exec_work.py using
the raw and packed files, the matching decoder, and
`--stage-bytes 480 --execs 1 --restores 1`. Each comparison checks the
decoded program body against the raw executable. Every pair trades fewer
stored bytes for more modeled input at this workload. Filesystem cache hits,
allocated blocks, SD transfers, scheduler delays and elapsed time require
separate measurements before changing the installed representation.

## Linked ownership on PICOCALC_SD

The ARMv6-M ELF for 345265caa and its parent b71a2ecc0 use the same
GCC 16.2 build directory and linker script. Bloaty section differences
and ARM objdump both show 56 fewer text bytes. The initialized flash
span remains 128,904 bytes. Candidate text ends at 0x1001f39c and the
board record starts at 0x1001f400. With the remaining sections fixed,
another 156 text bytes reach the preceding 256-byte alignment boundary.
The 34,680 bytes between the initialized span and the link-region limit
already include the 1,024-byte acceptance margin.

| Resource | Linked ownership or hardware role | Derivation and placement constraint |
| --- | --- | --- |
| Mask ROM and external QSPI | ROM enters boot2. The first 256 flash bytes hold boot2. The kernel executes through XIP. | Resolve callable ROM services by their table ABI. Earliest startup, overlap semantics and execution during a flash operation decide a copy substitution. |
| XIP cache and SRAM aliases | The 16 KiB cache holds fetched flash lines. The bank aliases expose the same SRAM0-3 cells. | Counting an alias twice invents capacity. Repurposing cache storage changes execution latency and MPU coverage. See BOOT-MAP.md and MPU.md. |
| Filesystem and swap flash | The SD map assigns 1,504 KiB at 0x10028000 to filesystem flash and 384 KiB at 0x101a0000 to raw swap. | Root selection and flash ownership have separate contracts. The foreign-flash boot gate samples seven words after SD root selection. |
| User RAM and kernel RAM | The flat process window occupies 192 KiB. Inside the 58 KiB kernel region, initialized data occupies 144 bytes, SRAM-resident code 1,000 bytes and the SwapRAM pool 16,384 bytes. | The flash writer and PSRAM transfer routines need executable SRAM. The pool stores compressed state. The two 3 KiB user areas beyond kernel RAM hold process-zero and current-process state. |
| SRAM4 and SRAM5 | The USB transmit ring occupies 4,096 bytes at 0x20040000. The tile atlas occupies the next 4,096 at 0x20041000. | Both owners appear in ARM nm. Console output and graphics can overlap. An additional buffer requires a demonstrated lifetime exclusion or a different owner. |
| USB dual-port SRAM | Endpoint hardware owns the low area. The Dhara page, shared codec workspace and flash program page occupy 1,024, 1,040 and 256 bytes in the tail. | The two unassigned intervals contain 256 and 944 bytes. A contiguous 1,024-byte object fits neither. This region is execute-never. The callable boot2 copy remains in ordinary SRAM. |
| PIO-attached PSRAM | The board provides 8 MiB behind PIO0. The default split assigns 2 MiB to swap and 6 MiB to temporary files. A usable SD temporary partition allows swap to occupy the full part. | Access uses bounded transfers. Executables and return addresses require resident, CPU-addressable memory. Loss of PSRAM has a different recovery contract from loss of clean executable text. |
| SD card | The filesystem owns persistent executables, manuals and assets. A clean-text restore reads the same executable inode. | The hard-link image comparison frees 374 inodes and 1,908 KiB on a fixed-size root. Aliases share one inode only when the installation supplies that identity. |
| Display and peripheral storage | Panel GRAM holds visible pixels. PIO instruction memory holds transfer programs. Device FIFOs hold pending transfers. Watchdog scratch registers carry reset state. | Display reset, FIFO consumption and reset-time recovery each end a different lifetime. The peripheral driver owns the transition. None of these capacities enters the user process window. |

The partition reader shows how lifetime changes can help before an alignment
boundary moves. Pahole reports four entries occupying bytes 0-63, a
two-byte signature at 64, and two trailing alignment bytes. The transport
consumes the sector for CRC and copies 66 bytes to an aligned local object.
An error discards that object and preserves the accepted mappings. The
normal read no longer borrows a filesystem cache block. Host mutations
exercise failed reinitialization and table publication. Configured Renode
mounts the root, reads and writes temporary storage, and exercises the
missing-root panic. These checks leave physical latency and PSRAM timing open.

Reproduce the binary observations with `arm-none-eabi-objdump -h`,
`arm-none-eabi-nm -S -n`, `pahole -C sd_mbr`, and
`bloaty -d sections CANDIDATE -- PARENT` on the two unstripped kernels.
The executable names, linker addresses and member offsets are independently
readable from the ELF, while all four tools share that artifact as evidence.
