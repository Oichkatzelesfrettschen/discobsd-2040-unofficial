# Securelevel on a small device: mechanism, reference corpus, threat model, cost and design

Scope: the kernel securelevel of this tree (`origin/main` 4b3eaf6c), the same mechanism in eight reference trees, what it protects on a single-user RP2040 handheld or bare board, and what each part costs in kernel text and RAM. No kernel change accompanies this note; the measured variants live in a scratch clone and are discarded.

## Findings

1. The mechanism is the 4.4BSD one, 2.11BSD flavor, with two additions (live swap write refusal and a per-write memory-device check). It is four checks and one handler: file-flag changes, raw and mounted disk opens, `/dev/mem` and `/dev/kmem` writes, the live swap minor, and the `kern.securelevel` sysctl. The clock check is compiled out (`#ifdef NOTNOW`) and does not compile when enabled.
2. `init` raises level 0 to 1 on entering multiuser mode and lowers a positive level to 0 on entering single-user mode. The board reports `kern.securelevel=1` after boot. The documented contradiction is stale: the sentence "init never raises it" entered `PICOCALC.md` in commit 19c35974 and left it in 92dbbb66, and the research notes that still quote it cite `init.c` lines that moved.
3. The whole mechanism costs 560 to 664 bytes of kernel text (0.54 to 0.56 percent) and no measurable RAM. Level 2 alone costs 152 to 192 bytes, the level 1 open checks 280 to 336, the handler 40 to 48.
4. A root process can reach kernel privilege through `ucall(2)`, which no securelevel test guards. Level 1 therefore restrains honest interfaces only. Gating `ucall` at level 1 costs 18 to 26 bytes and is the single addition that changes what the level protects.
5. Recommendation: keep the runtime two-level model, add the `ucall` gate, leave the other candidates, and let the owner decide five points (the last section).

## 1. This tree, site by site

Variable and handler. `sys/sys/systm.h:38` declares `extern int securelevel`. `sys/arch/rp2040/rp2040/machdep.c:151-156` defines it, `-1` under `PERMANENTLY_INSECURE` and `0` otherwise. No configuration defines `PERMANENTLY_INSECURE` (search: `rg PERMANENTLY_INSECURE` over the tree outside `legacy/` finds the two machdep files only), so PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM all start at 0 in `.bss`. `nm -S` of the PICO kernel shows `securelevel` as a 4-byte `B` symbol.

`sys/kern/kern_sysctl.c:257-265` is the handler: any new value needs root (`kern_sysctl.c:102`, `!suser()`), and a value below the current level returns `EPERM` unless `u.u_procp->p_pid == 1`. Raising is free. The 2.11BSD patch 451 term (`&& securelevel > 0`) is absent.

| Site | Level tested | Action refused | Class |
|---|---|---|---|
| `sys/kern/ufs_fio.c:119-124` (`ufs_setattr`) | `> 0` | root changing a file whose flags hold `SF_IMMUTABLE` or `SF_APPEND` | file flags |
| `sys/kern/sys_inode.c:617` | `>= 2` | write open of a character device for which `isdisk(dev, IFCHR)` holds | raw disk |
| `sys/kern/sys_inode.c:625-632` | `>= 1` | write open of a character device whose block device is mounted (`ufs_mountedon`); write open of `/dev/mem` or `/dev/kmem` (`iskmemdev`) | raw disk, mem |
| `sys/kern/sys_inode.c:637-638` | `>= 2`, `>= 1` | the same two tests repeated under `bdevlock` (the check and the open are one critical section) | raw disk |
| `sys/kern/sys_inode.c:654, 667-668` | `>= 2`, `> 0` | write open of a block disk; any open of a mounted block device | raw disk |
| `sys/dev/mem.c:48-53` (`mmrw`) | `> 0` | each write to minor 0 or 1, because a descriptor opened at level 0 survives a later rise | mem |
| `sys/dev/swap.c:63-73` (`swap_live_write_refused`) | `> 0` | write open and every write transfer of minor 64, the live swap unit | swap |
| `sys/kern/kern_time.c:21-35` | `> 0` | set the clock backwards | compiled out |
| `sys/arch/rp2040/rp2040/conf.c:341-361` (`isdisk`) | | classifies the SD raw major (11) as a disk, and the flash, swap and SD block majors | helper |

