# Kernel footprint candidates: noreturn exit and sched, boot ROM copy and fill

Two candidates from the kernel-minification notes, measured on the host by
building all six RP2040 kernels at `4726ff64e` (`arm-none-eabi-gcc` 16.2.0,
`bmake MACHINE=rp2040 -j12 kernel`) before and after a prototype. The tree
carries the first (see "Applied" below); the second stays a measured
candidate. This note records the measurements, the source argument each
depends on and what remains open. Decision D220 moved the remaining board
measurements (speed, boot behavior) ahead of the GizmoBSD rename.

Baseline sizes (`arm-none-eabi-size`, bytes):

| Kernel | text | data | bss |
| --- | ---: | ---: | ---: |
| PICO | 98,250 | 128 | 42,688 |
| PICO_DIAG | 100,785 | 128 | 42,800 |
| PICO_UART | 90,187 | 120 | 18,064 |
| PICOCALC | 127,122 | 144 | 53,592 |
| PICOCALC_LEAN | 119,382 | 144 | 48,672 |
| PICOCALC_LEAN_DIAG | 121,909 | 144 | 48,784 |

"Image" below is the length of `arm-none-eabi-objcopy -O binary` of
`unix.elf`, which the link rounds to 256-byte pages, so a text change moves
the image only when it crosses a page boundary.

## `__dead` on `exit()` and `sched()`

**Claim.** Neither function returns, so declaring both `__dead`
(`sys/sys/cdefs.h`: `_Noreturn` or `__attribute__((__noreturn__))`) is
correct and lets callers drop the code after the call.

**Source argument.**

- `sched()` (`sys/kern/vm_sched.c`) is one `for (;;)` loop. Its two
  `break` statements leave inner `for (rp = ...)` scans of the process
  list; nothing leaves the outer loop.
- `exit()` (`sys/kern/kern_exit.c`) ends in `swtch()` after
  `proc_zombie()` sets `p_stat = SZOMB` and moves the process to
  `zombproc`. `swtch()` (`sys/kern/kern_synch.c`) returns into a user
  process only through its `u_rsave`, after selecting it from the run queue
  `qs`. A zombie never returns to `qs`: `setrun()` panics on `SZOMB`, and
  every other `setrq()` caller acts on a runnable or swapped-in process. So
  `swtch()` does not return into `exit()`.
- Falsifier: a path that calls `setrq()` on a zombie, or a `return` added to
  `sched()`'s outer loop. With `__dead` and the
  `__builtin_unreachable()` below, such a path becomes undefined behavior
  instead of a return into the caller.

**Prototype.** `__dead` on both declarations in `sys/sys/proc.h`, and
`__builtin_unreachable()` after the `swtch()` call so GCC accepts the
`noreturn` definition warning-free (the changed lines):

```c
/* sys/sys/proc.h */
__dead void sched (void);
__dead void exit (int rv);

/* sys/kern/kern_exit.c, the end of exit() */
    wakeup((caddr_t) &runin);
    swtch();
    __builtin_unreachable();
    /* NOTREACHED */
}
```

**Measurement.**

| Kernel | text | data | bss | image |
| --- | ---: | ---: | ---: | ---: |
| PICO | -16 | 0 | 0 | -256 |
| PICO_DIAG | -8 | 0 | 0 | 0 |
| PICO_UART | -8 | 0 | 0 | 0 |
| PICOCALC | -8 | 0 | 0 | 0 |
| PICOCALC_LEAN | -8 | 0 | 0 | 0 |
| PICOCALC_LEAN_DIAG | -8 | 0 | 0 | 0 |

`arm-none-eabi-nm -S` places the change in `exit` (-4), `rexit` (-4) and
`procxmt` (-4), with `newproc` +4 on PICOCALC. PICO's image crosses a page
boundary, which is the one image change. The warning count of the kernel build
is unchanged (12).

**Reading.** At `4726ff64e` positive in every kernel, and small. The change is a declaration
fix of the kind the C17 work makes anyway; by the plan's rule (drop if not
positive) it survives as a candidate.

**Applied.** `sys/sys/proc.h` declares `sched()` and `exit()` `__dead`, and
`exit()` calls `panic("exit")` after `swtch()`: a zombie is never queued
again, so `swtch()` does not come back, and if it ever did the kernel stops
with a message instead of running on past a noreturn call. The host kernel
test `tests/kernel/sigauth_test.c` builds `kern_sig.c` with
`-Dexit=sigauth_exit`, so its stub leaves through `hk_escape()` while the
harness keeps the C library's `exit()`. Measured on the eight RP2040
kernels at `385506ba8` (`arm-none-eabi-gcc` 16.2.0), data and bss unchanged
and every build warning-free:

