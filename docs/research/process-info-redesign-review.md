# Process information and kernel-memory boundary redesign

## Architecture summary

The redesign should make the kernel the sole interpreter of process residency and should return one bounded, pointer-free row for every selected process. [I] The current `ps` combines a raw `struct proc`, a later `struct user` read, and a stack read, so a short detail read removes the row and the reads can describe different states. [R bin/ps/ps.c:443] [R bin/ps/ps.c:901] The correct detail sources already exist: direct resident access, ring-folded `swap_with_buf_result()`, and bounded `swapram_uarea_prefix()`. [R sys/kern/kern_sysctl.c:901] [R sys/kern/vm_swp.c:17] [R sys/arch/rp2040/rp2040/swapram.c:495]

The recommended transport is a record form of `KERN_PROC` selected by an operation bit: `{ CTL_KERN, KERN_PROC, KERN_PROC_INFO | selector, argument, element_size, element_count }`. [I] The legacy pointer-bearing record stays ABI-compatible during migration, while the caller-sized new form follows NetBSD, OpenBSD, and OSF/1 precedent. [R sys/sys/sysctl.h:189] [R docs/research/kmem-options-bsd-corpus.md:151] [R docs/research/kmem-options-synthesis.md:34] Version one is 192 bytes: 80 scalar bytes, `comm[64]`, and `args[48]`, with grouped validity and detail status but no pointer, backing address, pool offset, or raw wait channel. [I]

Arguments should be captured at exec into `u_psargs[48]` as a space-joined, NUL-terminated argv prefix. [I] The recorded 972-byte `struct user` leaves 52 bytes before `UAREA_STACK_OFFSET` 1024, so the snapshot adds no user-area allocation or swap transfer. [R docs/research/kmem-options-bsd-corpus.md:41] [R sys/arch/rp2040/include/uarea_stats.h:21] POSIX permits initial arguments and implementation-defined truncation. [R susv4-2018/utilities/ps.html:534] Reusing `u_comm` destroys command-name semantics, while query-time stack reconstruction adds ring, pool, I/O, and race machinery. [R sys/sys/user.h:27] [R docs/research/kmem-options-bsd-corpus.md:328]

The walker should snapshot at most 16 `{slot, pid}` keys without sleeping, then build rows individually. [R sys/arch/rp2040/include/machparam.h:70] [I] Each row captures base fields and backing identity before detail work; a flash path revalidates identity before and after I/O, and any mismatch emits the base row with detail unavailable. [I] This removes the current list cursor across `geteblk()` and device wait, where exit can relink the process. [R sys/kern/kern_sysctl.c:778] [R sys/kern/kern_sysctl.c:906] [R sys/kern/kern_exit.c:96]

`ps` should use only this sysctl, install as mode 0755, and remove setgid kmem, `-k`, alternate swap, symbol lookup, and all memory-device opens. [I] The three devices remain 0640 root:kmem. [R distrib/rp2040/mi.rp2040:59] `mmrw()` should reject writes whenever `securelevel > 0`, and `/dev/swap` needs the same write rule at open and I/O time because its disk classification otherwise acts only at securelevel two. [R sys/dev/mem.c:44] [R sys/kern/sys_inode.c:590] [R sys/arch/rp2040/rp2040/conf.c:311] [I]

Crash dumps remain separate from `ps`: an SD kernel may arm a dedicated unmounted raw partition and stream a versioned, checksummed dump of kernel data, both user areas, SwapRAM metadata, and the complete pool through a polled writer. [R sys/arch/rp2040/rp2040/machdep.c:788] [R sys/arch/rp2040/rp2040/swapram.c:84] [I] Non-SD links should emit no dump path. [I] `savecore` should enforce post-copy free space, publish numbered files atomically, and clear the marker only after successful fsync. [R 4.4BSD-Lite2/sbin/savecore/savecore.c:528] [R 4.4BSD-Lite2/sbin/savecore/savecore.8:87] Existing ELFs measure 90,300--118,174 text bytes; the process redesign is estimated at +60--280 net bytes and the SD-only dump path at +400--800 bytes, with rebuild deltas and calibrated gates as falsifiers. [M] [I]

## 1. Invariants