Flag enforcement itself does not depend on the level. `IMMUTABLE|APPEND` refuse writes, links, unlinks and renames at every level (`ufs_syscalls.c:323, 414, 558, 618, 661, 740, 1323, 1394, 1860`; `ufs_fio.c:36, 135`; `vfs_vnops.c:142`; `sys_inode.c:184`); the level decides only whether root may clear a system flag. Kernel flags are not set in the shipped image (`rg 'chflags|schg|IMMUTABLE' distrib/rp2040 etc` finds none), so this class protects nothing unless the owner sets a flag.

The clock block cannot be enabled as written. Removing `#ifdef NOTNOW` fails with `-Wparentheses` in the `timercmp` macro of `sys/sys/time.h:50` under the kernel's `-Wall -Wextra -Werror`. The comment above it records the original reason for disabling it: ntpd steps the clock backwards.

What raises or lowers the level.
- `sbin/init/init.c:901-902`, `multiple()`: level 0 becomes 1. Level -1 stays.
- `sbin/init/init.c:680-681`, `single()`: a positive level becomes 0, so `fsck`, `newfs` and `umount` run unrestricted in single-user mode.
- `sbin/init/init.c:1020-1065`: `getsecuritylevel()` and `setsecuritylevel()` use `sysctl(CTL_KERN, KERN_SECURELVL)`, log the change at `LOG_ALERT`, and occupy 76 and 104 bytes of `init.o` text (`nm -S`).
- `etc/rc.rp2040` and `etc/rc` never call `sysctl kern.securelevel`; nothing in the shipped image sets level 2. The two values the board can hold are 0 (before `multiple()`) and 1 (after).
- Board observation: `kern.securelevel=1` in `docs/research/board-inventory-2026-09-15.txt:106`, measured on a PICO build of that date after boot.

The stale contradiction. `docs/research/kmem-options-bsd-corpus.md:35`, `kmem-options-synthesis.md:94` and `process-info-redesign-brief.md:89` record that `PICOCALC.md:683` said "init never raises it" while `init.c:534` raises it. Resolution from source: `init.c:901-902` raises it; `git log -S'never raises' -- sys/arch/rp2040/doc/PICOCALC.md` lists 19c35974, which added the sentence, and 92dbbb66, which replaced it; `PICOCALC.md:727` now says `multiple()` sets level 1 on entry from level 0. The three research notes keep their text as retained evidence; the `init.c` line numbers they cite (306, 315-316, 534-535) now point at `single()` and `multiple()` at 680-681 and 901-902.

`ptrace` is not a level-1 concern here: `sys/kern/sys_process.c:105-116` implements `PT_TRACE_ME` and the requests of an already traced child, with no attach, so init cannot be traced by an unrelated process.

`ucall`. `sys/arch/rp2040/rp2040/sysctl.c:149-170` implements syscall 154: after `suser()` it calls the function at a caller-supplied address with two integer arguments, at `spl0` or `splhigh`. It is in the shipped libc (`distrib/rp2040/mkboardlibc.py:66`) and no securelevel or range test guards it. `sys/arch/rp2040/doc/MPU.md` does not mention it. The host fixture `check-ucall-qemu` executes the extracted body and shows that the call goes to the given address; it does not run on silicon. Claim, with its evidence class: root code runs at kernel privilege by calling a kernel or user-window address through `ucall` [source reading and host fixture, not board-tested]. Falsifier: a board run in which `ucall` of an address inside the user window faults under the MPU configuration.

## 2. The reference trees

Every row below was read at the cited lines in the tree named. "Handler" is the sysctl rule for lowering; a dash means the tree has no such class in the lines opened.

