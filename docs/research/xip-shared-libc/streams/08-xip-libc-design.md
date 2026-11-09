# Stream: in-tree design of a fixed-address execute-in-place libc

Source-and-docs reading only; no builds or probes ran. Sizes outside the
prototype in 01-local-measurement.md are estimates.

The variable-window assumptions below belong to the retained prototype.
`../../swap-exhaustion/review-04.md` supersedes the live layout with a fixed
192 KiB window and a kernel-resident SwapRAM pool.

## Corrections to the prototype
- The prototype's 0x10100000 lies inside the Dhara root (0x10020000 to
  0x101A0000, `sys/arch/rp2040/conf/RP2040.ld:32`); the in-tree libc needs its
  own partition.
- The kernel region holds 130,816 bytes after boot2 (`RP2040.ld:23`) against
  108,726 used; libc (33.5 KB with .data init) does not fit the 22 KB left.
- `STORAGE.md:65` records that in-place userland needs a new format, a
  validating loader and a swapper that never restores flash text; this design
  supplies all three.
- Place libc RAM at the window base: `user_top()` moves by SWAPRAM_BONUS under
  P_LARGE (`swapram.c:730`), and `exec_setupstack` parks the argv pointer in
  the last window word (`exec_subr.c:278`). The ROM float resolver already
  requires its cache cell inside `[USER_DATA_START, USER_DATA_END)`.
- Kernel-visible marker: a new a.out flag `EX_XIPLIBC` (0x02) in the 6-bit flag
  field (`exec_aout.h:60-69`); `aout_layout_check` rejects nonzero flags today
  (`exec_aout.h:127`); equality tests on `EX_HSPACK` become mask tests
  (`exec_aout.c:99`, `exec_hsaout.c:107`, `exec_hsaout.h:78`, `libhsaout.c:138`).

## Flash
- New raw partition at 0x10020000, 48 KB (six 8 KB Dhara erase blocks); the
  Dhara region moves to 0x1002C000, 1488 KB. Edit `FLASH_FS_OFFSET`
  (`flash.h:81`) and `RP2040.ld:32`; add `FLASH_LIBC_OFFSET`/`BYTES`.
- Dhara cost: 28 KB logical (989 to 961 KB per `flashimg -c` and the
  formulas in `dhara/journal.c:475` and `dhara/map.c:95`), against 214 packed
  blocks saved on the 28-executable union row; net 186, provisional until the
  float-hook and libgcc policy is measured.
- Rejected: appending to the kernel image (exceeds 128 KB; kernel UF2 range
  pinned in `check_rp2040_uf2.py:25`); taking from swap (384 KB already tight
  per `flash.h`).
- libc rides in the root UF2 (concatenate a padded partition image before
  `picotool uf2 convert`, `Makefile.inc:136`), so programs and libc flash
  together; `FS_KBYTES` follows the new `flashimg -c` value.
- Matching: a header at partition offset 0 with magic, `abi_hash` (32 bits of
  SHA-256 over sorted exports plus RAM layout), RAM size, .data load address
  and size, .bss size, syscall-table hash. Kernel compares at boot; elf2aout
  (or the board ld) stores `abi_hash` in `a_reltext`; the packed container
  carries it in `x_reserved` (now required zero by `hsx_check`).

## RAM, crt0, exec, swap
- `crt0x`: `_start` calls `__libc_init(env, progname, end)` and copies libc
  .data; the kernel's `exec_clear` zeroes bss. libc owns `environ`,
  `__progname`, and two break cells, `__curbrk` and `__minbrk`, both zero bss
  words set by crt0. `sbrk()` (`sbrk.c:9`) and `brk()` (which clamps against
  `_end` at `sbrk.c:44`) use them, so libc never references `_end`; a test
  links programs with different image ends.
- Kernel (fixed-data design only): both `exec_aout_check` and the packed
  loader (`exec_hsaout.c:127,168-201`, which validates and expands against
  `__user_data_start`) pass `base + L` and `WINDOW_MAX - L` to the layout check
  and place data at `base + L`; `p_dsize` and `u_dsize` (`exec_subr.c:475,814`)
  both become `L + text + data + bss`, because `brk()` zeroes from
  `p_daddr + u_dsize` (`kern_mman.c:54-58`); `exec_estab` adds L to `need`;
  `P_XIPLIBC` is inherited at fork (`kern_fork.c:154-161`); `exec_clear` zeroes
  `[base, base + L)`; `XIPLIBC_RAM` in machparam.h is checked against the
  header; tests cover break growth and shrink, fork, swap and SwapRAM round
  trips, and packed and raw reserved-RAM canaries.
- Swap is the invasive part: `swaptext` (`vm_swap.c:27`) assumes clean text at
  the window base; with libc RAM below text, swapout writes two dirty extents
  and swapin restores text at base + L. L must be a multiple of 512. SwapRAM
  calls (`vm_swap.c:66,193`) need a two-extent form.