1. Every allocated process selected at the initial bounded key pass produces exactly one row, including a resident, flash-swapped, pooled, zombie, or system process. [I] A detail failure clears validity bits and never removes that row. [I] This corrects `savcom()`, which returns zero after a short user-area read, and follows 4.4BSD's retained invalid row. [R bin/ps/ps.c:474] [R docs/research/kmem-options-bsd-corpus.md:120]
2. Resident detail comes directly from the process's user area, flash detail passes through the swap ring fold, and pooled detail passes through `swapram_uarea_prefix()`. [R sys/kern/kern_sysctl.c:901] [R sys/kern/vm_swp.c:37] [R sys/arch/rp2040/rp2040/swapram.c:487] User space never reconstructs those locations. [I]
3. A row is internally coherent against swap-out: the filler captures base fields and backing identity without sleeping, and any sleeping read is accepted only after identity revalidation. [I] `swap_image_commit()` publishes backing addresses together and clears `SLOAD|SLOCK` only after every byte reaches owned backing. [R sys/kern/vm_swap.c:696] A mismatch yields an incomplete row rather than a mixture of pre-swap proc fields and post-swap user fields. [I]
4. No bare process-list cursor survives a possible sleep. [I] The current loop advances through `p->p_nxt` around a helper that calls `geteblk()` and waits for I/O, while exit relinks `allproc` to `zombproc`. [R sys/kern/kern_sysctl.c:778] [R sys/kern/kern_sysctl.c:906] [R sys/kern/kern_exit.c:96]
5. The record exports values, never kernel addresses, pointers, backing block numbers, pool offsets, or raw wait channels. [I] The legacy `kinfo_proc` directly embeds `struct proc` and `e_paddr`, so enlarging it would preserve the wrong boundary. [R sys/sys/sysctl.h:202]
6. The transport applies process visibility and argument visibility before copying the row, while raw-device membership is irrelevant to `ps`. [I] NetBSD and OpenBSD separately authorize process records, arguments, environments, and pointer exposure. [R docs/research/kmem-options-bsd-corpus.md:153] [R docs/research/kmem-options-bsd-corpus.md:195]
7. Record storage and work are bounded by caller length, `element_count`, `NPROC`, a 192-byte row, and one `DEV_BSIZE` detail read. [I] The fixed process capacity is 16 and a SwapRAM prefix request rejects a length beyond the stored user-area segment. [R sys/arch/rp2040/include/machparam.h:70] [R sys/arch/rp2040/rp2040/swapram.c:501]
8. The first fields are `version` and `size`, new fields append at the end, the kernel zero-initializes the complete current record, and a caller-sized copy never interprets an unknown tail. [I] NetBSD and OpenBSD already use caller-supplied element size and count for process records. [R docs/research/kmem-options-bsd-corpus.md:153] [R docs/research/kmem-options-bsd-corpus.md:195]
9. Zombies preserve pid, parent, exit state, and the `defunct` presentation while marking overwritten live-union fields unavailable. [I] Exit overwrites the live union after moving the process to `zombproc`, and POSIX requires an exited, unreaped process to be marked defunct. [R sys/sys/proc.h:38] [R susv4-2018/utilities/ps.html:437]
10. System processes preserve their row and command identity but carry explicit unavailable argument and user-area fields when no meaningful argv exists. [I] The current `ps` already special-cases pid 0 as `swapper`. [R bin/ps/ps.c:951]

## 2. Argument source

| Option | Byte and RAM cost | I/O, staleness, privacy, and failure |
|---|---|---|
| Exec-time `u_psargs[48]` | The copy loop costs an estimated 40--70 text bytes, and the 48-byte field occupies the recorded 52-byte gap without increasing the fixed 3072-byte user area. [R docs/research/kmem-options-bsd-corpus.md:41] [I] | Every residency path already obtains the prefix, so a query adds no stack I/O. [I] The value is intentionally stale after argv self-modification, world-readable only under the selected policy, and always NUL-terminated after truncation. [I] POSIX permits an initial or modified argument view and further implementation-defined truncation. [R susv4-2018/utilities/ps.html:534] |
| Reuse `u_comm[64]` | It adds no bytes or I/O. [I] | It collapses command identity and arguments into one field, so `comm` can no longer retain the existing exec-time command name. [R sys/sys/user.h:27] It fails independently useful `comm` and `args` output. [I] |
| Kernel stack read at query time | Resident and flash tail readers plus a pooled stream-tail decoder are estimated at 400--550 text bytes, with up to 2048 bytes of temporary argument inspection. [R docs/research/kmem-options-bsd-corpus.md:328] [I] | It can reflect current argv, but it adds sleeping flash I/O, ring-seam handling, full compressed-stream decoding, and another identity race. [I] A read failure must fall back to `comm`. [R susv4-2018/utilities/ps.html:439] |
| Command name only | It adds no storage and uses the existing `u_comm`. [R sys/sys/user.h:27] | It is deterministic and private, but it makes `args` and full `-f` output unavailable for every process. [I] The standards allow a fallback only when arguments cannot be determined, rather than as the normal result of a design that already has room to retain them. [R susv5-html/utilities/ps.html:347] [I] |
| Snapshot plus live resident read | It adds the snapshot and preserves the live-stack code for one state. [I] | Resident rows change with argv while swapped rows do not, so output semantics depend on scheduler residency and a failed live read needs the snapshot anyway. [I] The additional mechanism brings no conformance benefit because initial arguments are permitted. [R susv5-html/utilities/ps.html:407] |
| Separate per-process heap cache | FreeBSD uses a referenced `p_args` object and falls back to a vmspace read when absent. [R docs/research/kmem-options-bsd-corpus.md:240] | This port would add allocation, lifetime, and per-process RAM machinery to retain at most 48 useful bytes already available in each fixed user area. [I] |
| User-set process title | DragonFly can return a user-mapped title before its exec-time cache. [R docs/research/kmem-options-bsd-corpus.md:281] | A new mutation API, ownership rule, and copy path are disproportionate to the required `ps` result and can be added later without changing the record prefix. [I] |

The recommendation is `u_psargs[48]`, containing at most 47 printable argument characters plus NUL and replacing embedded NUL separators with single spaces. [I] Exec should clear the array before filling it so a zero-argument or failed exec cannot expose prior bytes. [I] Fork inherits the parent's snapshot with the rest of the user area, and the next successful exec replaces it. [R sys/kern/kern_fork.c:120] [I]

Argument visibility is a policy decision rather than a transport property. [I] The recommended default is to expose `args` to the process owner and root and return `comm` with the argument-valid bit clear to other callers, preserving every row. [I] This is stricter than NetBSD's world-readable arguments and follows the separate argument-authorization mechanism found there. [R docs/research/kmem-options-bsd-corpus.md:166]

