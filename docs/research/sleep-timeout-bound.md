# The sleep() timeout bound: survey, first principles, repair

Scope: `sleep(3)` in `lib/libc/gen/sleep.c`, the kernel path its `select()` takes, the constants that bound that path, and the repair that lets `sleep()` sleep any unsigned request. The repair is in libc; the kernel is unchanged.

## Findings

1. On `origin/main` 4b3eaf6c, `sleep(n)` for `n` above 100,000,000 does not sleep. `select()` fails with `EINVAL` (`itimerfix()`, `sys/kern/kern_time.c:348`), the clock has not moved, and `sleep()` returns `n` unslept. `/bin/sleep 200000000` therefore returns at once.
2. The bound does not protect the arithmetic beneath it. At HZ 1000 the tick count saturates at 0x7fffffff, 2,147,483.647 s (24.9 days), and `select()` returns 0 at that point without re-reading the clock (source reading, not executed). A request between 2,147,484 and 100,000,000 s accepted by `itimerfix()` wakes about 24.9 days early, with no signal.
3. At the target's width (`_TIME_BITS=32`, `tv_sec` a signed 32-bit `long`) a request above LONG_MAX arrives negative, which is refused the same way. The ranges 100,000,001 to LONG_MAX and LONG_MAX + 1 to UINT_MAX fail by two routes and with one result.
4. The 100,000,000 constant is inherited: the earliest trees available (the 2.11BSD patch 431 import and 4.4BSD-Lite2) carry it, the four modern trees surveyed dropped it, and nothing in this kernel's tick arithmetic needs it.
5. The repair is a chunked loop in `sleep()`: at most 1,000,000 s per `select()`, repeated until the clock shows the request elapsed, ended by any nonzero `select()` result. It adds 48 to 52 bytes to each a.out program that links `sleep()` (the loop alone measured 16 to 24) and nothing to the kernel.

## 1. Record before editing

Observation. `lib/libc/gen/sleep.c` on main passes the whole request to one `select()` and subtracts the clock difference; the ledger row (LIBC-4) reports that `sleep(100000001)` returns at once. The existing gate `check-libc-sleep` replays two clock readings and never feeds `select()` a failure.

Governing rule. POSIX `sleep()` suspends the caller until the requested number of seconds has elapsed or a signal is delivered, and returns the unslept part. A `sleep()` that returns early without a signal, or returns the whole request without sleeping, breaks the first clause.

Hypotheses.
- H1: for `n` above 100,000,000 (and above LONG_MAX at the 32-bit view) `select()` fails with `EINVAL` and `sleep()` returns `n`.
- H2: for `n` from 2,147,484 to 100,000,000 the kernel saturates the tick count and `select()` returns 0 after 2,147,483 s, so `sleep()` returns `n - 2,147,483` without a signal. Evidence class: source reading and the kernel model of the test; the kernel was not run, and a 24.9-day sleep on silicon was not attempted.
- H3: a loop with a per-call chunk below 2,147,484 s, repeated on a clock that shows time left and ended by a nonzero `select()` result, sleeps every unsigned request and returns the unslept part on a signal or a failure.

Falsifiers. H1: a timeout above the bound that the kernel model accepts, or a board run in which `sleep(100000001)` blocks. H2: `hzto()` or `select1()` re-reading the clock after `EWOULDBLOCK`. H3: a request that the loop sleeps short, a failure that spins, or a signal that does not return the remainder.

Order of events. This record was written after the first run of the extended test against the unchanged `sleep.c`, not before it. The hypotheses come from the source reading that preceded the test; the prediction below was derived from the model and the case list before the case list was final, and was not saved ahead of the run. Read it as a consistency check of the model, not as a blind prediction.

