# Clocks and timers of the Pico and the PicoCalc, and a `clock_gettime` for this kernel

The owner asked for a `clock_gettime` system call that fits the kernel, an
account of the RP2040's 1 MHz timer, the timers of the keyboard controller
and the other parts of the PicoCalc, and an audit of the timer features no
driver serves yet (decision D118). The findings lead:
- The RP2040's 64-bit microsecond TIMER is the best clock on the board. The
  kernel already reads it; no user interface exposes it.
- No time source reachable from the RP2040 survives a power-off. The
  AXP2101's RTC supply pins are unconnected, and the keyboard controller's
  RTC domain is powered from the same rail the PMU switches off. Boot time
  continues to come from the root superblock.
- A `clock_gettime` and `clock_getres` with `CLOCK_REALTIME` and
  `CLOCK_MONOTONIC` at microsecond resolution measure 200 B of text in a
  GCC 14.2.1 sizing build, plus at most 29 B of trace names, and 0 B of RAM. The
  kernel needs no 64-bit division for them.

Evidence classes: data sheet (DS: RP2040 datasheet build 3184e62-clean,
section and printed page; STM32F103 DS5319 Rev 20; AXP2101 V1.4; Abracon
ABM8-272-T3 drawing), schematic (S, 300 dpi render of the mainboard V2.0
schematic), source (SRC, `origin/main` 268483f7; firmware SRC-FW at
ClockworkPi e36ee963b4), build (B, arm-none-eabi-gcc 14.2.1, Debian
`15:14.2.rel1-1`) and inference (I). No board, Renode or qemu run belongs
to this note.

## Every time source