| Tree | Variable home and initial value | Lowering rule | File flags | Raw disk and mounted | mem and kmem | Time backwards | Other classes | init and rc |
|---|---|---|---|---|---|---|---|---|
| 2.11BSD (211bsd, with patch 451) | `sys/sys/init_main.c:37` BSS; `-1` only under `PERMANENTLY_INSECURE` in `sys/CURLY/param.c:81-83` and `sys/QT/param.c` (patch 451 removed it from `sys/GENERIC/param.c` and `sys/conf/param.c`) | `kern_sysctl.c:253`: refused unless pid 1 **and** `securelevel > 0` | `ufs_fio.c:135-137` (`> 0`) | `sys_inode.c:655` (`>= 2` char disk), `:663-669` (`>= 1` mountedon, kmem), `:682` (`>= 2` block), `:694` (`> 0` mountedon) | open only, at `sys_inode.c:663-669`; no per-write test | `kern_time.c:94`, under `#ifdef NOTNOW` | -- | `init.c:286-287` lowers, `:384-385` raises 0 to 1 |
| 4.4BSD-Lite2 | `sys/kern/kern_sysctl.c:180` | `:223`: refused unless pid 1 | `ufs_vnops.c:363-366` | `spec_vnops.c:161` (`>= 2` D_DISK), `:169-177` (`>= 1` mountedon, kmem), `:193-195` (`>= 2` block) | open only (no per-write test: `rg securelevel` finds none in the memory driver) | `kern_time.c:113` active (`> 0`) | -- | `init.c:561-562`, `:1162-1163` |
| RetroBSD | `sys/pic32/machdep.c:126-130`; `-1` under `PERMANENTLY_INSECURE`, else 0 | `kern_sysctl.c:216`: pid 1 only | `ufs_fio.c:115` | `sys_inode.c:588, 596, 615, 627` | open only | `kern_time.c:36`, under `#ifdef NOTNOW` (:24) | -- | `init.c:259-260`, `:456-457` |
| LiteBSD | `sys/kern/kern_sysctl.c:180` | `:371`: pid 1 only | `ufs_vnops.c:373-378` | `spec_vnops.c:160, 168, 192` | open only | `kern_time.c:135` active | -- | `init.c:589-590`, `:1190-1191` |
| OpenBSD | `sys/kern/kern_sysctl.c:333` | `:1220-1222`: from a level at or below 0, any root may set a value down to -1; from above 0, pid 1 only; compare-and-swap | `ufs_vnops.c:358-360`; `tmpfs_subr.c:957`; `ext2fs_vnops.c:232` | `spec_vnops.c:127, 135-143, 161` | open of mem and kmem refused at `> 0` unless `allowkmem` (`arch/*/mem.c`, `arm64/mem.c:111`) | `kern_time.c:99`: `> 1`, with a console message | ptrace of pid 1 refused at `> -1` (`sys_process.c:425`); writable integer sysctls become read-only at `> 0` (`kern_sysctl.c:1244-1251`; used by `tcp_usrreq.c:1425`, `udp_usrreq.c:1290`); GPIO pin writes (`gpio.c:315-493`); `pf` ioctls at `> 1` (`pf_ioctl.c:1952`); ddb (`db_usrreq.c:59-91`); EFI variables (`efi.c:257`); PCI aperture (`pci.c:1205-1209`); ioperm and iopl (`i386/sys_machdep.c:69-72`); no loadable-module site in the grep | `init.c:508-509`, `:1096-1097`; `etc/rc:635-638` runs `/etc/rc.securelevel` then sets 1 if still 0 |
| NetBSD | `sys/secmodel/securelevel/secmodel_securelevel.c:61` (static); `:148-156`: `-1` under `INSECURE`, else 0 | `:85`: only `initproc` may lower | `:614-617` (sysflags write, `> 0`) | `:544-562`: mounted device `> 0`, any disk `> 1`; passthru `> 0` | `:525-535`: write `> 0` | `:278`: `> 1` and the step wraps; RTC offset `> 0` | module load `> 0` (:295); mount new `> 1`, remount only to read-only `> 1` (:299-318); sysctl add, delete, describe `> 0` (:326-337); core name `> 1`; ptrace and procfs write of init at `> -1` (:381, :394); ioperm, iopl, unmanaged memory `> 0` (:470-478); microcode apply `> 1`; GPIO pin set `> 0`; random-pool estimate `> 1`; map VA zero `> 0` | `init.c:671-673` lowers, `:1501-1502` raises; `etc/rc.d/securelevel` raises to `$securelevel` or to 1 from 0 |
| FreeBSD | per jail: `kern_jail.c:112` (`prison0`, `-1`); tests `securelevel_gt` and `securelevel_ge` at `kern_prot.c:1843-1853` | `kern_mib.c:421-458`: never lowered (a regression-test knob aside); children are raised to match | `ufs_vnops.c:683-690` (`> 0`, also `SF_NOUNLINK`); `tmpfs_subr.c:2065`; ext2 | `geom_dev.c:455`: write open of any disk `>= 2` | `memdev.c:85`: write open `> 0` | `kern_time.c:141`: `> 1` limits steps | kldload and kldunload `> 0` (`kern_linker.c:1207, 1275`); ptrace of init `> 0` (`kern_prot.c:2362`); kdb entry (`subr_kdb.c:515`); CAM passthrough `> 1`; `pf` `> 2`; ipfw, ipfilter, dummynet `>= 3`; ioperm, PCI, nvram, cpuctl, vt, syscons, firmware load | init does not touch the level (no hit in `sbin/init/init.c`); `libexec/rc/rc.d/securelevel` sets `kern_securelevel` when `kern_securelevel_enable` (default `NO`, `-1`) |
| DragonFly | `sys/kern/kern_mib.c:254` (`-1`); `kernel_mem_readonly` tunable at :255 | `:266`: every lowering refused, pid 1 included | `ufs_vnops.c:412-418`; `vfs_helper.c:188-193` | `devfs_vnops.c:1006-1023`: disk write open `>= 2`; mounted read-write `EBUSY`, mounted read-only `>= 1` | `kern_memio.c:156, 190`: `> 0` or `kernel_mem_readonly` | `kern_time.c:133`: `> 1` | module load and unload `> 0` or `kernel_mem_readonly` (`kern_linker.c:316, 486, 786, 833`); ptrace of init `> 0` (`sys_process.c:214`); `CTLFLAG_SECURE` sysctls (`kern_sysctl.c:1440`); `pf` `> 1`; ipfw `>= 3`; PCI, cpuctl, syscons, CAM `> 1` | `init.c:1382-1383` raises 0 to 1; no lowering call; `etc/rc.d/securelevel` |

