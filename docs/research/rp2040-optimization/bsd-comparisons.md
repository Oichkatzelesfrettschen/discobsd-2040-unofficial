# BSD comparison notes for the RP2040 footprint program

## Method and limit

The comparison inspected pinned upstream source on 2026-11-11. It extracts
design patterns; it does not imply API, ABI or license-compatible code can be
copied unchanged. The target remains a single-core-owned ARMv6-M kernel with a
128 KiB kernel flash region and a generated compact syscall surface.

| Tree and revision | Relevant pattern | RP2040 conclusion |
| --- | --- | --- |
| [NetBSD `42ccfc40`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/sys/cpu_data.h) | `cpu_data` deliberately puts remotely touched/cache-hot fields first; `percpu` provides general storage | validates ownership and hot-field grouping, but its multiprocessor framework is too large for this use |
| [FreeBSD `845293b1`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/sys/pcpu.h) | `pcpu` anchors per-CPU state; its `sysent` retains rich audit and tracing metadata | use the grouping idea, not the framework or full dispatch record |
| [DragonFly BSD `805ca132`](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/805ca132aededa6ae4c8b3499a2d5c934d78fe56/sys/sys/globaldata.h) | `globaldata` and `mycpu` make one per-CPU base the route to scheduler and execution state | closest conceptual donor for an anchored hot-scalar bank; do not reserve a global register or import SMP machinery |
| [OpenBSD `e553a660`](https://github.com/openbsd/src/blob/e553a660c8378fc436bc88aea913355a75628d3c/sys/sys/systm.h) | compact, conventional `sysent` still co-locates shorts, flags and a full handler pointer | confirms that direct aligned pointers are the conservative dispatch control |
| [MidnightBSD `4430a4c7`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/sys/sysent.h) | FreeBSD-derived `pcpu` and rich `sysent` design | useful independent lineage check, not a smaller embedded representation |

NetBSD's `sysent` carries argument count/size, flags, a function pointer and
two DTrace identifiers. FreeBSD and MidnightBSD add audit, systrace and thread
metadata. DragonFly carries argument/result sizes and start/abort handlers.
OpenBSD carries count, size, flags and a handler. DiscoBSD's generated split
arrays are already smaller for this fixed ABI: one byte per argument count and
one naturally aligned full pointer per handler. Upstream richness is evidence
against recreating a general `struct sysent` here.

The dispatch records above are pinned in [NetBSD `systm.h`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/sys/systm.h),
[FreeBSD `sysent.h`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/sys/sysent.h),
[DragonFly BSD `sysent.h`](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/805ca132aededa6ae4c8b3499a2d5c934d78fe56/sys/sys/sysent.h),
[OpenBSD `systm.h`](https://github.com/openbsd/src/blob/e553a660c8378fc436bc88aea913355a75628d3c/sys/sys/systm.h)
and [MidnightBSD `sysent.h`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/sys/sysent.h).

FreeBSD and MidnightBSD NOR drivers also distinguish write/erase geometry
from ordinary read alignment: sector erasure and page programming constraints
do not justify over-aligning unrelated C objects. NetBSD's flash framework
exposes geometry and explicit synchronization/error boundaries. These support
the telemetry and contract approach, but none supplies evidence for this
tree's Dhara power-loss behavior; that must come from the local host oracle and
board interruption matrix. The inspected implementations are [FreeBSD
`mx25l.c`](https://github.com/freebsd/freebsd-src/blob/845293b16d501a15f8f93f9ca68c0f2002899d36/sys/dev/flash/mx25l.c),
[MidnightBSD `mx25l.c`](https://github.com/MidnightBSD/src/blob/4430a4c710a6436a5c1199ba32f31f100411ee16/sys/dev/flash/mx25l.c)
and [NetBSD `flash.c`](https://github.com/NetBSD/src/blob/42ccfc40a9883a626831d04ee5f4c14026ae8dad/sys/dev/flash/flash.c).

The selected kernel build files at these pinned revisions were not evidence
that their general kernels enable function/data sections plus linker garbage
collection. Section GC remains a local toolchain/linker experiment, not an
upstream-derived default.

## Transfer rule

Borrow invariants and test shapes before mechanisms:

- one owner and one well-known base for related hot state;
- naturally aligned direct function pointers as the correctness control;
- geometry reported separately from I/O semantics;
- explicit synchronization and error boundaries;
- configuration-specific instrumentation rather than permanent shipping
  overhead.

Reject an imported design when its dynamic allocation, SMP indirection,
tracing record or compatibility surface costs more than the bounded RP2040
problem it solves.