Expected movement against the unchanged `sleep.c`: the eight replay cases pass. Of the fifteen kernel-model cases five pass (requests 1, 1,000,000 and 2,147,483; the signal after one chunk; the failure on the first call) and ten fail: requests 2,147,484 and 100,000,000 (the tick count saturates), requests 100,000,001, LONG_MAX, LONG_MAX + 1 and UINT_MAX (refused), the clock wrap over 3,000,000 s (saturates), the two signal cases whose request is past the bound, and the failure on the second call (the old function makes one call). Expected against the repaired `sleep.c`: all pass at native and ILP32 width, and the single-select negative control fails.

Observed against the unchanged function: eight of eight replay cases pass and eleven of the fifteen kernel cases of that first list failed at both widths, the expected ten plus "signal after one chunk". The extra failure comes from an assertion added after the first run (no timeout may exceed the saturated tick count): the old function asked for 3,000,000 s in that case although its signal return was correct. The assertion states the bound the repair must respect, so it stays. Against the repaired function all cases pass at both widths, and the single-select control fails the same eleven.

## 2. How the reference trees handle it

Every row was read at the cited lines of the tree named.

| Tree | `sleep()` | Kernel validation and tick conversion | Behavior for UINT_MAX |
|---|---|---|---|
| 2.11BSD | `lib/libc/gen/sleep.c:35-39`: one `select()`, then the clock difference (the code this port inherits) | `itimerfix`, `sys/sys/kern_time.c:244-250`: `tv_sec > 100000000L` is `EINVAL`; `hzto`, `sys/sys/kern_clock.c:336` | the `select()` fails; `sleep()` returns at once |
| 4.4BSD-Lite2 | `sleep.c:62-90`: `setitimer` and `sigpause` loop; the arming call at :84 is unchecked | `itimerfix`, `kern_time.c:341-350`, `> 100000000`; `hzto`, `kern_clock.c:331-352`: exact below `0x7fffffff/1000 - 1000` s, `sec * hz` to `0x7fffffff / hz`, then `0x7fffffff` ticks | arming fails, `ringring` never sets, `sigpause` loops until some other signal handler runs and the loop repeats: no return |
| RetroBSD | `src/libc/gen/sleep.c:35-43`, the 2.11BSD select form | `kern_time.c:254`, the same bound; `kern_clock.c:326` | as 2.11BSD |
| LiteBSD | `sleep.c:57-86`, the 4.4BSD form | `kern_time.c:369`, the same bound; `kern_clock.c:334` | as 4.4BSD |
| NetBSD | `lib/libc/gen/sleep.c:45-56`: `nanosleep`, returns `rmt.tv_sec` | `subr_time_arith.c:195-214`: `itimerfix` and `itimespecfix` have no upper bound; `tstohz` and `tvtohz` clamp to `INT_MAX` ticks (:131-153); `nanosleep1` (`kern_time.c:448-481`) re-reads the clock after each `EWOULDBLOCK` and sleeps the remainder | sleeps the whole request |
| OpenBSD | `sleep.c:34-46`: `nanosleep`; zero remainder on success | `kern_clock.c:158-198`: `tvtohz` clamps to `INT_MAX`; `itimerfix`, `kern_time.c:733-745`, rejects only values above `UINT_MAX` s | the kernel's `nanosleep` loop was not opened; the clamp is read |
| FreeBSD | `sleep.c:43-64`: a request above `INT_MAX` returns `seconds - INT_MAX + __sleep(INT_MAX)`, which sleeps `INT_MAX` and reports the rest as unslept; a non-`EINTR` failure returns `seconds` ("best guess"); `EINTR` returns the remainder rounded up | `kern_clock.c:592-628`: `tvtohz` returns `INT_MAX` for `tv_sec >= tick_seconds_max`; `itimerfix`, `kern_time.c:1020-1029`, has no upper bound | sleeps `INT_MAX` s (68 years), returns 2,147,483,648 |
| DragonFly | `sleep.c:42-62`, the FreeBSD form | `kern_clock.c:1176-1203`: `tvtohz_high` clamps to `INT_MAX`; `kern_time.c:1034-1041`, no upper bound | as FreeBSD |