## 3. Record transport

Extending the existing `KERN_PROC` handler with an information operation reuses all current filters and one list walker while preserving the old ABI when the operation bit is absent. [R sys/kern/kern_sysctl.c:788] [I] A replacement `machdep.psinfo.<pid>` node would require one call per pid, cannot provide a single bounded membership snapshot, and would keep a generic process contract under RP2040-specific headers. [R sys/arch/rp2040/rp2040/sysctl.c:229] [I] Procfs would add a vnode tree, lookup lifetime, text formatting, and stale-directory rules; the reviewed Minix procfs alone comprises 1703 C lines and still delegates process truth to sysctl. [R docs/research/kmem-options-minix-corpus.md:29] [I]

The proposed full MIB has six integers: `{ CTL_KERN, KERN_PROC, KERN_PROC_INFO | selector, argument, element_size, element_count }`. [I] Existing selectors `ALL`, `PID`, `PGRP`, `TTY`, `UID`, and `RUID` retain their values. [R sys/sys/sysctl.h:191] The operation rejects an element size below four bytes, caps count at `NPROC`, copies `min(element_size, 192)` bytes per row, advances by the caller's element size, and returns `ENOMEM` with the completed byte count when the supplied count is short. [I]

The following complete block is an inferred version-one C17 design. [I]

```c
/* Every declaration and assertion in this proposed block is [I]. */
struct kinfo_proc_info {
    uint16_t version;          /* 0: record contract version */
    uint16_t size;             /* 2: kernel record size */
    uint32_t valid;            /* 4: field-group validity mask */
    uint32_t flags;            /* 8: public process flags */
    uint32_t start_sec;        /* 12: start time */
    uint32_t user_ticks;       /* 16: process user CPU ticks */
    uint32_t system_ticks;     /* 20: process system CPU ticks */
    uint32_t pcpu_frac;        /* 24: documented fixed-point fraction */
    uint32_t text_bytes;       /* 28: text extent */
    uint32_t data_bytes;       /* 32: data extent */
    uint32_t stack_bytes;      /* 36: stack extent */
    uint32_t rss_bytes;        /* 40: resident bytes, when meaningful */
    uint32_t wait_id;          /* 44: stable wait-reason id, never address */
    uint16_t pid;              /* 48 */
    uint16_t ppid;             /* 50 */
    uint16_t pgid;             /* 52 */
    uint16_t sid;              /* 54 */
    uint16_t ruid;             /* 56 */
    uint16_t euid;             /* 58 */
    uint16_t rgid;             /* 60 */
    uint16_t egid;             /* 62 */
    uint16_t tty_dev;          /* 64 */
    uint16_t tty_pgid;         /* 66 */
    int16_t priority;          /* 68 */
    int16_t nice;              /* 70 */
    uint8_t state;             /* 72 */
    uint8_t cpu;               /* 73 */
    uint8_t residency;         /* 74 */
    uint8_t detail_status;     /* 75 */
    uint32_t reserved;         /* 76: must be zero */
    char comm[64];             /* 80 */
    char args[48];             /* 144 */
};

_Static_assert(offsetof(struct kinfo_proc_info, comm) == 80,
    "process command offset is ABI");
_Static_assert(offsetof(struct kinfo_proc_info, args) == 144,
    "process argument offset is ABI");
_Static_assert(sizeof(struct kinfo_proc_info) == 192,
    "process information record is 192 bytes");
```

`valid` should group identity, credentials, scheduling, times, sizes, terminal, command, arguments, and wait reason rather than allocate a bit per scalar. [I] `residency` should enumerate resident, flash, pool, zombie, and system, while `detail_status` should enumerate complete, unavailable, changed, I/O error, and policy-hidden. [I] Public `flags` should mask internal or address-revealing bits through an explicit allow-list. [I] `sid` remains invalid until this 2.11-derived kernel has a real session identifier, because `KERN_PROC_SESSION` is explicitly absent. [R sys/sys/sysctl.h:194]

## 4. Read paths

The first pass walks `allproc` and `zombproc` without any allocation or sleep and stores at most 16 `{slot_index, pid}` keys. [R sys/kern/kern_sysctl.c:775] [R sys/arch/rp2040/include/machparam.h:70] The second pass resolves each fixed slot, confirms the pid, snapshots base fields and the residency identity into one local record, and never holds the list cursor during detail work. [I]

- A resident row copies the required `struct user` fields directly from `p_addr` while the kernel remains in the non-sleeping section. [R sys/kern/kern_sysctl.c:901] The base snapshot and direct copy therefore observe one kernel execution interval. [I]
- A flash row acquires a buffer, revalidates the slot and saved identity, reads exactly `DEV_BSIZE` through `swap_with_buf_result(bp, p_addr, bp->b_addr, DEV_BSIZE, B_READ)`, and revalidates again after the sleeping I/O. [R sys/kern/vm_swp.c:17] [I] That routine folds a logical block at or above `m_ring_end`, splits a seam-crossing transfer, and rejects an error or residual. [R sys/kern/vm_swp.c:37] [R sys/kern/vm_swp.c:53]
- A pooled row calls `swapram_uarea_prefix()` for the required prefix; the decoder verifies the live entry and length and returns success only for an exact expansion. [R sys/arch/rp2040/rp2040/swapram.c:495] This path performs bounded decompression and does not need a permanent staging buffer. [R sys/arch/rp2040/rp2040/swapram.c:487]
- A zombie row reads only the stable dead fields and marks live-union, user-area, argument, tty, and residency-detail groups unavailable. [R sys/sys/proc.h:38] [I]
- A system row uses stable proc fields and a resident command when one exists, but pid 0 receives the documented `swapper` fallback and no fabricated argv. [R bin/ps/ps.c:951] [I]