| Kernel | text before | text after |
| --- | ---: | ---: |
| PICO | 98,361 | 98,374 |
| PICO_DIAG | 101,222 | 101,235 |
| PICO_UART | 90,701 | 90,714 |
| PICOCALC | 124,016 | 124,029 |
| PICOCALC_LEAN | 119,802 | 119,815 |
| PICOCALC_LEAN_DIAG | 122,663 | 122,676 |
| PICOCALC_SD | 130,670 | 130,683 |
| PICOCALC_SD_DIAG | 156,099 | 156,091 |

The change costs 13 bytes of text in seven kernels and saves 8 in
PICOCALC_SD_DIAG. In `exit()` the `panic()` reuses the function's existing
call to `panic` and adds a load, a branch, a literal and the 5-byte string;
`init_main.o` grows 4 bytes in the `-Os` kernels once `sched()` is `__dead`.
The prototype's `__builtin_unreachable()` saved 8 to 16 bytes at
`4726ff64e` but ran into the literal pool if `swtch()` returned. Since `2e8cad18b`,
`procxmt()` ends a traced child through `SIGKILL` and no longer calls
`exit()`, so one of the prototype's three shrinking callers is gone.

## Boot ROM `memcpy` and `memset` behind `bcopy` and `bzero`

**Claim.** The kernel's `bcopy()`, `bzero()`, `memcpy()` and the two
`__aeabi_memcpy` entries (`sys/arch/rp2040/rp2040/machdep.c`) can call the
boot ROM's `memcpy` ('MC') and `memset` ('MS'), found through the existing
`rom_func_lookup()` (`sys/arch/rp2040/dev/flash.c`), so their own word loops
leave flash.

**Contract.** At `4726ff64e`, `bcopy()`'s comment declares overlapping
regions undefined and its loops copy forward only, yet `namei()`
(`sys/kern/ufs_namei.c`, the symbolic-link step) shifts the rest of the path
within its own `path` buffer with `bcopy(cp, path + dp->i_size, pathlen)`.
With `cp - path` bytes consumed, the destination starts inside the source
whenever `0 < i_size - (cp - path) < pathlen`. That overlap corrupts the path. On
the host, `tests/kernel/bcopy_overlap.py` (pull request #711) runs the
production `bcopy` of both `machdep.c` files at `4726ff64e`: "l/file"
through a link to "ab" becomes `ab//////`, and of its 5,576 copies (every
length, offset and alignment of the grid) all 2,272 whose destination starts
inside the source differ from `memmove`. Under Renode, `check-renode` boots that
revision's PICO_UART kernel, and `cat l/file` with `l` naming `ab` prints
`l/file: No such file or directory`; with #711 the stage passes. The board
was not run. The host harness hid the defect: `tests/kernel/hostkern_kern.c` gives the kernel tests a `bcopy` that
copies backward. The fix gives `bcopy` memmove semantics, so the ROM
`memcpy`, which has no overlap handling, can back it only beside an overlap
branch. The prototype below omits that branch, so its text figures are an
upper bound on the saving of a correct veneer.

**Prototype.** Two function pointers in bss, resolved by `rom_copy_init()` as
the second statement of `startup()`; each entry jumps through its pointer.
The word and byte loops of `bcopy()` and `bzero()` are gone:

```c
/* sys/arch/rp2040/rp2040/machdep.c */
static void rom_copy_init(void);

void
startup(void)
{
	SystemClock_Config();

	rom_copy_init();
	/* ... the rest of startup() unchanged ... */
}

static void *(*rom_memcpy)(void *, const void *, u_int);
static void *(*rom_memset)(void *, int, u_int);

static void
rom_copy_init(void)
{
	rom_memcpy = (void *(*)(void *, const void *, u_int))
	    rom_func_lookup(ROM_CODE('M', 'C'));
	rom_memset = (void *(*)(void *, int, u_int))
	    rom_func_lookup(ROM_CODE('M', 'S'));
}

void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
	rom_memcpy(dst0, src0, nbytes);
}

/* memcpy, __aeabi_memcpy and __aeabi_memcpy4 call rom_memcpy the same way. */

void
bzero(void *dst0, size_t nbytes)
{
	rom_memset(dst0, 0, nbytes);
}
```

**Measurement.**

| Kernel | text | data | bss | image |
| --- | ---: | ---: | ---: | ---: |
| PICO | -136 | 0 | +8 | -256 |
| PICO_DIAG | -136 | 0 | +8 | 0 |
| PICO_UART | -136 | 0 | +8 | -256 |
| PICOCALC | -128 | 0 | +8 | 0 |
| PICOCALC_LEAN | -136 | 0 | +8 | -256 |
| PICOCALC_LEAN_DIAG | -128 | 0 | +8 | -256 |

The kernel's copies take 232 bytes of text at the baseline (`bcopy` 110,
`bzero` 106, `memcpy` 16); in the prototype `bcopy` and `bzero` are 20
bytes each and `memcpy` 16. The 8 bytes of bss are the two pointers. The
warning count is unchanged (12).