Reading across: the select and `setitimer` generations (2.11BSD, 4.4BSD and their descendants) validate against a fixed bound and fail on a larger request; the modern trees remove the bound and saturate the tick count instead, with NetBSD adding a re-read loop in the kernel and FreeBSD and DragonFly a chunk in libc. No surveyed tree sleeps UINT_MAX seconds in one call; the three that return normally do it by a loop or by reporting the unslept part.

Origin of 100,000,000. The constant is in 4.4BSD-Lite2 (`kern_time.c:345`), LiteBSD (:369), RetroBSD (:254) and the first commit of the 2.11BSD tree (patch 431 import, `a88c5bc`). The NetBSD tree's history reaches further: its initial import of 386BSD 0.1 (`61f282557f0`, dated 1993-03-21) holds `itimerfix` with `tv_sec > 100000000` at `sys/kern/kern_time.c:290`, in the form the later trees copy. `git log -S100000000 -- sys/kern/kern_time.c` in the FreeBSD and OpenBSD trees of the workspace lists one unrelated commit each (a version bump and a driver change): those trees are squashed and say nothing about the origin. The 386BSD import is the earliest source available; the reason is not written beside it. Inference [I]: 100,000,000 s keeps `time.tv_sec + it_value.tv_sec` inside a signed 32-bit count for any clock below 2^31 - 10^8 s (18 November 2034), which a 1990s kernel with a signed clock needed. Falsifier: a source before 386BSD 0.1 or a mailing-list text giving another reason; neither is in the workspace. The modern trees' removal fits the inference: each moved to a 64-bit `time_t` and a saturating tick conversion. NetBSD also removed a sibling bound, `atv.tv_sec > 1000000000` in `nanosleep`, as a "bogus bounds check" in 2003 (`446a984305b`, PR/23613).

## 3. First principles for this kernel

Arithmetic, from the source.
- HZ is 1000 (`sys/arch/rp2040/include/machparam.h:44`).
- `hzto()` (`sys/kern/kern_clock.c:424-455`) takes `sec = wall_interval(deadline, now)`. For `sec <= 0x7fffffff / 1000 - 1000 = 2,146,483` it computes whole milliseconds and divides by the tick; up to `0x7fffffff / hz = 2,147,483` it computes `sec * hz`; above that it returns `0x7fffffff`.
- `select1()` (`sys/kern/sys_generic.c:405-515`) validates with `itimerfix()`, adds the clock, calls `tsleep()` with `timo` ticks, and on `EWOULDBLOCK` sets `error = 0` and finishes without testing `wall_reached()` again. A timeout the tick count cannot carry therefore ends early with a return of 0.
- `tsleep()` (`sys/kern/kern_synch.c:121-128`) returns `EINVAL` for `timo > INT_MAX`; `timeout()` (`sys/kern/kern_clock.c:354-376`) takes an `int` tick count and `c_time` is an `int` increment (`sys/sys/callout.h:23`), so one callout carries at most 0x7fffffff ticks. `hzto()` never returns more.
- `wall_interval()` (`sys/sys/time.h:90-102`) saturates at LONG_MAX, so a larger distance is held, not wrapped.

Where a bound is necessary. The tick count must fit 31 bits: 2,147,483 s at HZ 1000. Nothing else in the path needs a bound: the wall clock arithmetic is unsigned modulo 2^32 and saturating. 100,000,000 is above the necessary bound by a factor of 46 and below the width of a 32-bit `tv_sec` by a factor of 21, so it serves neither role; it is an inherited constant.