Reading across the table.
- Levels. The 4.4BSD family defines -1 (permanently insecure), 0, 1 (secure) and 2 (highly secure); `sys/h/systm.h:12-38` of 2.11BSD and `sys/sys/systm.h:7-35` of this tree carry the same prose. FreeBSD and DragonFly extend to 3 for packet filters.
- The three small trees that share this port's lineage (2.11BSD, RetroBSD, this tree) keep four checks: flags, raw disk, mem, clock. They never grew the module, sysctl or ptrace classes because they have no modules and no network.
- The lowering rule is where the trees differ in kind: pid 1 (4.4BSD, RetroBSD, LiteBSD, NetBSD), pid 1 above 0 and anyone at or below (2.11BSD patch 451, OpenBSD), never (FreeBSD, DragonFly). DragonFly pairs "never lower" with an init that does not try; this tree pairs "init lowers" with a handler that lets it.
- NetBSD and OpenBSD gate the init process itself (ptrace and procfs write at `> -1`), because init holds the lowering right. This tree has no attach, so the class is moot.

### 2.11BSD patch 451, read in full (commit 930227f)

`930227f` ("sysctl securelevel -1 (Path 451)", 2018-12-22 by the VERSION file) changes four files. `VERSION` moves from patch level 450 to 451. `sys/GENERIC/param.c` and `sys/conf/param.c` each lose the `#ifdef PERMANENTLY_INSECURE ... int securelevel = -1; #endif` block and its comment (the variable falls back to the BSS definition in `init_main.c`). `sys/sys/kern_sysctl.c` changes the lowering test from `level < securelevel && p_pid != 1` to `level < securelevel && p_pid != 1 && securelevel > 0`.