| Source | Resolution and width | Clocked from, drift | Sleep, reset, power-off | Driver in the tree |
| --- | --- | --- | --- | --- |
| RP2040 TIMER (DS 4.6, p. 534) | 1 us, 64-bit counter read through latching registers; four alarms that match the low 32 bits, one IRQ each | the watchdog tick generator, one tick per 12 `clk_ref` cycles of the 12 MHz crystal (DS 4.7.2, p. 544; `machdep.c:243-249`, `:464-465`); crystal drift | runs in SLEEP while `clk_ref` and the tick run (I); stops in DORMANT with the oscillators (DS 2.11.3, p. 161); reset with the other blocks on a watchdog reset that selects them (`include/watchdog.h:66-72`); lost at power-off | read only: `rp2040/clock.c:21-37` (tick catch-up), `dev/usb.c:79-80`, `:425`, `:860`, `:1034`, `:1107`, `dev/picocalc_reg.h:60`, `:105-107`; no alarm is used |
| Cortex-M0+ SysTick, one per core (DS 2.4.5.1.1; ARMv6-M) | 24-bit down-counter on the processor clock, 8 ns at 125 MHz | `clk_sys` from the PLL, crystal drift | stops when `clk_sys` stops | `clock.c:55-68` sets the reload for `HZ` 1000 (`include/machparam.h:44`); `rp2040/systick.c:44-84` is the handler |
| RTC block (DS 4.8, pp. 548-549) | 1 s calendar (year to second), alarm IRQ that can wake DORMANT | `clk_rtc`, any integer rate from 1 Hz to 65,536 Hz, 46,875 Hz in the SDK's arrangement (DS 2.15.3, p. 183); DORMANT wake needs an external clock source (DS 2.11.3, p. 161) | lost at power-off: the RP2040 has no battery domain | none: `machdep.c:489` stops `clk_rtc`; `locore0.S:286`, `:342` route its IRQ to the default handler |
| Watchdog counter (DS 4.7, pp. 544-547) | 24-bit, counts down in ticks; `LOAD` holds twice the ticks (erratum RP2040-E1) | the same tick | its eight scratch registers survive a watchdog or software reset | the counter is never armed (`include/watchdog.h:34-44`); the kernel uses `TRIGGER` to reset (`rp2040/core1.c:186-188`) and the scratch registers (`machdep.c:259-307`); `WATCHDOG_MEASURE` measures its rate and no configuration sets it (`machdep.c:608-673`) |
| PWM slices (DS 4.5, p. 521) | eight 16-bit counters with fractional dividers; a slice can count edges or gate on its B pin | `clk_sys` | as `clk_sys` | slice 5 drives the PicoCalc audio and bell (`dev/picoaudio.c:50`); none used as a timer |
| PIO state machines (DS 3.5.5, p. 339) | 16.8 fixed-point clock divider per state machine; a program can count, time or generate edges | `clk_sys` | as `clk_sys` | PIO0 runs the PSRAM program (`dev/psram_pio.h`); PIO1 is free |
| Crystal (Pico data sheet section 2; Abracon ABM8-272-T3 drawing, lines 24-32) | 12 MHz | +-30 ppm at 25 C, +-30 ppm over temperature, +-5 ppm the first year | stops in DORMANT | enabled at boot (`machdep.c:446-449`) |
| USB start-of-frame (SOF) | 1 ms frames timed by the host | the host's clock | present while USB is enumerated | `dev/usb.c:380`, `:1034` stamp each SOF with TIMERAWL for console pacing; no drift estimate uses it |
| STM32F103 SysTick (DS5319 2.3.15, p. 19) | 24-bit; the Arduino core's 1 ms `millis()` | the 8 MHz crystal X201 (S; tolerance not stated on the schematic) | off when the PMU is off | firmware only: key repeat, the 20 s battery refresh and the I2C watchdog (SRC-FW `picocalc_keyboard.ino:45-50`, `:310`, `:636-640`) |
| STM32F103 TIM1-TIM4 (DS5319 2.3.15, pp. 17-18) | 16-bit with 16-bit prescalers | the same | as above | firmware only: 10 kHz PWM on PA8 for the LCD backlight and on PC8 for the keyboard backlight (SRC-FW `backlight.ino:15-16`, `:43-44`) |
| STM32F103 RTC and backup registers (DS5319 2.3.14, p. 17) | 32-bit seconds counter, 20-bit prescaler, alarm; ten 16-bit backup registers | the 32.768 kHz crystal X202 on PC14/PC15 (S) | the domain runs from VDD or from the VBAT pin (DS5319 2.3.14); on this board VBAT is wired to `MCU_3V3`, the PMU's DCDC1 output (S), so the domain loses power whenever the PMU powers off | unused by the firmware (no RTC or backup-register call in any file) and not exposed over I2C |
| STM32F103 watchdogs (DS5319 2.3.15, p. 18) | IWDG 12-bit on a 40 kHz RC; WWDG 7-bit | internal RC; `clk` | IWDG runs in Stop and Standby | unused by the firmware (a case-insensitive search of its sources for iwdg, wwdg and watchdog finds nothing) |
| AXP2101 timers (AXP2101 V1.4) | I2C watchdog 1-128 s (REG 19, p. 34); charge safety timers, 40-70 min pre-charge and 5-20 h total (6.7.4.1, p. 25; REG 67, p. 49); power-key levels (REG 27, p. 38); 64 s ADC sampling while off on battery (REG 36, p. 40) | internal | the PMU stays powered from the cell | reachable only by the keyboard controller; the firmware never writes REG 18 bit 0 or REG 19 (the same search finds no watchdog call) |
| AXP2101 RTC supply | none: `VRTC` (pin 28) and `VBACKUP` (pin 27) are not connected (S), so no backup cell and no RTC LDO output exist on this board (DS 6.12.2, p. 29) | | | |

The ST7365P panel's tearing-effect output, a frame-rate timing source, is
not connected (`d2ffbd83:docs/research/memory-devices.md`, row 13). The
CH340C and the SD card carry no timer the RP2040 can read.

Two consequences. First, every clock the RP2040 reads runs from the same
12 MHz crystal, so the TIMER, SysTick and the PLL-derived counters share
one drift of at most about 35 ppm in the first year, about 3 s a day (I,
DS figures summed). The USB host's frame clock is the only independent
reference on the board, available whenever the console is on USB. Second,
nothing keeps time across a power-off: `sys/kern/init_main.c:133` sets the
clock from the root file system's last write, as
`docs/research/time-representation-past-2038.md` records in its residuals.

## `clock_gettime` for this kernel

### What exists

`gettimeofday(2)` returns `time` with `tv_usec = lbolt * usechz`
(`sys/kern/kern_time.c:61-90`): 1 ms resolution, stepped by `settimeofday`
and slewed by `adjtime`. The kernel keeps a monotonic clock, `monosec` and
`monotick` (`sys/kern/kern_clock.c:19-20`, `:322-346`), for interval timers
only; no system call returns it and libc has no `clock_gettime`. `clock.c`
stamps every delivered tick with TIMERAWL (`clock_tick_stamp()`) and restores
ticks lost in masked windows (`clock_catchup()`), which also advances the
monotonic clock (`clock.c:84-109`).

### Interface