A flash identity mismatch should trigger at most one restart from a fresh base snapshot for the same slot and pid; a second mismatch produces the original base row with detail unavailable. [I] Bounded retry prevents swap churn from turning `ps` into an unbounded kernel operation. [I] A vanished or reused slot still produces the selected pid with `detail_status=changed`, which preserves the initial row-count contract without attributing new-process data to it. [I]

## 5. `ps`

The new `ps` should issue only the record sysctl and should delete `openfiles()`, `knlist()`, `getcmd()`, raw proc-table reads, `-k`, and the optional swap operand. [R bin/ps/ps.c:333] [R bin/ps/ps.c:638] [R bin/ps/ps.c:901] [I] Its image mode becomes 0755 root:wheel or root:root with no saved kmem group. [I]

The POSIX parsing layer should support `-a`, `-A`, `-d`, `-e`, `-f`, `-g`, `-G`, `-l`, `-n`, `-o`, `-p`, `-t`, `-u`, and `-U`; SUSv5 also requires `-w`. [R susv4-2018/utilities/ps.html:74] [R susv5-html/utilities/ps.html:108] `-n namelist` should be accepted and documented as having no effect on the live sysctl path because no namelist participates in that path, while `-k` and alternate memory operands disappear. [I] Existing compact BSD flags such as `ax`, `l`, `u`, `v`, and `x` may remain as a separate compatibility grammar, but a leading dash selects the POSIX grammar and selection options combine by inclusive OR. [R bin/ps/ps.c:807] [R susv4-2018/utilities/ps.html:161] [I]

The required POSIX `-o` fields are `ruser`, `user`, `rgroup`, `group`, `pid`, `ppid`, `pgid`, `pcpu`, `vsz`, `nice`, `etime`, `time`, `tty`, `comm`, and `args`. [R susv4-2018/utilities/ps.html:453] The record carries real and effective user and group IDs, process and parent IDs, process group, recent CPU fraction, text/data/stack byte counts, nice, start seconds, user/system ticks, tty device, command, and argument snapshot for those fields. [I] The long and BSD formats additionally use state, public flags, priority, CPU byte, terminal foreground group, residency, and wait reason validity. [R bin/ps/ps.c:493] [I]

`vsz` is the rounded-up sum of text, data, and stack bytes in 1024-byte units; `time` is user plus system ticks converted by the kernel clock rate; and `etime` is current time minus `start_sec`. [R susv4-2018/utilities/ps.html:493] [R susv4-2018/utilities/ps.html:501] [I] A field whose validity group is clear prints `-`, while a zombie prints `<defunct>` and an unavailable full argument prints `[comm]`. [R susv4-2018/utilities/ps.html:437] [R susv4-2018/utilities/ps.html:439] [R susv4-2018/utilities/ps.html:543] A field failure never suppresses the row. [I]

The 48-byte snapshot gives a deliberately truncated `args`; the manual must state the 47-character payload limit and the caller-visibility policy. [I] The `comm` field retains the exec-time argv[0] copy, and only `comm` and `args` may contain blanks. [R sys/kern/exec_subr.c:325] [R susv5-html/utilities/ps.html:405] Session-based `-d` and `-g` remain explicitly nonconforming until the kernel supplies a valid `sid`; the record reserves the field rather than inventing a session from `pgrp`. [R sys/sys/sysctl.h:194] [I]

## 6. Siblings

`w` opens `/dev/mem` and `/dev/swap`, then repeats the resident-or-linear-swap user-area read and drops a row after a short read. [R usr.bin/w/w.c:383] [R usr.bin/w/w.c:435] It should migrate to the same process record for pid, tty, times, command, and arguments, after which those device opens disappear. [I]

`fstat` opens all three devices, shifts `p_addr` by 9 for a swap offset even though RP2040 `DEV_BSIZE` is 1024, and has no pooled-image branch. [R usr.bin/fstat/fstat.c:84] [R usr.bin/fstat/fstat.c:90] [R usr.bin/fstat/fstat.c:507] Replacing its process identity lookup with the new record does not replace its need for `u_ofile`, `u_cdir`, and `u_rdir`, which are pointers in `struct user`. [R sys/sys/user.h:61] [I] A complete migration therefore needs a separate pointer-free file-descriptor record comparable to `KERN_FILE`; until then `fstat` remains an unshipped root diagnostic, and its `DEV_BSIZE` and pool defects should be corrected if it is maintained. [I]

Both `w` and `fstat` treat every nonresident `p_addr` as a flash block, so a pooled process with deliberately zero `p_addr` reads swap block zero. [R usr.bin/w/w.c:440] [R usr.bin/fstat/fstat.c:87] [R sys/arch/rp2040/rp2040/swapram.c:487] The generic record eliminates that classification defect for `w`; the future descriptor record must do the same for `fstat`. [I]