Effect: a root process at level 0 may now set -1 at run time, which keeps `init` from raising to 1 (init raises only from exactly 0). The compile-time `PERMANENTLY_INSECURE` switch becomes a boot-script decision. At levels above 0 nothing changes. At level 0 root could already write `/dev/kmem`, so no protection is given up. The diff moves no check site.

## 3. Threat model for this device

Assets and actors. The device is a handheld or bare board with a flash root (the kernel and 1536 KB Dhara root in internal QSPI flash) and a removable SD card. The console is USB CDC-ACM or UART0. The image holds a root account, an operator account and system accounts (`etc/passwd.rp2040`); the console line is marked `insecure` and root logs in through operator and `su` (`etc/ttys`). The generated PICO Makefile carries no network option. Someone holding the device can press BOOTSEL and reflash any firmware (RP2040 datasheet 2.8): physical access defeats every software boundary and securelevel does not try to resist it. The MPU separates user code from kernel memory (`sys/arch/rp2040/doc/MPU.md`); every process image shares one 192 KB window.

The memory devices ship mode 0640 owned by root and group kmem (`etc/MAKEDEV:24-25`), so the securelevel write refusals bind root and kmem-group members; an ordinary user cannot open them for writing at any level.

Two adversaries remain: a faulty or hostile program run as root, and a mistake of the operator. On a single-user device the second dominates, and the checks act as accident guards.

| Check | What it protects here | Verdict |
|---|---|---|
| Level 1: write open of a raw card or flash node while a filesystem on it is mounted (`sys_inode.c:625-638, 667-668`) | A running filesystem from `dd`, a second `fsck` or `newfs` on the same medium. The cost of the mistake is a corrupted root or card, the device's main durable state. Tested at the host tier (`ufs_disk_test.c` admission cases, `check-kernel-ilp32`) | Protects |
| Level 1: write to `/dev/mem`, `/dev/kmem` (`sys_inode.c:629`, `mem.c:53`) | The kernel image and tables from a stray store. `mmrw` admits kernel data, kernel flash and the caller's window for writes (`kmem-options-bsd-corpus.md`), so `securelevel`, `proc[]` and the MPU-free kernel globals are otherwise writable | Protects against accident; bypassed by `ucall` |
| Level 1: live swap minor 64 (`swap.c:73`) | The swap image of resident processes from a write that would corrupt a swapped process | Protects |
| Level 1: flag changes (`ufs_fio.c:122`) | Nothing in the shipped image sets a system flag | Moot unless the owner uses `chflags` |
| Level 2: raw disk writes always refused (`sys_inode.c:617, 654`) | A stronger form of the first row, at the price of blocking `savecore`, `fsck` and `newfs` of the card (`savecore.8:70`, `PICOCALC.md:735`). Nothing sets level 2 | Reachable only by a manual `sysctl`; moot by default |
| Clock backwards (compiled out) | File timestamps from a forged clock; the device runs no ntpd, which is the reason the tree's own comment gives for disabling the check | Moot |
| Lowering by pid 1 only | Prevents a root program from clearing level 1 | Holds for honest interfaces; bypassed by `ucall` or a kernel write at level 0 |
| `ptrace` of init | Init holds the lowering right | Moot: no attach request |
| sysctl write locks, module load, mount | The device has no loadable modules and a single mount path | Moot |

The `ucall` gap changes the verdict of every "protects" row against the deliberate adversary: a root program that calls a kernel routine at an address of its choosing already holds more than a `/dev/kmem` write. Against the accident guard role the gap is irrelevant, since a stray program does not call `ucall`.

## 4. Measured cost