- **Calls.** `clock_gettime(clockid_t, struct timespec *)` and
  `clock_getres(clockid_t, struct timespec *)`. `clock_settime` is left out:
  `settimeofday` already sets `CLOCK_REALTIME`, and `CLOCK_MONOTONIC` cannot
  be set.
- **Clocks.** `CLOCK_REALTIME` 0 and `CLOCK_MONOTONIC` 3, OpenBSD's values
  (`openbsd-src/sys/sys/_time.h:64-66`); FreeBSD uses 4 for the monotonic
  clock (`freebsd-src/sys/sys/_clock_id.h:49-56`). Any other id returns
  `EINVAL`.
- **Numbers.** `sys/kern/syscalls.master` has free slots; 87 and 88 are
  unused here, and OpenBSD's `clock_gettime` is 87 (`openbsd-src/sys/kern/syscalls.master:196`).
  A free slot costs nothing in the dispatch tables; appending 163 and 164
  costs 5 B a number (`syscall_handlers` 652 B and `syscall_nargs` 163 B for
  163 numbers, B).
- **Layout.** The kernel copies out its narrow `struct timespec`: a 32-bit
  unsigned second count and a `long` nanosecond count, 8 B. Under the 64-bit
  interface of `docs/research/time-representation-past-2038.md` ("The
  interface width: 64 bits, chosen"; "Programs on the 64-bit view"), the
  headers bind `clock_gettime` and `clock_getres` to `__clock_gettime64` and
  `__clock_getres64` with `__TIME64_NAME()`, and libc wraps the narrow entry
  and widens `tv_sec` zero-extended, as `lib/libc/gen/time64_gettimeofday.c`
  does for `gettimeofday`. Programs Smaller C compiles on the board call the
  narrow entry by name. `tools/check_time_binding.py` gains the two names
  (24 to 26).

### Mechanism

Read under `splhigh()`: the sub-tick is TIMERAWL less the last tick's stamp,
clamped to one tick less a microsecond; `CLOCK_REALTIME` adds it to
`time.tv_sec` and `lbolt` ticks, `CLOCK_MONOTONIC` to `monosec` and `monotick`.
Every quantity stays below 10^6 microseconds before the nanosecond scale,
so the kernel needs no 64-bit division; it links none today (B: `nm` of the
PICO kernel lists `__aeabi_uidiv` and `__aeabi_idiv` and no `__aeabi_uldivmod`),
and reading the 64-bit TIMER directly would pull in a 64-bit divide to
split seconds.

The clamp keeps the clock monotonic: between ticks the value grows with
TIMERAWL and stops one microsecond short of the next tick; a delivered tick
adds a whole tick to `monotick`; a masked window that holds the value at the
clamp ends with `clock_catchup()` adding the owed ticks. `clock_getres`
reports 1,000 ns for both clocks, which is what the interpolation delivers.
NetBSD's first version reported `1000000000 / hz` (below), which the
interpolation would make untrue here.

### Cost

A sizing build of the two handlers with the PICO kernel's options
(`bmake -n kern_time.o` in `sys/arch/rp2040/compile/PICO`, GCC 14.2.1):

| Part | Text | RAM |
| --- | ---: | ---: |
| `clock_gettime` handler | 148 B (B) | 0 |
| `clock_getres` handler | 52 B (B) | 0 |
| Trace names on `SYSTRACE` kernels: 25 B of pool, two 2 B spans | at most 29 B (I: the pool overlaps names, so less if one shares a tail) | 0 |
| An accessor for the tick stamp in `clock.c`, or the stamp made global | 0 to 12 B (I) | 0 |
| Dispatch tables | 0 B in free slots, 10 B appended | 0 |
| Total | about 230 B | 0 |

For comparison, `gettimeofday` is 104 B and `settimeofday` 164 B in the same
build (B). The estimate in `d2ffbd83:docs/research/memory-devices.md` was
120 to 200 B. The sizing source follows, its comments shortened; it declares the stamp as an
external and was compiled, not linked, with the options `bmake -n kern_time.o`
prints in `sys/arch/rp2040/compile/PICO`, then measured with
`arm-none-eabi-nm -S`:

```c
#include <sys/param.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>

#define	CLOCK_REALTIME		0
#define	CLOCK_MONOTONIC		3

struct ktimespec {			/* the kernel's narrow layout */
	u_int	tv_sec;
	long	tv_nsec;
};

extern volatile u_int clock_stamp_us;	/* clkcatch.stamp_us in clock.c */
#define	TIMERAWL	(*(volatile u_int *)0x40054028UL)
#define	TICK_US		(1000000U / HZ)

void clock_gettime(void);
void clock_getres(void);

void
clock_gettime(void)
{
	struct a {
		int	clock_id;
		struct ktimespec *tp;
	} *uap = (struct a *)u.u_arg;
	struct ktimespec ts;
	u_int us;
	int s;

	s = splhigh();
	us = TIMERAWL - clock_stamp_us;
	if (us >= TICK_US)
		us = TICK_US - 1;
	switch (uap->clock_id) {
	case CLOCK_REALTIME:
		ts.tv_sec = time.tv_sec;
		us += lbolt * TICK_US;
		break;
	case CLOCK_MONOTONIC:
		ts.tv_sec = monosec;
		us += monotick * TICK_US;
		break;
	default:
		splx(s);
		u.u_error = EINVAL;
		return;
	}
	splx(s);
	ts.tv_nsec = (long)us * 1000;
	u.u_error = copyout((caddr_t)&ts, (caddr_t)uap->tp, sizeof ts);
}

void
clock_getres(void)
{
	struct a {
		int	clock_id;
		struct ktimespec *tp;
	} *uap = (struct a *)u.u_arg;
	struct ktimespec ts;

	if (uap->clock_id != CLOCK_REALTIME &&
	    uap->clock_id != CLOCK_MONOTONIC) {
		u.u_error = EINVAL;
		return;
	}
	if (uap->tp == NULL)
		return;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000;
	u.u_error = copyout((caddr_t)&ts, (caddr_t)uap->tp, sizeof ts);
}
```

`gettimeofday` can share the sub-tick read for about +16 to +32 B (I), so the
two calls agree to the microsecond; left alone, `gettimeofday` keeps its 1 ms
steps and the two clocks differ by up to a tick.

### Tests and their known-bad inputs

| Tier | Test | Known-bad input it must reject |
| --- | --- | --- |
| Host | the read helper compiled as a pure function of `(monosec, monotick, stamp, now)`, driven through ticks, a masked window with catch-up, and a pending tick: non-decreasing, within one tick of truth | a mutant without the clamp (decreases across a pending tick); a mutant reading `lbolt` for `CLOCK_MONOTONIC` (decreases across `settimeofday`) |
| Host | `tests/time64_wrappers`: the wrapper widens a narrow `tv_sec` with bit 31 set to a value above 2^31 | a sign-extending wrapper |
| Cross | `check-time-binding` with the two new names | a unit that calls `clock_gettime` at `_TIME_BITS=32` |
| qemu-user | the wrapper's instruction sequence; qemu-user reaches a Linux system-call seam and runs no DiscoBSD kernel code (`docs/research/qemu-thumb1-test-tier.md:51-52`) | as the host wrapper test |
| Renode | a PICO_UART boot that reads `CLOCK_MONOTONIC` 10,000 times, sleeps one second between two reads, asks `clock_getres` and passes clock id 99 | a transcript with a decreasing pair, and one where id 99 succeeds |
| Board | drift of `CLOCK_MONOTONIC` against the host over 600 s | none needed: a measurement against the crystal's +-35 ppm bound (21 ms in 600 s) |

### How the reference trees first did it

- NetBSD, `7c90cd4d2cf` (jtc, 1996-11-15, "Add clock_gettime, clock_settime,
  and clock_getres"): `CLOCK_REALTIME` only, read through `microtime()` and
  converted with `TIMEVAL_TO_TIMESPEC`; `clock_getres` returned `1000000000 / hz`;
  `settime()` became shared with `settimeofday`. 196 lines inserted across
  eight files: `init_sysent.c`, `kern_time.c`, `syscalls.c`, `syscalls.master`,
  `syscall.h`, `syscallargs.h`, `sys/time.h` and `sys/types.h` (the full
  netbsd-src history, `git log -S clock_gettime -- sys/kern/kern_time.c`).
- OpenBSD: the manual page dates the calls to OpenBSD 2.1
  (`lib/libc/sys/clock_gettime.2`, HISTORY); they occupy 87-89
  (`sys/kern/syscalls.master:196-200`).
- FreeBSD: the manual page dates them to FreeBSD 3.0
  (`lib/libsys/clock_gettime.2`, HISTORY).
- DragonFly's page carries no HISTORY section; the FreeBSD, OpenBSD and
  DragonFly clones are shallow, so their first commits were not read.
- 2.11BSD, RetroBSD and LiteBSD have no `clock_gettime` (`rg -l clock_gettime`
  over each `sys` tree).

The NetBSD shape is the one to copy: the narrow interface first, built on
the kernel's existing time read, with the monotonic clock added where the
kernel already keeps one.

## Timer drivers not built

| Slice | Feature | Files | Text, RAM (I unless noted) | Evidence without a board | Board measurement | Value |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | `clock_gettime`, `clock_getres` as above | `sys/kern/syscalls.master` and `bmake regen-kernel-metadata`, `sys/kern/kern_time.c`, `rp2040/clock.c`, `include/time.h`, `sys/sys/types.h` (`clockid_t`), `lib/libc/gen`, `tools/check_time_binding.py` | about 230 B (200 B measured), 0 | the host, cross and Renode rows above | drift over 600 s | the benchmark clock D118 asks for |
| T2 | `gettimeofday` on the same sub-tick read | `kern_time.c` | +16 to +32 B, 0 | the T1 host model | none | one time base for both calls |
| T3 | TIMER alarm in place of the 1 kHz SysTick when idle (tickless idle) | `clock.c`, `systick.c`, `sys/kern/kern_clock.c` | +200 to +400 B, +8 B | Renode's TIMER model with alarm IRQs; a host model of the tick accounting | idle current, before and after | power on battery |
| T4 | Hardware watchdog armed, reloaded from `hardclock` | `machdep.c`, `include/watchdog.h` | +40 to +80 B, 0 | none: Renode does not model the counter's rate; `WATCHDOG_MEASURE` first (`machdep.c:608-673`) | a deliberate masked spin resets with `REASON` TIMER; a 400 ms flash erase does not | recovery from a hung kernel without the host |
| T5 | RP2040 RTC set and read | a new `rp2040/rtc.c` | +150 to +250 B, 0 | Renode RTC model | none useful | low: the block keeps no time across power-off, and the kernel already keeps the calendar in `time` |
| T6 | Crystal drift against USB SOF | `dev/usb.c`, a sysctl node | +40 to +60 B, +8 B | none | the ppm the node reports against a host log | measures the 35 ppm bound on each board |
| T7 | STM32 tick or RTC register | a fork of `picocalc_keyboard` | +40 B kernel | none | not recommended: D123 keeps the vendor firmware, and the RTC domain shares the PMU's switched rail |  |
| T8 | AXP2101 watchdog or charger timers | the same fork | not reachable from the RP2040 | | | |

## Residual owner decisions

1. **Resolution of the new clocks.**
   - Microsecond, by sub-tick interpolation from TIMERAWL (recommended): what
     a benchmark needs, about 40 B of the 200 B (I).
   - Tick resolution, 1 ms, as `gettimeofday` today: smaller, and too coarse
     for timing a function.
2. **System-call numbers.**
   - Free slots 87 and 88 (recommended): no table growth, and 87 matches
     OpenBSD's `clock_gettime`.
   - Append 163 and 164: keeps the holes for older interfaces, 10 B of tables.
3. **`gettimeofday` and the sub-tick read.**
   - Share it (T2, recommended): one time base, +16 to +32 B.
   - Leave `gettimeofday` at 1 ms: no change, and the two calls disagree by
     up to a tick.
4. **The hardware watchdog (T4).**
   - Measure its rate with `WATCHDOG_MEASURE`, then arm it (recommended):
     recovery without a host, after a board session.
   - Leave it unarmed: the core-1 stall detector and the host's
     `picotool reboot -f` remain the recovery paths.

## Unverified and not run

- The sizing build compiled the two handlers alone, not linked into a kernel;
  the totals assume the linker adds no veneers.
- The crystal's tolerance comes from the part's drawing; no board has been
  measured against a reference.
- The 46,875 Hz `clk_rtc` figure is the data sheet's table of SDK defaults;
  this kernel stops the clock instead.
- The STM32 8 MHz crystal's tolerance is not on the schematic.
- Which STM32 timer drives PC8 depends on the Arduino core's pin map, which
  is not in the repository.

## Sources

The RP2040 datasheet, Pico data sheet, Abracon ABM8-272-T3 drawing, DS5319,
AXP2101 V1.4 and the schematic are the notes repository files whose
SHA-256 sums `d2ffbd83:docs/research/memory-devices.md` lists, except the
Abracon drawing (`docs/rp2040/abracon_abm8_272_t3_source_control_drawing_456603_revb.pdf`
in the same repository, revision B). Reference trees: netbsd-src 1e8e727e461
(full history), openbsd-src 8849c5d46, freebsd-src 88e7371d9 and
DragonFlyBSD 94448f62 (shallow), 211bsd 0a38e59, retrobsd 10e2a8d, LiteBSD
03934b7.