The RP2040 manifest ships standalone `ps` but does not ship `w`, `fstat`, `vmstat`, `iostat`, `pstat`, or `devupdate`, and `adminbox` contains no raw-memory diagnostic. [R distrib/rp2040/mi.rp2040:226] [R distrib/rp2040/mi.rp2040:385] Therefore no additional shipped executable consumes `/dev/kmem`, `/dev/mem`, or `/dev/swap`. [I] The source tree still contains raw-device consumers in `w`, `fstat`, `vmstat`, `iostat`, `pstat`, and `devupdate`, so future manifest additions require a new boundary review. [R docs/research/kmem-options-bsd-corpus.md:42]

## 7. Kernel-memory boundary

`mmrw()` applies the same address-range test to reads and writes and contains no securelevel check. [R sys/dev/mem.c:44] `openi()` refuses a new write open of mem or kmem at securelevel one, but that decision does not revoke a descriptor opened earlier. [R sys/kern/sys_inode.c:590] [I] `mmrw()` should therefore return `EPERM` for `UIO_WRITE` on minors 0 and 1 whenever `securelevel > 0`, before address validation or `uiomove()`. [I]

`init` lowers a positive level on entry to single-user mode and raises level zero to one on entry to multiuser mode. [R sbin/init/init.c:306] [R sbin/init/init.c:521] `PICOCALC.md` says init never raises it, so the document contradicts the executable source and must be corrected rather than treated as authority. [R sys/arch/rp2040/doc/PICOCALC.md:681]

`/dev/swap` block major 4 is classified as a disk, which makes a write open fail only at securelevel two. [R sys/arch/rp2040/rp2040/conf.c:311] It should reject `FWRITE` at securelevel one in `swopen()` and reject a write again in the actual block or character I/O path, so an old descriptor cannot overwrite live swap after the level rises. [R sys/dev/swap.c:63] [I] Temporary swap extents keep their existing explicit write authorization and receive the same I/O-time level check. [I]

`mmrw()` should not admit the two user-area regions for root merely to preserve `ps`. [I] The resident addresses sit outside the admitted kernel-data interval, and widening the interval would expose execution stacks while leaving ring, pool, and identity races unsolved. [R docs/research/kmem-options-bsd-corpus.md:31] Root diagnostics should use the process-aware record or an explicitly scoped future interface. [I]

## 8. Crash dumps

The present dump function is `nodump()`, and reboot calls the configured function only for `RB_DUMP` with a non-`NODEV` dump device. [R sys/arch/rp2040/rp2040/machdep.c:172] [R sys/arch/rp2040/rp2040/machdep.c:788] The generated configurations currently name flash swap as both `dumpdev` and `swapdev`, which is unsuitable because a dump would overwrite live swap and has no retained-storage contract. [R sys/arch/rp2040/compile/PICOCALC/swapunix.c:4] [I]

The conditional design should use these rules. [I]

1. A boot-time probe arms dumping only when an SD-enabled kernel finds the configured removable card, a dedicated raw partition type or label, sufficient sectors, and no mounted or swap use of that partition. [I] The current driver exposes bounded per-partition size and refuses unopened or out-of-range I/O. [R sys/arch/rp2040/dev/sd.c:316] [R sys/arch/rp2040/dev/sd.c:340]
2. Panic or an explicit reboot carrying `RB_DUMP` triggers the writer after normal sync has ceased. [R sys/arch/rp2040/rp2040/machdep.c:788] The target is an `sd0b`-style reserved raw partition, never a filesystem, the active flash swap, or the whole card. [I]
3. Panic-time code writes a fixed-endian header with magic, format version, kernel build identity, timestamp, total length, segment count, and header checksum, followed by a segment table and checksummed segments. [I] The SD path must be a bounded polled writer that does not sleep, allocate, take ordinary buffer-cache locks, or depend on interrupts after shutdown. [I]
4. Segments include kernel data and bss, both fixed 3072-byte user areas, the SwapRAM entry and segment metadata, and the complete configured pool when present. [R sys/arch/rp2040/include/uarea_stats.h:20] [R sys/arch/rp2040/rp2040/swapram.c:63] [R sys/arch/rp2040/rp2040/swapram.c:84] Pool metadata and bytes form one decoding unit. [I]
5. Non-SD configurations compile against `static inline` stubs whose constant false probe removes the call path and all persistent data. [I] A link map containing a dump writer in PICO or PICO_UART falsifies the zero-cost claim. [I]
6. `savecore` validates magic, version, build identity, segment bounds, and checksums; computes the complete output size; copies the dump and matching kernel to temporary numbered files; fsyncs files and directory; renames them; advances `bounds`; and only then clears the dump-valid marker. [I] The successful-copy-then-clear sequence follows 4.4BSD. [R 4.4BSD-Lite2/sbin/savecore/savecore.8:97]
7. The free-space rule is `available_bytes >= dump_bytes + kernel_file_bytes + reserve_bytes`. [I] It follows 4.4BSD's post-copy `minfree` check rather than a fixed 16 MB guess. [R 4.4BSD-Lite2/sbin/savecore/savecore.c:548] The recommended default reserve is 1024 KiB, configurable by a `minfree` file, because it leaves recovery room while remaining proportionate to RP2040 media and dump sizes. [I]
8. Core interpretation remains a future standalone debugger or `pstat`-style tool; `ps -M`, `ps -k`, and alternate-memory operands stay absent. [I]

## 9. Elegance and security

The claim that this redesign cannot be both elegant and secure is false for the process-listing scope. [I] One generic record, one residency-aware detail helper, one bounded walker, and one validity contract remove three user-space layout models and the setgid path. [I] The corpus converges on kernel-produced fixed records rather than user-space raw-memory reconstruction. [R docs/research/kmem-options-synthesis.md:24]

