# BSD comparison notes for the RP2040 footprint program

## Method and limit

The notes extract design patterns from pinned upstream source. They do not
imply that API, ABI or license-compatible code can be copied unchanged. The
revisions and URLs below were recorded when PR #435 inspected them on
2026-11-11; this record did not fetch them again. The target remains a
single-core-owned ARMv6-M kernel with a 128 KiB kernel flash region and a
generated system-call surface.

| Tree and revision | Pattern | RP2040 conclusion |
| --- | --- | --- |
| [NetBSD `42ccfc40`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/sys/cpu_data.h) | `cpu_data` puts remotely touched and cache-hot fields first; `percpu` provides general storage | validates ownership and hot-field grouping; the multiprocessor framework is too large here |
| [FreeBSD `845293b1`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/sys/pcpu.h) | `pcpu` anchors per-CPU state; its `sysent` retains audit and tracing metadata | the grouping idea applies; the framework and the full dispatch record do not |
| [DragonFly BSD `805ca132`](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/805ca132aededa6ae4c8b3499a2d5c934d78fe56/sys/sys/globaldata.h) | `globaldata` and `mycpu` make one per-CPU base the route to scheduler and execution state | closest donor for an anchored hot-scalar bank; no reserved global register and no SMP machinery |
| [OpenBSD `e553a660`](https://github.com/openbsd/src/blob/e553a660c8378fc436bc88aea913355a75628d3c/sys/sys/systm.h) | compact `sysent` still co-locates shorts, flags and a full handler pointer | direct aligned pointers are the conservative dispatch control |
| [MidnightBSD `4430a4c7`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/sys/sysent.h) | FreeBSD-derived `pcpu` and rich `sysent` | an independent lineage check, not a smaller embedded representation |

NetBSD's `sysent` carries argument count and size, flags, a function pointer
and two DTrace identifiers. FreeBSD and MidnightBSD add audit, systrace and
thread metadata. DragonFly carries argument and result sizes and start and
abort handlers. OpenBSD carries count, size, flags and a handler. DiscoBSD's
generated split arrays are smaller for this fixed ABI: `syscall_nargs`
(one byte per entry) and `syscall_handlers` (one naturally aligned pointer per
entry) in `sys/kern/init_sysent.c`. The upstream richness argues against
recreating a general `struct sysent` here.

Dispatch records: [NetBSD `systm.h`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/sys/systm.h),
[FreeBSD `sysent.h`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/sys/sysent.h),
[DragonFly BSD `sysent.h`](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/805ca132aededa6ae4c8b3499a2d5c934d78fe56/sys/sys/sysent.h),
[OpenBSD `systm.h`](https://github.com/openbsd/src/blob/e553a660c8378fc436bc88aea913355a75628d3c/sys/sys/systm.h)
and [MidnightBSD `sysent.h`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/sys/sysent.h).

FreeBSD and MidnightBSD NOR drivers separate write and erase geometry from
ordinary read alignment: sector erasure and page programming constraints do
not justify over-aligning unrelated C objects. NetBSD's flash framework
exposes geometry and explicit synchronization and error boundaries. These
support the telemetry-and-contract approach of `wear-recovery.md`; none of
them evidences this tree's Dhara power-loss behavior, which has to come from
the local host oracles and a board interruption matrix. The inspected sources
are [FreeBSD `mx25l.c`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/dev/flash/mx25l.c),
[MidnightBSD `mx25l.c`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/dev/flash/mx25l.c)
and [NetBSD `flash.c`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/dev/flash/flash.c).

## Transfer rule

Borrow invariants and test shapes before mechanisms:

- one owner and one well-known base for related hot state;
- naturally aligned direct function pointers as the correctness control;
- geometry reported separately from I/O semantics;
- explicit synchronization and error boundaries;
- configuration-specific instrumentation instead of permanent shipping
  overhead (the PICO and PICO_DIAG split, `fdd25393`, is this rule applied).

An imported design is rejected when its dynamic allocation, SMP indirection,
tracing record or compatibility surface costs more than the bounded RP2040
problem it solves.