The width. `lib/libc/Makefile:40` builds libc at `_TIME_BITS=32`: `tv_sec` is a signed 32-bit `long`, and the layout the kernel reads (`struct __timeval32`, `sys/sys/time.h:20-23`) is the same signed `long`. A request above LONG_MAX therefore arrives negative, and `itimerfix()` refuses it through its `tv_sec < 0` test. (The kernel's own `struct timeval` and the `timespec` layout hold `utime32_t`, unsigned; an unsigned value above LONG_MAX would fail the `> 100000000L` test instead. The source gives one refusal on either reading.) A libc that must express UINT_MAX seconds in this field has to split the request regardless of what the kernel accepts. This makes the libc loop necessary, whatever the kernel does.

Callers of `select()` that could hit the same refusal.
- `usleep()` (`lib/libc/gen/usleep.c:25-33`): `micros` is a `long`, so `tv_sec` is at most 2,147 s.
- `getty` (`libexec/getty/subr.c:375, 381`), `calc`, `picotop`, `pforth`, the games, `uucp`, `virus`: fixed or small timeouts read from constants and sub-second intervals.
- No shell `read -t` exists in the tree (`rg 'select\s*\(' --glob '*.c'` over `bin sbin usr.bin usr.sbin libexec lib games` lists the callers; none passes a user-sized seconds count).
- `/bin/sleep` (`bin/sleep/sleep.c`) passes an `int` parsed from `argv` to `sleep()`, so it is the user-visible case of finding 1.
- `setitimer(ITIMER_REAL)` and `alarm()` share `itimerfix()`; `alarm(n)` above 100,000,000 returns `(unsigned)-1` (`lib/libc/gen/alarm.c`). This is a separate defect with the same cause and is not repaired here.

Where to repair. Three places were measured or reasoned.

| Repair | Kernel text | Libc | Fixes | Leaves |
|---|---|---|---|---|
| Libc chunk loop with the stall guard (chosen) | 0 | +48 to +52 B per a.out program (measured; +52 B in `sleep.o` by object delta) | `sleep()` for every unsigned request; `/bin/sleep` | a `select()` caller with a timeout above 100,000,000 still gets `EINVAL`; none exists |
| Kernel: drop the 100,000,000 test from `select1()` | 0 B (measured, four kernels) | the loop is still required at the 32-bit view | `select()` accepts any timeout | the early return past 2,147,483 s |
| Kernel: drop the test and re-read the clock after `EWOULDBLOCK` | +16 B (PICO and PICOCALC, +16 on all four) | the loop is still required for requests above LONG_MAX | `select()` is correct for every duration the field holds | |
| Both | +16 B | as the loop | | |

The kernel variants were built in a scratch clone on the WSL build host (`arm-none-eabi-gcc` 14.2.1 20241119) and compared on PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM against baselines of 100819, 90201, 121163 and 123691 bytes of text: `select-nobound` 0, 0, 0, 0; with the re-read 16, 16, 16, 16. `bss` and free RAM did not move. Kernel text and RAM are both tight budgets, the libc loop is needed in every variant, and no caller needs the kernel change, so the repair is libc-only.

## 4. The repair

`lib/libc/gen/sleep.c` is now a C17 prototype definition with this behavior.
1. `sleep(0)` returns 0 without reading the clock.
2. The first clock reading `f` is taken once.
3. Each pass sleeps `min(remaining, 1,000,000)` seconds in one `select()`, then reads the clock. The elapsed time is the difference modulo 2^32 (unchanged from the 2038 repair), so a clock set back reads as a large elapsed time and returns 0, as before.
4. The pass repeats while `select()` returned 0 and time remains; a nonzero result (a signal, `EINTR`, or any failure) ends the loop and the unslept part is returned.

The 1,000,000 s chunk is the largest power of ten inside the exact-millisecond range of `hzto()` (2,146,483 s), under the saturated count at any HZ up to 1000, and inside a signed 32-bit `tv_sec`. A request of 1,000,000 s or less is one `select()` and two clock reads, as before.

Behavior changes: requests above 100,000,000 s sleep instead of returning at once; requests above 2,147,483 s sleep their full length instead of 24.9 days; a failed `select()` ends the sleep instead of being read as a completed one with the clock as arbiter (the return is the same unslept part).

### Calibration

`tests/libc_contracts/sleep_contract_test.c` keeps its eight replay cases (the interrupted ones now return -1 from the stub, as the kernel does) and adds seventeen cases against a model of the kernel's limits: a clock that moves only while `select()` sleeps, `EINVAL` above 100,000,000 s or below zero, saturation at 2,147,483 s, a signal at a chosen second and a failure at a chosen call. Each case checks that no refusal reached the model, that no timeout passed the saturated count, that the unslept return equals the request minus the time slept, and the outcome-specific condition (full sleep, signal, failure). A call counter of 20,000 stops a spinning loop.

| Subject | Result |
|---|---|
| `sleep.c` on main, native and `-m32` | 10 of 17 kernel cases fail (the rollover cases pass on main: it returns after one call), 8 of 8 replay cases pass |
| repaired `sleep.c`, native and `-m32` | all pass |
| `sleep_unchunked_bad.c` (the single-select function), native | fails the same 10 cases: variant `libc.sleep.unchunked-negative`, expected exit 1 |
| `sleep_signed_bad.c` (signed clock compare, chunked), `-m32` | fails the 2038 and UINT_MAX-across-2^31 cases: variant `libc.sleep.negative` |
| mutant: chunk 100,000,000 | fails the saturated-count assertion |
| mutant: loop ignores the select result | fails `interrupted sleep` |
| mutant: stall detection removed, native and `-m32` | the model's call counter stops the spin: "select() was called without bound" |
| mutant: the modulo-2^32 mask removed, native | fails the wrap cases (at `-m32` the mask is the identity and the mutant passes: the native run owns that mutant) |

### Review round: the 2106 rollover and signal atomicity

A review found two defects in the first form of the loop; both held against the source.

Rollover. `select1()` forms the deadline with `timevaladd()`, whose seconds are `wall_advance()` (modulo 2^32), and compares it with `wall_reached()`, an unsigned compare (`sys/kern/sys_generic.c`, `sys/kern/kern_time.c:419-424`, `sys/sys/time.h:100-112`). A deadline that wraps below the clock reads as already due, so within one timeout of the count's last value (7 February 2106) `select()` returns 0 without sleeping; `hzto()` agrees, since `wall_interval()` reads the wrapped deadline as far in the past. The first form of the loop would then call `select()` until the clock itself wrapped. The kernel model now applies the same rule (a deadline above 0xffffffff returns 0 with the clock unmoved), and the loop stops when two consecutive `select()` calls return 0 without the second count moving: a legitimate `select()` of one second or more cannot leave the count unmoved twice. The rollover cases return the unslept part after two calls, as the single `select()` of earlier versions returned it after one. The model also made the earlier "clock wraps across chunks" success case impossible, which is the kernel's behavior: no sleep can cross the rollover. The kernel's deadline comparison is not repaired here (a signed 31-bit window would limit requests the saturating `wall_interval()` now carries); it is a defect for 2106, listed below.

Signals. Between one `select()` returning 0 and the next entering the kernel a handler can run without producing `EINTR`; the sleep then continues to the end of the next chunk, at most 1,000,000 s after the signal. The single `select()` of earlier versions has the same window once, between its first `gettimeofday()` and the `select()`, bounded by the whole request. Both the kernel and libc have the pieces for a race-free form: `pselect` is syscall 65 (`sys/kern/syscalls.master:67`, `select1()` with a mask) and the libc wrapper is `lib/libc/gen/time64_pselect.c`; a form would block all signals with `sigprocmask()`, wait with `pselect()` and the saved mask, and restore the mask on exit. It was prototyped for size only. Its cost does not fit a tight budget for a window of a few instructions, so the shipped function keeps the window, and the code comment and this note state it and its bound. A boundary-signal regression case cannot pass without the race-free form, so none is added.

Object size of `sleep.o` (text bytes, `arm-none-eabi-gcc` 16.2.0, `-Os -mcpu=cortex-m0plus`, relative numbers): main 72; chunk loop 96 (+24); chunk loop with stall detection 124 (+52); chunk loop with stall detection, `sigprocmask()` and `pselect()` 168 (+96, before the two library members it pulls in for programs that do not already link them).

### Size

Per program, from clean builds of main and of the final repair on the build host (`arm-none-eabi-gcc` 14.2.1 20241119, `bmake MACHINE=rp2040 build`, bytes of the built file): 65 of 314 built files differ. 52 gain 48 bytes and 5 gain 52 (the a.out programs, for example `bin/sleep/sleep`); 8 gain 64 (the ELF intermediates). The loop without the stall guard measured 16 to 24 bytes per a.out program; the guard adds the rest. The kernel is unchanged: the four PICO and PICOCALC kernels link at 100819, 90201, 121163 and 123691 bytes of text before and after, and RAM is untouched.

### Gates

Exit codes read directly from the command, logs retained in the working session's scratch space and on the build host (not in the repository).

| Gate | Where | Exit |
|---|---|---|
| `check-libc-sleep` | build host, native and `-m32` | 0 (four variants pass: native, ILP32, both negatives) |
| `check-libc-host-contracts` | build host | 0 (140 variants pass, the four sleep variants among them) |
| `check-c17-source-denominator`, after `--refresh-classification` and `--sync-paths --add-path` | build host | 0 |
| `check-test-execution`, `check-repository-coverage`, `check-lint` | build host | 0, 0, 0 |
| `check-changed-text`, `check-changed-comments`, range `4b3eaf6c..HEAD` | build host | 0, 0 (three advisories for comment length) |
| `bmake MACHINE=rp2040 build`, main and repair | WSL build host | 0 and 0 |
| `check-c17-compile-profiles` | WSL build host | 0 |
| `logged-distribution`, `logged-flash`, `check-renode` on the first form of the repair | WSL build host | 0, 0, 0 (not rerun for the stall guard) |
| `bmake MACHINE=rp2040 build` and `check-c17-compile-profiles` on the final form | WSL build host | 0, 0 |

`check-renode` boots the PICO_UART kernel of the distribution on Renode's third-party peripheral models to the login prompt; it shows that the linked programs (`init`, `getty`, `login` and the others that call `sleep()`) start with the new function. It did not trace a `sleep()` call, so it is boot evidence, not a behavior test of the loop. The loop's behavior is established by the kernel-model gate above, which runs on the host with stand-in system calls.

Not run: a board run, which this task leaves untouched; `sleep 100000001` and a 24.9-day sleep on silicon; `check-ucall-qemu` and the other gates that do not read `sleep.c`. The branch is based on `origin/main` 4b3eaf6c; `check-libc-sleep` and the denominator gates need a rerun after a rebase that moves `tools/c17/source-denominator.json`.

## 5. Residual decisions

0. **Repair the kernel deadline compare at the 2106 rollover?** Options: (a) leave it, 0 bytes, the libc loop stops cleanly and every select() deadline within one timeout of the rollover returns at once, a defect 80 years out; (b) make `select1()` compare a signed 31-bit distance, a few bytes of text, and a change to what `wall_reached()` promises. Recommended: (a) now and a ledger row.

1. **Also repair `select1()` in the kernel?** Options: (a) no, 0 bytes, the early return past 2,147,483 s stays reachable only by `select()` callers other than `sleep()` (none known); (b) drop the bound and re-read the clock after `EWOULDBLOCK`, +16 bytes of text, `select()` is correct for every duration. Recommended: (a); the kernel budget is tight and no caller needs it.
2. **Repair `alarm()` and `setitimer(ITIMER_REAL)` above 100,000,000 s?** Options: (a) document the bound in `alarm.3` and `setitimer.2`, 0 bytes; (b) clamp in `itimerfix()`, a few bytes of text and an ABI behavior change (a timer that cannot reach its date); (c) refuse and keep. Recommended: (a) now, (b) when a caller appears.
3. **Chunk size.** 1,000,000 s is safe to HZ 1000. A port with HZ above 1000 saturates below 2,147,483 s and the constant needs a derived form. Options: (a) leave the constant, valid for every maintained port; (b) derive it from `HZ` at build time. Recommended: (a).