The first real tension is a single coherent row versus sleeping flash I/O. [I] Holding a bare list cursor is compact but unsafe, while copying every 192-byte row before I/O would consume 3072 bytes at `NPROC=16`. [I] The bounded key list plus per-row base snapshot and post-I/O identity check resolves the tension with about 64 bytes of key storage and one 192-byte work record. [I]

The second tension is argument privacy versus useful `ps -o args` output. [I] World-readable initial arguments match traditional BSD practice, while owner/root-only arguments reduce incidental disclosure. [R docs/research/kmem-options-bsd-corpus.md:362] The validity mask lets policy hide arguments without hiding the process, so the owner can change policy without changing the ABI. [I]

The third tension is a compact fixed record versus future fields. [I] Caller-sized copying, an explicit record size, grouped validity, zeroed reserved bytes, and append-only tails allow future growth without raw structures or per-version handlers. [I] A field that would expose an address remains absent rather than masked into an ambiguous zero. [I]

The fourth tension is securelevel enforcement versus root diagnostics. [I] Write-time denial prevents a descriptor opened at level zero from surviving the transition, while read-only root:kmem devices remain available for explicitly audited tools. [R sys/kern/sys_inode.c:603] [I] The process record removes the only shipped reason for a privileged diagnostic. [R distrib/rp2040/mi.rp2040:226] [I]

## 10. Byte budget

The measurements below come from read-only `arm-none-eabi-size` and `arm-none-eabi-nm -S` runs on the existing ELFs; no object was rebuilt. [M]

| Existing ELF or symbol | Measured size |
|---|---:|
| `PICO/unix.elf` | text 99,662; data 272; bss 43,136 bytes. [M] |
| `PICOCALC/unix.elf` | text 116,102; data 392; bss 55,584 bytes. [M] |
| `PICOCALC_PSRAM/unix.elf` | text 118,174; data 448; bss 55,736 bytes. [M] |
| `PICO_UART/unix.elf` | text 90,300; data 216; bss 18,440 bytes. [M] |
| `__sysctl` | 328 text bytes in every measured ELF. [M] |
| `sysctl_doproc` and `fill_eproc` | 268 and 32 text bytes in every measured ELF. [M] |
| `fill_from_u` | 224 bytes in PICO and both PicoCalc ELFs; 204 bytes in PICO_UART. [M] |
| `swap_with_buf_result` and `mmrw` | 232 and 176 text bytes in every measured ELF. [M] |
| `swapram_uarea_prefix` | 62 text bytes where present. [M] |
| `cpu_sysctl` | 920 bytes in PICO, 916 in both PicoCalc ELFs, and 532 in PICO_UART. [M] |
| `proc`, `u`, and `u0` | 1600, 3072, and 3072 bss bytes in every measured ELF. [M] |
| SwapRAM pool and table | `swapram_pool_mem` is 16,384 bytes and `sr_tab` is 384 bytes where present. [M] |

The generic record handler, fill logic, caller-size loop, and validity mapping are estimated at +180 to +320 text bytes. [I] Sharing the new detail helper with legacy `fill_from_u()` should recover 60--140 bytes, and retiring the machine-specific pooled record should recover 140--240 bytes inside `cpu_sysctl`. [I] The exec snapshot copy is estimated at +40--70 bytes, the mem and swap write-time guards at +20--40 bytes, and the net process redesign at +60--280 bytes. [I] A rebuilt per-symbol `nm` comparison and total `size` delta falsify each interval; a net delta above +320 bytes requires re-examining duplicated handlers before accepting the series. [I]

`u_psargs[48]` increases `sizeof(struct user)` by 48 bytes but adds zero allocated bytes per process because it occupies the fixed prefix before `UAREA_STACK_OFFSET`, and it adds zero flash or pool transfer bytes because swap already moves `USIZE`. [R sys/arch/rp2040/include/uarea_stats.h:20] [R sys/kern/vm_swap.c:692] The capacity `_Static_assert` is the layout falsifier. [R sys/kern/subr_capacity.c:32]

The process key list is estimated at 64 automatic bytes and the work record at 192 automatic bytes per sysctl invocation, with zero persistent per-process RAM. [I] The existing buffer cache supplies the one-block flash staging area. [R sys/kern/kern_sysctl.c:906] Stack-usage output and a sentinel test under the maximum 16-row query falsify the safety estimate. [I]

The SD-only dump writer is estimated at +400--800 text bytes and 32--64 persistent state bytes, with no full-dump RAM buffer because it streams segments. [I] PICO and PICO_UART should have zero linked dump symbols; PicoCalc rebuilt `size`, `nm`, and a forced undersized partition are the falsifiers. [I]

Removing `-k`, nlist, raw user-area, and stack reconstruction should shrink the `ps` executable, but no permitted existing kernel ELF measures that userland delta. [I] The linked `ps` a.out size before and after the later implementation is the falsifier. [I] The savings do not count against the 128 KiB kernel ceiling, but they recover root-image flash and reduce privileged code. [I]

## 11. Blast radius

The process-information kernel group comprises `sys/sys/sysctl.h` for the operation and record ABI, `sys/kern/kern_sysctl.c` for the bounded walker and shared detail fill, `sys/sys/user.h` for `u_psargs`, `sys/kern/exec_subr.c` for the snapshot, and `sys/kern/subr_capacity.c` for explicit layout assertions. [R sys/sys/sysctl.h:137] [R sys/kern/kern_sysctl.c:760] [R sys/sys/user.h:24] [R sys/kern/exec_subr.c:245] [R sys/kern/subr_capacity.c:32]

