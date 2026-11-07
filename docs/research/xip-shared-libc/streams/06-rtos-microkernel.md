# Stream: RTOS and microkernel memory techniques

Retrieved 2026-11-07 through `gh api` and the web. GitHub sources are pinned to
the default-branch commits read that day: apache/nuttx
`b2b181fbd99ad63dc83b24edcf921d826796b8db`, zephyrproject-rtos/zephyr
`0c63b6bfeb0d5dc9f56a156be0248a61ab2bb6bc`, tock/tock
`26893d05e38075034c98f182dfd1091e1bfce922`, f9micro/f9-kernel
`c0471dcb0be9881a5ee6cbb4b2d9fa25ef59994e`, FreeRTOS/FreeRTOS-Kernel
`ca759444e9ca59cfa3a9faa6cb8b49e0772ca520`. The DiscoBSD tree is at
`37cef7bbd30df8a8ee24bcb35d2a057c7c60b98b`. Src = read from the cited source;
Inf = inference. Rows marked "search summary" rest on a web-search digest of the
cited page and not on the page text; treat them as leads. Not retrieved, so
unverified: RIOT, Mynewt, ChibiOS, Embox, L4Ka::Pistachio and Hazelnut, classic
Mac OS A5 world, uClinux (covered by another stream).

| System | Mechanism | Citation | Applicability to DiscoBSD |
| --- | --- | --- | --- |
| NuttX NXFLAT | Src: PIC modules in ROMFS or XIPFS run in place; only .data copies to RAM. Current ABI keeps the PIC base in r9 (`-msingle-pic-base -mpic-register=r9`); r10 is the retired v1 ABI. Imports bind at load from a base-image export table. The module header has no version field, so ABI generation is declared by importing the symbol `__nxflat_abi_v2`, which libc defines and the loader requires (ENOEXEC when missing, ENOENT when a newer marker is absent from the firmware). No shared libraries; toolchain Cortex-M3/M4 only; callbacks and static function pointers restricted. | apache/nuttx `Documentation/os/binfmt/nxflat.rst` (Module ABI marker section), `binfmt/libnxflat/`, `include/nxflat.h` | Partial. Link-time `--just-symbols` binding avoids load-time import strings. The marker-symbol check is a direct precedent for the libc version gate in technique 1. |
| NuttX protected build | Src: privileged and unprivileged blobs; `struct userspace_s` at the fixed `CONFIG_NUTTX_USERSPACE` holds text/data/bss bounds, heap end, entry and signal trampolines; SVC call gate; split heaps; `mpu_user_flash()` programs user regions. All apps and libc link into one user blob. | `include/nuttx/userspace.h`; `Documentation/os/build_modes.rst`; `arch/arm/src/stm32u5/stm32_mpuinit.c` | Applies as a pattern: a fixed header at the libc base carrying version, layout and trampolines that exec validates. |
| NuttX CROMFS | Src: a memory-addressed, read-only filesystem in NOR flash; file data is LZF-compressed and decompressed on read, metadata is uncompressed. It establishes compressed storage in addressable flash and no direct execution of compressed instructions. | `Documentation/os/filesystem/cromfs.rst` | Not applicable; heatshrink on Dhara already compresses. |
| Zephyr userspace | Src: MPU as identity page table; memory domains and partitions per thread; `CONFIG_HW_STACK_PROTECTION` guard region before the supervisor stack; stack canaries; every syscall has a `_vrfy` handler treating user pointers as untrusted; 256-byte minimum MPU alignment on ARMv6-M. | `doc/kernel/usermode/*.rst`; `arch/arm/core/mpu/Kconfig` | Partial: guard region, canaries, verifier discipline. |
| Zephyr LLEXT | Src: four loader behaviors. (1) Default: copy every section into allocated RAM and apply relocations. (2) `LLEXT_STORAGE_WRITABLE` or persistent storage: a section with no `SHF_WRITE` and no relocations maps in place from the ELF buffer when `peek()` is available, aligned, and (for text) in instruction memory. (3) `relocate_local`: relocations against local symbols are applied. (4) `pre_located`: the ELF's own virtual addresses are used, the application places the extension (for example by linker script), and the core allocates and copies nothing. Case (4) is the fixed-address model closest to the libc design. | zephyr `subsys/llext/llext_mem.c` lines 128-178; `include/zephyr/llext/llext.h` lines 195-220; `doc/services/llext/config.rst` | Partial. Persistent reuse and pre-location show a relocation-free image can run from storage. The relocating loader (case 1) is the comparator for an exec-time relocation design and has not been measured against the fixed-address libc here; see the review resolution. |
| Tock | Src (search summary for TickTock): per-process MPU; one power-of-two RAM block with the kernel-owned grant region at its top; subregion masks; Cortex-M0+ is `MPU<8, 256>` (Src, `arch/cortex-m0p/src/lib.rs`); TickTock (SOSP 2025) found subregion rounding exposed grant memory to the process. | tock `arch/cortex-m0p/src/lib.rs`, `kernel/src/process_standard.rs`; ranjitjhala.github.io/static/sosp25-ticktock.pdf | Partial: audit the subregion edges of our map. |
| F9 (L4 on Cortex-M) | Src: flex pages become power-of-two MPU regions, unaligned spans split; stack and PC regions always mapped, others loaded lazily on MemManage fault using MMFAR. | f9micro/f9-kernel `Documentation/memory.md`, `platform/mpu.c` | Partial: splitting reusable; lazy loading is not available on ARMv6-M, which has no MemManage fault and no MMFAR (Inf, from the ARMv6-M architecture and the repository's MPU notes; not re-read from the ARM manual in this pass). |
| seL4 | Src: FAQ says an MMU-less microcontroller makes little sense for seL4. | sel4.systems/About/FAQ.html | Not applicable. |
| OKL4 / L4 ARM FASS | Src (search summary): ARM domains plus FCSE PID on ARMv4/v5. | UNSW thesis "High-performance microkernels and virtualisation on ARM" (unsworks.unsw.edu.au) | Not applicable: ARMv6-M has neither. |
| FreeRTOS-MPU | Src: kernel in a `privileged_functions` region; MPU wrappers raise privilege through an SVC (`portSVC_RAISE_PRIVILEGE` in `portable/GCC/ARM_CM4_MPU/port.c`); per-task stack plus 3 regions reprogrammed on switch. Src (search summary): raise/reset made macros (V10.4.6) and function-pointer arguments closed (V10.5.0) after escalation findings. | FreeRTOS-Kernel `portable/GCC/ARM_CM4_MPU/port.c`; FreeRTOS release history; NDSS BAR 2023 paper 23007, section IV-A | Partial. SVC entry gives no immunity: the FreeRTOS hole raised privilege through SVC. DiscoBSD's entry, dispatch and return path needs its own audit (open item in technique 7). |
| Linux a.out DLL | Src (search summary plus HOWTO text): fixed load addresses registered centrally; `libfoo.sa` link stub; 8-byte jump-table slots at fixed positions; `jump.vars` reserves slots; minor newer accepted, older warns, major mismatch fails; reserved holes enlarge images. | linuxjournal.rubdos.be/ljarchive/LJ/012/1052.html; progettoatena.it/manuali/HOWTO/GCC-HOWTO-6.html | Applies: closest precedent for versioning a fixed-address libc. |
| Amiga exec | Src (search summary): library base in A6; 6-byte jump vectors at negative offsets; only address 4 fixed; `SetFunction` patches vectors; ROM-resident libraries via RomTag; per-library state at positive offsets allocated at open. | wiki.amigaos.net/wiki/Exec_Libraries | Applies as a pattern: table-resolved entry points; state allocated at open. See technique 2 for the ABI constraint. |
| PalmOS | Src (search summary): trap dispatcher indexes a per-library dispatch table; globals allocated at install. | drdobbs.com/database/extending-the-palmos/184411100 | Partial. |
| RISC OS modules | Src (search summary): header of entry offsets; private workspace word in R12; ROM modules. | riscos.com/support/developers/prm/modules.html | Partial: per-instance workspace. |
| Mach and Lites | Src (search summary): Unix server plus an emulation library mapped into every task; syscalls redirect into it. | os.inf.tu-dresden.de/~hohmuth/prj/lites-on-l3/beleg/main-2.html | Not applicable: needs an MMU and a server. |

## Ranked techniques for DiscoBSD, each with its falsifier

The ranking is a list of candidates. No technique here has been shown to beat
the current multicall linking or an exec-time relocating loader. A matched
Cortex-M0+ experiment must compare peak SRAM, final flash blocks, exported-data
ABI, pointer permissions, loader footprint, swap-in cost and runtime latency.

1. Versioned header at the libc base, optionally with a jump table: major and
   minor version, slot count; exec refuses a major mismatch (NuttX does the
   same through `__nxflat_abi_v2`). Falsifier: kernel, libc and every program
   always ship in one image, so the check never fires; then keep a single hash
   word and drop the table.
2. Lazy libc state in place of the fixed reservation. The reservation is
   configuration dependent: libc RAM ends at 0x2000152e (5,422 bytes, programs
   linked at 0x20001600, 5,632 bytes rounded) for the union of archive members
   the 27 programs use, and at 0x200016ea (5,866 bytes, programs at
   0x20001800, 6,144 bytes rounded) for the whole archive (measure results
   `relink-results-union.txt`, sha256 prefix `8b38f48e7e95`, and
   `relink-results-whole.txt`, prefix `fbb79313ffb8`). Lazy allocation needs an
   ABI first: `include/stdio.h` exposes `_iob` addresses, `SYS.h` stores through
   the absolute `errno`, and `lib/startup-arm/crt0.c` owns `environ`; the MPU
   cannot demand-map those addresses. Specify fixed slots for the exposed
   symbols, accessors, or a state-base register before measuring. Falsifier:
   after that ABI exists, measure peak RAM, initialization, fork, signal and
   swap cost per program; the rework fails if the measured peak-RAM saving per
   program does not exceed its added initialization and accessor cost. The
   earlier 1 KB threshold had no support and is withdrawn.
3. Execute-in-place text for a few hot programs (NuttX ROMFS and NXFLAT idea;
   Zephyr persistent-section reuse). Requires text linked at its flash address
   and a user RX flash MPU region. Every process occupies the one 144 KB window
   at a fixed address (`sys/arch/rp2040/rp2040/mpu.c`), so smaller resident text
   cannot create a second resident process. The measurable gains are lower peak
   process RAM and lower swap-in traffic, because `swapin` in
   `sys/kern/vm_swap.c` reloads executable text through `exec_text_restore`.
   Falsifier: measure flash blocks, peak process RAM, swap-in bytes and latency
   separately; the technique fails if flash cost exceeds the RAM and swap
   savings, or if XIP-cache misses cost more than decompression.
4. Stack guard (256-byte MPU region or canary) and per-program stack budgets
   from `-fstack-usage`. Falsifier: no spare region after other uses; canary
   growth exceeds the safety gained; call graph unresolvable through function
   pointers.
5. MPU edge audit for subregion rounding (TickTock bug class). Falsifier:
   mputest probes the first byte past and last byte inside each span; full
   agreement means nothing to gain.
6. Shared rodata and string pooling across multicall images. Flash-resident
   strings are outside the user window: `baduaddr_span` in
   `sys/arch/rp2040/rp2040/machdep.c` accepts only the SRAM window, and
   `copyin`, `copyout` and the `write(2)` path in `sys/kern/sys_generic.c` use
   that predicate, so a buffer in flash fails ordinary syscalls. The design
   needs a user RX flash MPU region plus separate readable-user and
   writable-user span checks. Heatshrink does not pool across images:
   `tools/hsaout/libhsaout.c` resets the encoder and encodes text and data
   separately per executable, so no compression history crosses an image.
   Falsifier: compare aggregate packed blocks of the 27 images against the
   shared pool plus references, veneers and metadata; pooling fails if the net
   gain is within one block.
7. Syscall argument verification in Zephyr `_vrfy` style; the M0+ kernel runs
   privileged with PRIVDEFENA so checks are software. Open audit: the complete
   SVC entry, dispatch and return path (`sys/arch/rp2040/rp2040/locore0.S`,
   `syscall.c`) is not yet reviewed against the FreeRTOS privilege-raise class.
   Falsifier: fuzz an enumerated denominator of syscall, pointer argument,
   span and direction (read versus write) with kernel, flash and wrapping
   addresses; any success disproves coverage, and zero successes bounds only
   the enumerated cases.
8. Per-process kernel data in the swapped image (Tock grants). The u-area
   already swaps: `swapout` saves `USIZE` bytes to SwapRAM or flash and
   `swapin` restores them. Falsifier: list the kernel structures not yet in
   the u-area and measure the incremental bytes and copy time; the technique
   fails if the increment is zero.

Not worth pursuing on current evidence: lazy MPU loading on fault, FCSE and ARM
domains, seL4, paging. An exec-time relocating loader (Zephyr LLEXT case 1,
NXFLAT import tables) is an unmeasured comparator, not a rejected one.

## Resolution of the adversarial review

1. ACCEPT. `llext_mem.c` lines 128-178 reuse read-only, relocation-free sections from persistent or writable storage, and `llext.h` lines 195-220 define `pre_located`; the LLEXT row now separates copy, relocation, persistent reuse and prelocation, and the relocating-loader comparison is marked unmeasured.
2. ACCEPT. `baduaddr_span` in `machdep.c` tests only `__user_data_start` to `USER_TOP`, `copyin` and `copyout` call it, and `sys_generic.c` applies it to every iovec; technique 6 now budgets a user RX flash region and split span checks.
3. ACCEPT. `stdio.h` exposes `_iob`, `SYS.h` stores to `errno` by absolute address, `crt0.c` defines `environ`; technique 2 requires an ABI and drops the unsupported 1 KB threshold.
4. ACCEPT. `mpu.c` documents one fixed 144 KB window and `swapin` calls `exec_text_restore`; technique 3 falsifier now splits RAM, swap traffic and flash cost.
5. ACCEPT. `libhsaout.c` calls `heatshrink_encoder_reset` per `encode` and encodes text and data separately; technique 6 falsifier now compares aggregate blocks.
6. ACCEPT. `port.c` in `ARM_CM4_MPU` contains `portSVC_RAISE_PRIVILEGE`, so SVC supplies no immunity; the FreeRTOS row drops the claim, and technique 7 lists the DiscoBSD path audit as open and bounds the fuzz claim.
7. ACCEPT. The pinned `nxflat.rst` states the r9 PIC base and describes r10 as v1; the NXFLAT row now names r9 and `__nxflat_abi_v2`, verified in `include/nxflat.h`.
8. ACCEPT. `cromfs.rst` lines 6-18 describe a memory-addressed filesystem with LZF-decompressed data; the CROMFS row now says so.
9. ACCEPT. `vm_swap.c` swaps `USIZE` of the u-area out and in; technique 8 states the baseline and asks for the incremental structures.
10. ACCEPT. Source commits are now pinned; rows built on search digests are marked "search summary". The result files hash to prefixes `8b38f48e7e95` and `fbb79313ffb8`, and their headers give 0x2000152e with programs at 0x20001600 (5,632 bytes) and 0x200016ea with programs at 0x20001800 (6,144 bytes), so "5.4 KB" names the union configuration only. Input hashes and the command line live in the measurement stream and are not repeated here.