- Every process pays the full libc RAM. Shrink by allocating on first use:
  `gr_mem` (getgrent.c:12, 800), crypt `KS` (768), regex `expbuf` (512),
  256-byte `line` buffers (getgrent.c:10, getpwent.c:34); make `_ctype_`
  const. Estimated residue about 2.4 KB.

## MPU
- Region 4: base 0x10020000, read-only unprivileged, executable, SIZE 15 (64
  KB) with subregions 6 and 7 disabled so only the 48 KB partition is
  reachable. Update `MPU_NMAP`, the identify banner, MPU.md and mputest; new
  probes: a write to libc flash faults, a read inside succeeds, a read of the
  Dhara start faults.

## Linking
- `lib/libcx`: link script, header, exported symbols as an ELF symbols file
  and as an a.out object of N_EXT|N_ABS symbols for the board ld. The board ld
  stores N_ABS symbols (`ld.c:850,1295`) but converts a call against one to
  RABS and rejects Thumb calls beyond +/-(1<<22) with "Thumb BL out of range"
  and no veneer (`ld.c:592-645`), so on-board linking needs callable import
  stubs or ld veneers plus an absolute-call fixture; until then board-built
  programs keep static `libc.a`.
- Program link: an `XIPLIBC=yes` variant in sys.mk using an elf32-arm.ld copy
  based at 0x20000000 + L, `crt0x.o`, `--just-symbols`, then `-lc` as a
  fallback that must stay empty. Multicall BSS overlay unchanged.
- Member set: union of shipped programs plus the board libc member list plus
  an explicit allow-list, with a size gate at the partition size.
- Weak hooks: `_doprnt`'s weak `__doprnt_cvt`, `__doprnt_ll`, `__doscan_cvt`
  (sys.mk:107-137) resolve inside libcx, so float printf must live in libcx
  and `PRINTF_FLOAT` becomes inert for XIP programs.
- Interposition does not fail closed: under `--just-symbols` a program global
  that libc also exports binds silently to the libc address (stream 01:
  sh `rename` and `tmpnam`, ps `uname`, init `shutdown`). Repair each with
  `static` and keep the export-intersection gate calibrated on bin/sh.
- Keep libgcc leaf helpers (`__gnu_thumb1_case_*`, `__aeabi_*div*`) private
  to each program to avoid a veneer and an XIP fetch on every switch and
  divide.
- Veneers: one Thumb-1 long-branch stub per imported function per program;
  count `*_veneer` symbols as a gate figure.
- ROM float resolver code moves to libc flash; its cache cells land in libc
  RAM; lazy resolution per exec works as today.

## Native toolchain and old binaries
- Unflagged static a.outs keep loading at 0x20000000 and use the whole window.
- On-board `cc` keeps libc.a by default; an opt-in `cc -xip` would link
  `crt0x.o` plus the N_ABS import object at `-T 0x20001600` and set the flag.
  Unproven; probe with an a.out ld fixture first.

## Gates and falsifiers
- Changed: check-multicall-composition (TESTING.md:518), check-hsaout,
  check-elf2aout, check-swapram, check-uf2 and flash bounds, mputest,
  check-renode banner.
- New: ABI hash agreement and mutated-hash rejection; member closure; no
  libc.a members left; export-collision; size budget; exec_hsaout flag 0x03;
  board swap round-trip of libc state; mputest residue still zero.
- Falsifiers: XIP cache thrash (coremark and a stdio microbench before and
  after; more than a few percent regression on stdio-heavy programs refutes);
  swap growth per context switch; exec latency; veneer cost on hot paths.

## Ordered commits
1. Flash map moves Dhara to 0x1002C000 with an empty libc partition.
2. `EX_XIPLIBC` flag, `XIPLIBC_RAM`, exec and hsaout mask checks with host
   fixtures, no producer.
3. Two-extent swap and SwapRAM support with a host test.
4. MPU region 4, MPU.md, mputest.
5. libc: `__libc_init`, libc-owned environ, progname and break, lazy buffers,
   const `_ctype_`.
6. `lib/libcx`: link script, header, exports, ABI hash, member policy, size gate.
7. Build wiring: sys.mk `XIPLIBC`, crt0x, elf2aout flag, partition into flash.uf2.
8. Switch shipped programs and boxes; update composition gate with measured
   block and swap numbers.
9. Native `cc -xip` with its own tests.
10. Board and Renode evidence (coremark, microbench, swap, exec latency).

## Common to both data designs: user-address admission of libc flash

`baduaddr_span` (`machdep.c:920`) admits only `[__user_data_start, USER_TOP)`
and guards `copyin` (`machdep.c:1185`), `copyinstr` and the iovec check
(`sys_generic.c:68`). A pointer into libc flash rodata therefore fails with
EFAULT: `sys_errlist` strings, `strerror()` results, and libc's own literal
paths ("/etc/passwd", "/bin/sh") passed to `open` or `execve`. The kernel must
admit exactly the enabled libc range for read-direction transfers (`copyin`,
`copyinstr`, the writev source side) while `copyout`, readv targets and
`physio` (`vm_swp.c:124`) stay window-only. The admitted range matches the
MPU region's enabled subregions, not the whole 64 KB region. This commit and
its fault gate precede any program switching to the shared libc.