The libc group has no production-source change because `lib/libc/gen/sysctl.c` already forwards every non-`CTL_USER` MIB unchanged to `__sysctl()`. [R lib/libc/gen/sysctl.c:46] `tests/libc_sysctl/libc_sysctl_test.c` needs no new record behavior because `ps` owns element allocation and interpretation, while the kernel sysctl fixture owns copy-length semantics. [R tests/libc_sysctl/libc_sysctl_test.c:6] [I]

The RP2040 cleanup group comprises `sys/arch/rp2040/rp2040/sysctl.c`, `sys/arch/rp2040/include/cpu.h`, and `sys/arch/rp2040/include/swapram.h` to retire `machdep.swapram_psinfo` and its record. [R sys/arch/rp2040/rp2040/sysctl.c:229] [R sys/arch/rp2040/include/cpu.h:125] [R sys/arch/rp2040/include/swapram.h:87] `sys/arch/rp2040/rp2040/swapram.c` needs no process-record change because its bounded prefix API is already sufficient. [R sys/arch/rp2040/rp2040/swapram.c:495] [I]

The boundary group comprises `sys/dev/mem.c` for I/O-time securelevel enforcement, `sys/dev/swap.c` for open- and I/O-time write enforcement, and `sys/arch/rp2040/doc/PICOCALC.md` for the init contradiction. [R sys/dev/mem.c:44] [R sys/dev/swap.c:63] [R sys/arch/rp2040/doc/PICOCALC.md:681]

The crash-dump kernel group comprises `sys/arch/rp2040/rp2040/machdep.c`, `sys/arch/rp2040/dev/sd.c`, `sys/arch/rp2040/conf/files.rp2040`, the PicoCalc configuration inputs, the generated PicoCalc and PicoCalc-PSRAM Makefiles and `swapunix.c` files, and a new RP2040 dump-format header. [R sys/arch/rp2040/rp2040/machdep.c:172] [R sys/arch/rp2040/dev/sd.c:325] [R sys/arch/rp2040/conf/files.rp2040:98] [I] A new `sbin/savecore` source, Makefile, manual, top-level build registration, and an SD-closure manifest entry provide extraction without adding the tool to non-SD images. [R distrib/rp2040/mi.rp2040:70] [I]

The userland group comprises `bin/ps/ps.c`, `bin/ps/Makefile`, `include/psout.h`, and `share/man/man1/ps.1`; the binary-output structure must either become pointer-free or retire with the raw-output mode because it currently carries a backing address and wait-channel pointer. [R include/psout.h:16] [I] A later sibling commit comprises `usr.bin/w/w.c` and `share/man/man1/w.1`, and a separate descriptor-export project comprises `usr.bin/fstat/fstat.c` and its manual. [R usr.bin/w/w.c:383] [R usr.bin/fstat/fstat.c:507] [I]

The image group comprises `distrib/rp2040/mi.rp2040` for mode 0755 and conditional `savecore`, plus `tools/check_rp2040_account_image.py` for owner, group, mode, and absence of setgid. [R distrib/rp2040/mi.rp2040:226] [I]

The test group comprises `tests/kernel/sysctl_test.c` for record layout, filters, sizing, validity, revalidation, and residency fixtures; `tests/kernel/mem_span_test.c` plus new mutants for the write-time check; the swap-device ioctl or disk harness for old-descriptor writes; `tests/picocalc/sdstrat_test.c` plus a polled-dump harness; and savecore host tests for malformed headers, checksums, space, atomic publication, and marker clearing. [R tests/kernel/mem_span_test.c:2] [R tests/picocalc/sdstrat_test.c:19] [I]

The gate and documentation group comprises the root `Makefile`, `tools/renode/check-boot.py`, `sys/arch/rp2040/doc/TESTING.md`, `share/man/man3/sysctl.3`, `docs/research/STYLE-GUIDE.md` ledgers where required, and `tools/c17/source-denominator.json`. [R tools/renode/check-boot.py:265] [R sys/arch/rp2040/doc/TESTING.md:1732] [R docs/research/STYLE-GUIDE.md:288] [I]

## 12. Implementation plan