Method. A scratch clone of `origin/main` 4b3eaf6c was built on the WSL build host with `arm-none-eabi-gcc` 14.2.1 20241119 (package 15:14.2.rel1-1) at `-Os -mcpu=cortex-m0plus`; the four kernels PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM were relinked per variant and read with `arm-none-eabi-size` and the map file. Each variant edits one class in place (the test compiled to a constant), so the figure is the text the class occupies after the compiler has folded the rest. Deltas are in bytes of `text` against the baseline of the same kernel (100819, 90201, 121163, 123691). The compiler places functions on 4 or 8 byte boundaries, so a delta of 8 or less is within layout noise. Logs: `o16-s21-meas.log`, `o16-s21-meas2.log` on the build host. `bss`, `data` and the map's free-RAM figure were identical to the baseline in every variant.

Existing mechanism, removed one class at a time:

| Class compiled out | PICO | PICO_UART | PICOCALC | PICOCALC_PSRAM |
|---|---|---|---|---|
| file-flag test (`ufs_fio.c`) | -16 | -16 | -16 | -24 |
| level 2 open checks (`sys_inode.c`, four sites) | -152 | -152 | -192 | -192 |
| level 1 open checks (mounted raw, kmem, mounted block) | -280 | -280 | -336 | -336 |
| all open checks together | -440 | -440 | -528 | -536 |
| `mem.c` per-write test | -16 | -24 | -16 | -24 |
| live swap test (`swap.c`) | -40 | -40 | -40 | -40 |
| lowering rule (`p_pid != 1` test) | -8 | -16 | -8 | -16 |
| handler replaced by a read-only report | -40 | -48 | -40 | -40 |
| whole mechanism, level fixed at 0 | -560 | -568 | -656 | -664 |
| level fixed at 1 | -264 | -272 | -304 | -312 |
| level fixed at 2 | -144 | -152 | -144 | -152 |

RAM. `securelevel` is a 4-byte `.bss` object (`nm -S`, PICO address 0x20031cac). Removing it in the three fixed-level variants left `bss` and the free-RAM figure unchanged at the linker's alignment, so the RAM saving is 4 bytes at most and 0 bytes at the reported granularity. Userland: `getsecuritylevel` and `setsecuritylevel` are 76 and 104 bytes of `init.o` text, flash root, not kernel.

Candidate additions, applied to the baseline:

| Addition | PICO | PICO_UART | PICOCALC | PICOCALC_PSRAM |
|---|---|---|---|---|
| patch 451 lowering term | +10 | +2 | +10 | +2 |
| `ucall` refused at level above 0 | +26 | +18 | +26 | +18 |
| clock set backwards refused at level above 1 (using `wall_interval`, which the dead block's `timercmp` cannot) | +24 | +16 | +24 | +16 |

The first row of each pair is the cost the candidate adds to kernel text; none adds RAM.

## 5. Designs

Common test oracles already in the tree: `tests/kernel/ufs_disk_test.c` (admission at each level, with `ufs_disk_mutate.py` mutants), `tests/kernel/mem_span_test.c` and `mem_span_mutate.py` (the `mem.c` per-write check), `tests/kernel/swap_ioctl_test.c` (live swap), `tests/kernel/sysctl_test.c` (handler), and `check-ucall-qemu` (the `ucall` body under qemu-user).

**A. Keep as is.** Cost 0. Protects: the first three rows of the table in section 3 against accident. Falsifier: a write open of a mounted raw device succeeds at level 1; the `ufs_disk` admission cases and their mutants fail it. Test plan: none new. Residual: the `ucall` gap and the stale research citations.

**B. Adopt patch 451.** Cost +2 to +10 bytes text, 0 RAM. Protects nothing new; lets `/etc/rc` write `kern.securelevel=-1` at level 0, which replaces a kernel rebuild with `PERMANENTLY_INSECURE` for development boards. Falsifier: a non-init root lowers 1 to 0, or 0 to -1 fails. Test plan: extend `sysctl_test.c` with four cases (0 to -1 by a non-init root allowed; 1 to 0 by non-init refused; 1 to 0 by pid 1 allowed; raise always allowed) and mutants for each term. Note that `init` at -1 never raises, so a development kernel loses the level 1 guards until rebooted.

**C. Reduced two-level model.** Drop the level 2 checks and document the level as 0 or 1. Saves 152 to 192 bytes text. Gives up: a way to forbid all raw writes to the card and flash. The only evidence of use is documentation (`PICOCALC.md:735`, `savecore.8:70`); no script sets it. Falsifier: a shipped flow that needs level 2. Test plan: remove the level 2 rows of `ufs_disk_test.c` and the matching mutants; run `check-kernel-ilp32`. A softer variant keeps the checks and the documented level, at no saving.

**D. Build-time fixed level, no variable.** At fixed 0 it saves 560 to 664 bytes and deletes the guards: equal to a kernel without securelevel. At fixed 1 it saves 264 to 312 bytes but also removes the lowering that `single()` performs: the boot `fsck -p` runs at level 0 only because the variable is 0 until `multiple()` (`etc/rc.rp2040` calls `fsck` before multiuser), and `board_capacity_cases.py:31` records that level 1 refuses a second `fsck` of the mounted root. A fixed 1 would refuse the boot check of the root it mounts, or force a mounted-root exemption in `openi`, which is a new check and no saving. Fixed 2 is incompatible with `fsck`, `newfs` and `savecore`. Falsifier: a kernel with fixed 1 completes the boot `fsck -p` with the root mounted. Test plan: a Renode boot of a fixed-1 kernel (`check-renode`) would fail the autoboot step. Verdict: only fixed 0, as a documented "no securelevel" build option.

**E. Keep (A) and add the `ucall` gate.** Cost +18 to +26 bytes. Protects: makes level 1 mean something against a hostile root program, since the dispatch of an arbitrary routine at kernel privilege is the cheapest bypass of the `/dev/kmem` guard. Does not close root-writable kernel memory by other paths (`__sysctl` writes of `machdep` nodes, for example `CPU_XIP_CACHE` at `sysctl.c:371-378`, run in kernel context by design). Falsifier: with the gate, a program at level 1 still reaches a kernel routine through `ucall`; `check-ucall-qemu` with a level-1 case and a mutant that drops the test would show it. Test plan: add the level test to the qemu-user fixture (both a refusal at 1 and success at 0); document in `MPU.md` that `ucall` is a root escape hatch below level 1. Autoconfig, the historical user of `ucall`, is not in the image.

**Recommendation: E.** Keep the mechanism (it costs about half a percent of kernel text and guards the card and the kernel against accidents), add the `ucall` gate, record the level semantics in one place (`PICOCALC.md` or `MPU.md`), and leave B, C and the clock check as owner options.

## 6. Residual owner decisions

1. **Gate `ucall` at level above 0?** Options: (a) gate at `> 0`, +18 to +26 bytes, makes level 1 bind root against deliberate code, and a development workflow that calls `ucall` at level 1 breaks; (b) gate at `>= 2` only, same cost, protects only where nothing runs by default; (c) leave open and document it, 0 bytes, level 1 stays an accident guard. Recommended: (a).
2. **Keep level 2?** Options: (a) keep, 0 bytes, an unused but documented lever; (b) remove, -152 to -192 bytes, the documentation changes with it. Recommended: (a) until a savings target needs the bytes.
3. **Adopt patch 451?** Options: (a) adopt, +2 to +10 bytes, run-time `-1` for development kernels; (b) leave, the compile-time switch stays. Recommended: (b), because no script of the shipped image needs it and `PERMANENTLY_INSECURE` already covers the development case.
4. **Enable a backwards-clock refusal at level 2?** Options: (a) no, the dead `NOTNOW` block stays and the note records why; (b) add the `wall_interval` form at `> 1`, +16 to +24 bytes, protects timestamps only where level 2 is used. Recommended: (a).
5. **Offer a no-securelevel build?** Options: (a) document fixed 0 as a kernel option, -560 to -664 bytes, no guards; (b) do not offer it. Recommended: (b) for the shipped configurations, since the kernel budget stays sensitive to text and the guard is half a percent of it.

Each recommendation names its price and its falsifier above. A change to any of them is a kernel change and starts with the prediction record for its gate (`ufs_disk_test.c` rows, `sysctl_test.c` rows, or the `ucall` fixture).