**Both budgets.** About 130 bytes of flash for 8 bytes of RAM. The kernel's
RAM and text budgets do not trade freely, so the 8 bytes count against
kernel SRAM. No gate covers that cost: `check-kernel-headroom` measures the
flash-resident bytes alone.

**Open, and needing the board.**

- Speed: the ROM routines against the current loops, on the measured board,
  for the sizes the buffer cache and `copyin`/`copyout` move.
- Ordering: any copy or fill before `rom_copy_init()` jumps through a null
  pointer. The prototype resolves the pointers after `SystemClock_Config()`,
  which was not checked for copies; a landing change needs a gate or a
  zero-RAM fallback that resolves at the first call.
- Core 1: whether code on core 1 calls these entries before core 0 resolves
  the pointers.
- Overlap: the branch that copies backward when the destination starts
  inside the source, in front of the ROM call, and its text cost.
- XIP: the ROM routines run from ROM, but the veneers and their callers stay
  in flash, so the change gives no copy that works while XIP is off.

## Table lookup on each zero fill

On the PICOCALC_SD kernel at f871ca7f5, a zero-fill veneer returns immediately
for zero bytes and otherwise calls the ROM MS routine through rom_func_lookup
with the destination, zero and the requested length. This design requires
neither a writable pointer nor an initialization hook. The overlap-sensitive
bcopy path keeps its existing implementation in both builds.

| GCC 16.2.0 ARM link | Text | Data | BSS | Initialized flash span |
| --- | ---: | ---: | ---: | ---: |
| Existing bzero | 128,916 | 144 | 53,680 | 128,904 |
| ROM fill with lookup per nonempty call | 128,844 | 144 | 53,680 | 128,904 |

ARM nm reports a 32-byte candidate bzero. The 72-byte reduction in linked
text stays within the padding before the aligned board record.
The bytes do not move the flash-image boundary.

The pinned Renode RP2040 B2 ELF has SHA-256
`3a3c4c617d83bc9b910a6fa7bfdffa11ce26ff89370271d861507be821cc49aa`.
Unicorn 2.1.4 executes its actual table lookup through the 16-bit pointers
at offsets 0x14 and 0x18. MC resolves to Thumb entry 0x261d and MS to
0x26b9. For lengths 0 through 80 inclusive, the disjoint copy probe crosses
four source alignments with four destination alignments. The fill probe
crosses four byte values with four destination alignments. All 1,296 cases
per function preserve their guard bytes, return the destination and balance
the stack.

A second probe loads each linked kernel and the ROM, calls its bzero with
four destination alignments, checks the bytes around the destination and
counts executed instructions with UC_HOOK_CODE. For a word-aligned
destination the counts are as follows.

| Bytes cleared | Existing instructions | ROM veneer instructions |
| ---: | ---: | ---: |
| 0 | 4 | 5 |
| 1 | 23 | 73 |
| 4 | 43 | 76 |
| 16 | 45 | 95 |
| 64 | 69 | 104 |
| 256 | 165 | 140 |
| 1,024 | 549 | 284 |

The direction changes between the sampled 64- and 256-byte aligned cases.
At 64 bytes and offset one, the existing routine takes 113 instructions
and the veneer takes 111. Fill length and alignment therefore both matter.
The instruction counts exclude silicon fetch latency and bus contention.
The production kernel retains its existing bzero pending a representative
size and alignment distribution, paired device timing and an image that
crosses a flash placement boundary.