1. **Define the append-only process-record ABI.** Add the operation, enums, record, offsets, and static assertions without changing the legacy path. [I] The host sysctl gate accepts exact v1, smaller-prefix, larger-stride, every filter, zero-count, and short-count inputs; it rejects size below four, count above `NPROC`, unknown selectors, and a mutant that copies a pointer. [I]
2. **Capture bounded arguments at exec.** Add `u_psargs[48]`, clear it, and fill a space-joined prefix from the existing exec spool while preserving `u_stack` as the last member. [R sys/kern/exec_subr.c:285] [R sys/sys/user.h:96] The capacity and exec host gates accept zero, one, many, exactly fitting, and truncated argv inputs; they reject missing NUL, stale tail, environment leakage, and a 53-byte field mutant. [I]
3. **Build rows for resident and terminal states.** Implement the key snapshot, pointer-free base fill, resident detail, zombie and system handling, grouped validity, caller stride, and row preservation. [I] A host fixture with all five states is known-good; mutants that copy raw pointers, omit pid 0, omit a zombie, or drop an unavailable-detail row are known-bad. [I]
4. **Unify flash and pool detail reads.** Route flash through `swap_with_buf_result()`, pool through `swapram_uarea_prefix()`, revalidate identities around sleeps, and make legacy `fill_from_u()` a narrow consumer of the same helper. [R sys/kern/vm_swp.c:17] [R sys/arch/rp2040/rp2040/swapram.c:495] The ring-seam and SwapRAM host fixtures are known-good; the raw-`d_strategy`, no-fold, block-zero, no-revalidation, and row-drop mutants are known-bad. [I]
5. **Retire the machine-specific pool record.** Remove `CPU_SWAPRAM_PSINFO`, its structure, handler, and `ps` fallback only after the generic pool fixture passes. [R sys/arch/rp2040/rp2040/sysctl.c:238] A pool-backed pid is known-good through generic `KERN_PROC_INFO`; a binary or source reference to the retired node is known-bad. [I]
6. **Replace `ps` raw-memory reconstruction.** Consume the new record, add POSIX selection and `-o` formatting, preserve documented BSD compact output, print markers, and remove `-k`, alternate swap, nlist, stack reads, and device opens. [I] Host formatting fixtures cover every standard field and invalid group; the present `ps` source path and golden output with a missing resident row are known-bad. [R bin/ps/ps.c:443] [I]
7. **Remove the shipped privilege.** Change `/bin/ps` to mode 0755 and update the account-image checker while retaining the three devices at 0640 root:kmem. [R distrib/rp2040/mi.rp2040:59] [R distrib/rp2040/mi.rp2040:226] The generated image with ordinary `ps` and protected devices is known-good; the current 02751 `ps` and any widened device mode are known-bad. [I]
8. **Enforce securelevel at every write.** Add I/O-time mem/kmem checks and open- plus I/O-time swap checks. [I] The host fixtures open at level zero, raise to one, and require later writes to return `EPERM`; the unmodified drivers and mutants that check only open are known-bad. [R sys/dev/mem.c:44] [R sys/dev/swap.c:63]
9. **Require every boot process row.** Change the Renode stage to parse exactly one row each for pid 0, pid 1, `update`, the operator shell, and `ps` itself, reject duplicates and missing rows, and accept a marked detail field without losing its row. [I] The new kernel and userland are known-good; current `ps`, whose oracle checks only init and whose resident reads fail, is the calibrated known-bad input. [R tools/renode/check-boot.py:265] [R docs/research/kmem-options-synthesis.md:14]
10. **Migrate `w` without expanding the shipped image.** Replace its memory and swap reads with the generic record and add host fixtures for resident, folded, pooled, and unavailable detail. [I] The new record path is known-good; the existing block-zero pool and linear-fold readers are known-bad. [R usr.bin/w/w.c:435]
11. **Add the conditional dump writer.** Define the format, SD probe, polled writer, compile-time stubs, configuration, and host hardware model. [I] A valid dedicated partition and a complete checksum chain are known-good; mounted, swap, undersized, replaced-card, partial-write, bad-header, and non-SD linked-writer cases are known-bad. [I]
12. **Add atomic `savecore`.** Implement validation, threshold, temporary files, fsync, rename, bounds, and success-only marker clearing, then add the SD-closure manifest entry and manual. [I] A complete dump with enough post-copy space is known-good; checksum error, identity mismatch, low space, interrupted copy, and early-clear mutants are known-bad. [R 4.4BSD-Lite2/sbin/savecore/savecore.8:87] [I]
13. **Run the integration envelope.** The required later gates are the focused host and mutant suites, strict C17 and warning gates for touched units, `logged-build`, `logged-check`, `logged-ilp32`, `check-renode`, `check-renode-union`, PicoCalc SD tests, and linked `size`/`nm` comparison. [R sys/arch/rp2040/doc/TESTING.md:1732] [I] This review ran none of those gates because its brief forbids builds and Renode. [I]

## 13. Remaining owner decisions

1. **Argument visibility.** The recommended option is owner and root receive `args`, while other users receive `comm` and an argument-policy status without losing the row. [I] The alternatives are world-readable arguments, matching traditional BSD practice, or command-only output for every caller, which gives the strongest privacy and weakest `-f` result. [R docs/research/kmem-options-bsd-corpus.md:362] [I]
2. **Default crash reserve.** The recommended option is an administrator-overridable 1024 KiB post-copy reserve. [I] The alternatives are zero by default, which maximizes captures but can fill the filesystem, or a percentage policy, which costs more code and behaves poorly on very small partitions. [I]
3. **Sibling scope.** The recommended option is to land the shipped `ps` path and `w` migration in the same reviewed series, then design the separate descriptor export before changing `fstat`. [I] The alternatives are to stop after `ps`, leaving known defects in unshipped tools, or to include `fstat`, which expands this architecture into file-descriptor disclosure and authorization. [I]
4. **Legacy `KERN_PROC` lifetime.** The recommended option is to preserve it during `w` migration and mark it deprecated once all in-tree live callers use the pointer-free record. [I] Immediate removal breaks existing callers, while indefinite support retains a pointer-bearing ABI and two fill paths. [R sys/sys/sysctl.h:200] [I]
5. **Session conformance scope.** The recommended option is to ship the record with an invalid `sid` and document `-d` and `-g` as pending a separate session-model change. [R sys/sys/sysctl.h:194] [I] The alternatives are to add a real session identifier to the process model in this series, increasing per-process and job-control scope, or to derive a false session from process group, which should be rejected. [I]