## Fixed-data design (A) against r9-relative libc data (B)

B builds libc with `-fPIC -msingle-pic-base -mpic-register=r9
-mno-pic-data-is-text-relative` and programs with `-ffixed-r9`. crt0 reserves
libc data after the program's bss through `_brk`, copies .data, zeroes .bss,
sets r9 and fixes up the GOT and data words once. libc code stays at the fixed
XIP address in both designs, so exports, ABI hash, MPU region, partition and
the admission change above are common. Tock's `libtock-c` crt0 applies the
same startup-time GOT and data fixups on Cortex-M0 (stream 05).

| Dimension | A: fixed data at window base | B: r9-relative data after bss |
| --- | --- | --- |
| Kernel files touched | exec_aout.c, exec_hsaout.c/.h, exec_aout.h, exec_subr.c, vm_swap.c, swapram.c/.h, kern_fork.c, proc.h, machparam.h, libhsaout.c, plus the common set | the common set only: mpu.c, machdep.c (admission), flash.h, RP2040.ld, an optional boot hash line |
| Process-length invariant | repaired by setting both lengths to `L + text + data + bss`, with new tests | avoided: libc data is break memory, so `p_dsize` and `u_dsize` follow `brk` (`kern_mman.c:51-58`) |
| Packed loader base | both loaders change | avoided: programs link and load at 0x20000000 |
| `_end` references | two break cells | the same two cells; crt0 sets `__minbrk` to the end of libc data |
| Flash | libc text about 32.4 KB | text +7.2 % at object level (38,856 to 41,644 bytes on 185 sources), about +2.3 KB, plus about 330 bytes of relocation records; fits 48 KB with about 11 KB headroom |
| Per-process RAM and swap | the image's libc state (5,632 bytes reserved for the union image) | the same plus about 696 bytes of GOT and 82 relocated words, about +13 %, swapped as data |
| Exec cost | copy .data, zero the reservation | the same plus GOT and data-word fixups (tens of microseconds, unmeasured) |
| Run-time speed | absolute literals | each global access adds an r9-relative load; unmeasured |
| Callbacks and signals | no register assumption | r9 never changes after crt0; programs are `-ffixed-r9`, so qsort comparators and atexit handlers cannot clobber it; the trapframe holds r9 (`frame.h:41`), sigcontext saves and restores it (`sig_machdep.c:112,244`), `_setjmp` saves r4-r11; libgcc and ROM helpers preserve r9 under AAPCS; a gate checks r9 at handler entry and exit |
| ROM float resolver | cache cell at a fixed window address, unchanged | the cell lies in the heap inside the window, so the resolver's window check passes, but the trampolines' absolute `ldr r2,=cell` and `.word cell` (`rom_float.inc`) need r9-relative rewrites, or float printf stays out of libcx behind a crt0-set hook |
| Board ld | `-T base+L`, a flag and hash option, import stubs | import stubs only; a hash check can live in crt0 |
| Admission | exact at exec | libc data is outside exec admission; crt0 exits cleanly when `sbrk` fails, and elf2aout or a gate checks `image + libc data + stack <= MAXMEM` |

Recommendation: pursue B first. It removes the kernel loader and swap
changes, the riskiest part of A, for about 2.3 KB of flash and about 13 % more
libc RAM per process. B becomes credible after three results: coremark and a
stdio microbenchmark inside an agreed bound, a decision on the ROM float
trampolines, and a signal and callback gate that checks r9. A remains the
fallback when the B benchmark misses its bound.

## Resolution of the adversarial review

Verified by the stream's originating agent against the tree and applied by the
orchestrator, because the agent's role had no write access.

1. ACCEPT. No PIC alternative was costed; the comparison above adds the r9
   libc-data design measured at object level.
2. ACCEPT, narrower. The text already set `p_dsize = L + data + bss`; both
   `p_dsize` (`exec_subr.c:475`) and `u_dsize` (`exec_subr.c:814`) must become
   `L + text + data + bss` because `brk()` zeroes from `p_daddr + u_dsize`.
3. ACCEPT. The packed loader validates and expands against
   `__user_data_start` (`exec_hsaout.c:127,168-201`); a flag mask reserves
   nothing.
4. ACCEPT. `sbrk.c:44` clamps `brk()` against `_end`; two libc-owned break
   cells replace both references.
5. ACCEPT. The board ld rejects out-of-range Thumb calls without a veneer
   (`ld.c:592-645`); N_ABS covers storage only.
6. ACCEPT. The 187-block figure is withdrawn; frozen rows are 502 to 288
   (union, 28 executables) and 475 to 278 (whole, 26 executables).
7. ACCEPT. The Dhara loss is 28 KB for a 48 KB partition, confirmed by
   `flashimg -c`.

The first report's claim that interposition "fails closed" is withdrawn in the
Linking section: stream 01 measured silent binding.
