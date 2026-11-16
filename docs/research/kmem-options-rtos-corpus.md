# Task listing in NuttX, Zephyr and FreeRTOS: kernel-side copy versus raw memory

## Finding

None of the three RTOSes lets user space rebuild task state from raw kernel memory or from swap for its task-listing interface. Each listing runs in kernel context: NuttX formats text from the live task control block (TCB) inside a critical section, Zephyr walks the kernel thread list and prints from the live `struct k_thread`, and FreeRTOS fills a fixed `TaskStatus_t` array under a scheduler suspension. Two raw-access paths exist beside the listings: NuttX exposes the address and size of its TCB layout table through procfs (`tcbinfo`), and Zephyr ships a separate physical-memory dump shell service (`devmem`). Neither is a task list. None of the three has a swap-backed process table in the listing path.

Evidence classes: every claim below is marked **[read]** (observed in the cloned source at the path:line given) or **[inferred]** (a conclusion from the read source, not executed). No code was built or run. Corpus trees are shallow clones under `corpora/{nuttx,zephyr,FreeRTOS-Kernel}`, referred to below as `nuttx/`, `zephyr/`, `FreeRTOS-Kernel/`.

## Commands run (for re-checking absences)

- `grep -rn "nxtask_argvstr\|group_argvstr\|argvstr" --include=*.c --include=*.h nuttx` : `nxtask_argvstr` only; `group_argvstr` has 0 matches.
- `grep -rln "ps_main\|\"ps\"" --include=*.c --include=*.h --include=Make.defs --include=Kconfig --include=CMakeLists.txt nuttx` : no matches. `ls nuttx/apps` fails: the NuttX `apps/` tree (which holds the nsh `ps` builtin) is not in the corpus.
- `grep -n "cmdps\|^ps" nuttx/Documentation/applications/nsh/commands.rst` : `.. _cmdps:` at line 1204; the `ps` example output with columns `PID PRI POLICY TYPE NPX STATE EVENT SIGMASK COMMAND` appears near line 1005.
- `grep -rn "CONFIG_BUILD_KERNEL\|CONFIG_BUILD_PROTECTED" nuttx/fs/procfs/fs_procfsproc.c nuttx/sched/task/task_argvstr.c` : 0 matches.
- `grep -rn "kernel thread\|\"threads\"\|k_thread_foreach\|thread_foreach" zephyr` (excluding tests) : command registration in `subsys/shell/modules/kernel_service/thread/`, iterators in `kernel/thread_monitor.c`.
- `grep -n "uxTaskGetSystemState\|vTaskList\|TaskStatus_t" FreeRTOS-Kernel/tasks.c FreeRTOS-Kernel/include/task.h` : the API and the `vTaskList` macro.
- `grep -n "/dev/mem\|argv\|swap" FreeRTOS-Kernel/tasks.c FreeRTOS-Kernel/include/task.h` : matches only in comments ("swapped out", task.h:1662-1736); 0 code matches.
- `grep -rn "swap" nuttx/fs/procfs` : 0 matches in task-listing sources. `grep -rln "swap" nuttx/fs/procfs nuttx/sched` : `sched/paging/pg_miss.c` (demand paging), `sched/sched/sched_sysinfo.c` (doc comment), `sched/sched/sched_addreadytorun.c`.
- `grep -rn "swap" zephyr/subsys/shell/modules/kernel_service zephyr/subsys/debug/thread_analyzer` : 0 matches.
- `grep -rln "/dev/mem\|/dev/kmem\|devmem" --include=*.c --include=*.h nuttx/fs nuttx/sched nuttx/include nuttx/drivers/misc` : `drivers/misc/dev_mem.c`, `include/nuttx/drivers/drivers.h`, `include/paths.h`, plus paging-related sched files.
- Function-length counts: an awk brace counter, started at each function's signature line, ended at the brace that closes the body. It ignores string literals and `//` comments and counts both arms of preprocessor conditionals. Counts include the signature. Approximate to about 2 lines.

## 1. NuttX

### 1.1 Listing command and interfaces

- **nsh `ps`**: documented at `nuttx/Documentation/applications/nsh/commands.rst:1204` (anchor `cmdps`) and gated by `CONFIG_NSH_DISABLE_PS` (`nuttx/Documentation/applications/nsh/config.rst:105`). **[read]** The `ps` source is not in the corpus, so the mapping from `ps` to procfs fields is **[inferred]** from the documented column set.
- **procfs per task** (`/proc/<pid>/...`), implemented in `nuttx/fs/procfs/fs_procfsproc.c`:
  - `status` = `proc_status`, lines 484-686 (203 lines). **[read]** Name at 501-503 (`%-12s%.18s`, truncated to 18 characters). Type (flag-decoded), Group (`tg_pid`), Uid/Gid under `CONFIG_SCHED_USER_IDENTITY`, CPU under `CONFIG_SMP` (SMP is symmetric multiprocessing, several cores), State via `nxsched_get_stateinfo` at 602, Flags, Priority at 639 and 643 (base priority in parentheses under `CONFIG_PRIORITY_INHERITANCE`), Scheduler policy at 662, SigMask at the end.
  - `cmdline` = `proc_cmdline`, lines 692-737 (46 lines). Name at 718-722, argv at 724-729.
  - `stack` = `proc_stack`, lines 1094-1224 (131 lines). StackSize at 1144, StackUsed at 1162 via `up_check_tcbstack`, deepest-stack-pointer math at 1176.
  - CPU time: `proc_critmon`, lines 794-983 (190 lines). **[read]** It prints `tcb->run_time` through `perf_convert` (about line 964) and the per-task maximum, and it is dispatched only under `CONFIG_SCHED_CRITMONITOR` (dispatch guard at `fs_procfsproc.c:1789`). Without that option, procfs gives no per-task CPU time.
  - `groupstatus` = `proc_groupstatus`, lines 1230-1349; `groupfd` 1355-1458; `groupenv` 1564 onward.
  - `loadavg` = `proc_loadavg`, 744-786; the per-task CPU load is not part of `status`.
- **Process enumeration** (top level): `procfs_opendir` at `nuttx/fs/procfs/fs_procfs.c:652-810` (159 lines). **[read]** It takes a snapshot of all pids with `nxsched_foreach(procfs_enum, level0)` at line 705. `procfs_enum` (lines 368-385, 18 lines) copies `tcb->pid` into an array.
- **Snapshot locking**: `nxsched_foreach` (`nuttx/sched/sched/sched_foreach.c:60-81`, 22 lines) takes `enter_critical_section()` (67) and `sched_lock()` (68), iterates `g_pidhash`, and releases both (79-80).
- **Per-read path**: `proc_read` (`fs_procfsproc.c:1746-1837`, 92 lines). **[read]** It holds `enter_critical_section()` for the whole read (1763), calls `nxsched_get_tcb(procfile->pid)` inside it, dispatches on `procfile->node->node` (1774), and releases the critical section after the switch (1827).

### 1.2 What the kernel copies, and when

**[read]** No fixed record. Each read formats text into a per-open buffer `procfile->line` (declared `char line[STATUS_LINELEN]`, `fs_procfsproc.c:137`, `STATUS_LINELEN` = `PATH_MAX`, line 82). The fields come from the live TCB, under the critical section above. The text is what user space parses. This is the same pattern the Zephyr shell uses, and the opposite of FreeRTOS's fixed array.

**Raw-layout exposure (not a task list)**: `tcbinfo` (`nuttx/fs/procfs/fs_procfstcbinfo.c:187-225`, 39 lines) emits one line: `pointer <&g_tcbinfo> size <sizeof(struct tcbinfo_s)> current regs <running_regs()>`. **[read]** The `g_tcbinfo` table for ARMv6-M is in `nuttx/arch/arm/src/armv6-m/arm_tcbinfo.c:70`. This hands a debugger the address and size of a layout table, so a decoder can read TCB fields by offset. It is gated by `CONFIG_ARCH_HAVE_TCBINFO` and `CONFIG_FS_PROCFS_EXCLUDE_TCBINFO` (guard at `fs_procfstcbinfo.c:49`). **[inferred]** On a protected build, user space cannot dereference that address without an address mapping.

### 1.3 Argument vector and non-addressable tasks

- **Argv routine**: `nxtask_argvstr`, `nuttx/sched/task/task_argvstr.c:59-111` (53 lines).
  - Lines 68-72: a NULL tcb, a zero-size buffer, or the idle task returns an empty string with no marker.
  - Lines 74-79: when `CONFIG_ARCH_ADDRENV` and `tcb->addrenv_own != NULL`, call `addrenv_select(tcb->addrenv_own, &oldenv)`. The return value is ignored (line 77).
  - Lines 81-88: a pthread task prints its entry pointer and argument pointer instead of argv.
  - Lines 91-101: otherwise `nxsched_get_stackargs(tcb)` supplies `tl_argv`, and each string is appended with `snprintf(" %s")`.
  - Lines 103-108: `addrenv_restore`, also with the return value ignored.
  - Line 110: `return n < size - 1 ? n : size - 1;` truncates with no marker.
- **Stack argv source**: `nxsched_get_stackargs` (`nuttx/sched/sched/sched_get_tls.c:74-78`) returns `nxsched_get_tls(tcb)->tl_argv`. The field is `tl_argv` in `nuttx/include/nuttx/tls.h:284`.
- **Address-environment switch**: `addrenv_select` (`nuttx/sched/addrenv/addrenv.c:420-429`) takes the address environment of `this_task()` (the reader), saves the previous one in `tcb->addrenv_curr`, and calls `addrenv_switch`. The reader therefore maps the target's memory while it copies, and the copy happens inside the procfs critical section. **[inferred]** The reader and the target share one CPU for the duration, which is what keeps the mapping valid.
- **`group_argvstr`**: absent. 0 matches in the corpus.
- **Hidden-row handling for argv**: `proc_cmdline` returns the argv text or nothing; the tool sees a shorter line, not a marker.

### 1.4 Silent-drop points, read from source

1. **Pid vanishes during directory enumeration.** `procfs_readdir` (`nuttx/fs/procfs/fs_procfs.c:1012-1018`) returns `-ENOENT` when `nxsched_get_tcb(pid)` is NULL. **[read]** The same error code is the end-of-directory signal (`fs_procfsproc.c:2090-2095`, comment: "signal the end of the directory by returning the special error -ENOENT"). **[inferred]** A reader cannot tell this apart from a normal end of listing, so the rest of the pid list is dropped without a marker. Relevant to the question: this is the NuttX path where rows can drop silently.
2. **Pid vanishes between open and read.** `proc_read` returns `-ENODEV` (`fs_procfsproc.c:1765-1770`). This is an error return, not a dropped row.
3. **argv truncation.** `nxtask_argvstr` truncates at line 110 with no marker.
4. **Defect found while reading (not executed).** `proc_cmdline` passes `sizeof(procfile->line)` as the destination length to `procfs_memcpy` at `fs_procfsproc.c:724-726`, where the other calls in the same routine pass `remaining`. `procfs_memcpy` (`nuttx/fs/procfs/fs_procfsutil.c:94-123`) copies `MIN(srclen, destlen)` bytes (line 117 region). **[inferred]** When the caller's `buflen` is smaller than name plus argv, the copy writes past the caller's buffer, by up to `PATH_MAX` minus the remaining space, and `remaining -= copysize` at line 729 underflows. Verification needs a read with a small `buflen` on a build with a non-empty argv. Not run.

### 1.5 Kernel-side routine sizes (lines, approximate)

| Routine | File:lines | Lines |
|---|---|---|
| `proc_status` | fs_procfsproc.c:484-686 | 203 |
| `proc_cmdline` | fs_procfsproc.c:692-737 | 46 |
| `proc_critmon` | fs_procfsproc.c:794-983 | 190 |
| `proc_stack` | fs_procfsproc.c:1094-1224 | 131 |
| `proc_read` | fs_procfsproc.c:1746-1837 | 92 |
| `proc_open` | fs_procfsproc.c:1623-1720 | 98 |
| `proc_opendir` | fs_procfsproc.c:1930-2049 | 120 |
| `proc_readdir` | fs_procfsproc.c:2072-2149 | 78 |
| `procfs_opendir` | fs_procfs.c:652-810 | 159 |
| `procfs_enum` | fs_procfs.c:368-385 | 18 |
| `procfs_readdir` (pid check at 1012-1018) | fs_procfs.c:888-1113 | 226 |
| `nxsched_foreach` | sched_foreach.c:60-81 | 22 |
| `nxtask_argvstr` | task_argvstr.c:59-111 | 53 |
| `procfs_memcpy` | fs_procfsutil.c:94-123 | 30 |

## 2. Zephyr

### 2.1 Listing command and API

- **Shell command**: `kernel thread list`, not `kernel threads`. **[read]** `subsys/shell/modules/kernel_service/thread/thread.c:41-42` registers the `thread` subtree (`KERNEL_CMD_ADD(thread, &sub_kernel_thread, ...)`), and `subsys/shell/modules/kernel_service/thread/list.c:141` registers `list` under it (`KERNEL_THREAD_CMD_ADD(list, ...)`). The handler is `cmd_kernel_thread_list`, `list.c:120-139` (20 lines). The shell build gates it through `subsys/shell/modules/CMakeLists.txt` (`kernel_service`).
- **Kernel API**: `k_thread_foreach` (`zephyr/include/zephyr/kernel.h:198`, locked variant) and `k_thread_foreach_unlocked` (`kernel.h:259`, unlocked). Both are kernel-only: neither has `__syscall`, so user-mode threads cannot call them.
- **Thread analyzer**: `subsys/debug/thread_analyzer/thread_analyzer.c`, `thread_analyzer_run` at 289-313 selects a foreach variant (295-303) and calls `thread_analyze_cb` (155-244, 90 lines) for each thread. The per-thread record `struct thread_analyzer_info` carries name, stack size and used, optional private-stack usage, runtime-stats usage, utilization, and priority.

### 2.2 What the kernel copies, and when

**[read]** No copy. `shell_tdata_dump` (`list.c:62-118`, 57 lines) prints from the live `struct k_thread` as it visits each thread:

- Name via `k_thread_name_get` (list.c:73).
- `base.user_options`, `base.prio`, and the timeout field: `timeout.abs_ticks` under the min-heap or skip-list timeout backends, else `timeout.dticks` (list.c:83-93). The comment at 79-82 documents that the raw field has different meanings per backend.
- State string via `k_thread_state_str` (list.c:95; `kernel/thread.c:234-289`).
- Entry pointer `thread->entry.pEntry` (list.c:96).
- `base.cpu_mask` under `CONFIG_SCHED_CPU_MASK` (list.c:98-100).
- Runtime stats via `rt_stats_dump` (list.c:14-59, 46 lines), which calls `k_thread_runtime_stats_get`.
- Stack via `k_thread_stack_space_get` (list.c:104-116).

**Locking**: `thread_foreach_helper` (`kernel/thread_monitor.c:40-75`, 36 lines). **[read]** Locked mode holds `z_thread_monitor_lock` across the callback (lines 52, 69-71, 74). The unlocked mode used by the shell (line 136 of list.c) releases the spinlock around each callback (65-68). Documented hazard: `include/zephyr/kernel.h:250-258` says an aborted thread may still be visited, and that the walk "may never return" if the freed `k_thread` memory is reused. **[inferred]** The shell listing can read a freed thread struct under this condition.

**Stack bytes (the one raw read)**: `z_stack_space_get` (`kernel/thread.c:1180-1229`, 50 lines) scans stack memory from the bottom. Lines 1197-1204 return `-ENOTSUP` when the stack under inspection is the running stack and `CONFIG_NO_UNUSED_STACK_INSPECTION` is set, since reading unused stack there can fault. Line 1206 starts the sentinel path (`CONFIG_STACK_SENTINEL`).

**Runtime stats copy**: `k_thread_runtime_stats_get` (`kernel/thread.c:1393-1407`, 15 lines) copies `z_sched_thread_usage` into the caller's struct. **[read]** Without `CONFIG_SCHED_THREAD_USAGE` it zeroes the struct and still returns 0 (the `#else` arm at `kernel/thread.c:1402`).

### 2.3 Argument vector and non-addressable tasks

- **No argv.** The listing prints `entry: %p` (list.c:94-96), not arguments. Zephyr kernel threads carry an entry pointer and three parameters, not a vector.
- **User mode.** `k_thread_stack_space_get` is `__syscall` (`include/zephyr/kernel.h:605`). Its documented user-mode returns are `-EINVAL` for an uninitialized or exited thread and `-EFAULT` for a bad output pointer (kernel.h:600-603). The shell listing does not go through the syscall; it runs in kernel context.
- **Silent-drop check.** Visible errors are printed as markers, not dropped:
  - `list.c:105-108`: "Unable to determine unused stack size (%d)".
  - `list.c:51-58`: runtime-stats fields print `?` when `k_thread_runtime_stats_get` or `k_thread_runtime_stats_all_get` returns nonzero.
  - `thread_analyzer.c:185-193`: "unable to get stack space (%d)", then `unused = 0`.
- **Zero, not a marker.** When `CONFIG_SCHED_THREAD_USAGE` is off, the runtime-stats get returns 0 with zeroed fields (`thread.c:1393-1407`), so the shell prints 0 rather than `?`. **[inferred]** This is a silent zero.

### 2.4 Raw-memory tool (separate, not a listing)

`subsys/shell/modules/devmem_service.c:35` defines `memory_dump(phys_addr, size, width)`. **[read]** It is gated by `DEVMEM_SHELL` (`subsys/shell/modules/Kconfig:29`). It is a physical-memory dump, not a task list, and it does not parse kernel structures.

### 2.5 Kernel-side routine sizes

| Routine | File:lines | Lines |
|---|---|---|
| `shell_tdata_dump` | list.c:62-118 | 57 |
| `cmd_kernel_thread_list` | list.c:120-139 | 20 |
| `rt_stats_dump` | list.c:14-59 | 46 |
| `thread_foreach_helper` | kernel/thread_monitor.c:40-75 | 36 |
| `thread_analyze_cb` | subsys/debug/thread_analyzer/thread_analyzer.c:155-244 | 90 |
| `z_stack_space_get` | kernel/thread.c:1180-1229 | 50 |
| `k_thread_state_str` | kernel/thread.c:234-289 | 56 |
| `k_thread_runtime_stats_get` | kernel/thread.c:1393-1407 | 15 |

## 3. FreeRTOS

### 3.1 Listing API

- **Status array**: `uxTaskGetSystemState` (`FreeRTOS-Kernel/tasks.c:4589-4618`, 30 lines), declared at `include/task.h:2254`.
- **Record**: `TaskStatus_t` (`include/task.h:165-185`). Fields: `xHandle`, `pcTaskName` (pointer into the TCB; the source says it "will be invalid if the task was deleted", task.h:167), `xTaskNumber`, `eCurrentState`, `uxCurrentPriority`, `uxBasePriority`, `ulRunTimeCounter` (CPU time, valid only under `configGENERATE_RUN_TIME_STATS`), `pxStackBase`, `pxTopOfStack`/`pxEndOfStack` (under stack-growth or high-address options), `usStackHighWaterMark`, `uxCoreAffinityMask`.
- **Text table**: `vTaskList` is a macro for `vTaskListTasks` (`include/task.h:2416`). The function is `tasks.c:7396-7559` (164 lines). `vTaskGetRunTimeStatistics` follows at about 7569.
- **Per-task record builder**: `vTaskGetInfo` (`tasks.c:6286-6426`, 141 lines).

### 3.2 What the kernel copies, and when

**[read]** A fixed record. `vTaskGetInfo` writes each `TaskStatus_t` from the TCB (lines 6294-6340). Run time at line 6326 (`ulTaskGetRunTimeCounter`). The stack high-water mark is computed by `prvTaskCheckFreeStackSpace` at lines 6412 and 6416, which reads stack bytes from the bottom in kernel context. The callback writer `prvTaskStatusArrayWriter` (`tasks.c:4506-4513`, 8 lines) fills the array slot.

**Lock**: `uxTaskGetSystemState` calls `vTaskSuspendAll()` at `tasks.c:4597` and `xTaskResumeAll()` after the walk. **[read]** This suspends the scheduler; it does not call a critical-section macro. **[inferred]** Interrupts stay enabled during the walk, so a tick can run, but no task switch occurs.

**Walk**: `prvCallForEachTask` (`tasks.c:4533-4564`, 32 lines) visits ready lists by priority, then the delayed and overflow-delayed lists, then `xTasksWaitingTermination` (deleted, not yet reaped; state `eDeleted`) under `INCLUDE_vTaskDelete`, then the suspended list. `prvForEachTaskInList` (`tasks.c:6433-6463`, 31 lines) iterates each list.

### 3.3 Argument vector and non-addressable tasks

- **No argv.** `grep -n "argv" FreeRTOS-Kernel/tasks.c FreeRTOS-Kernel/include/task.h` returns 0 code matches. The record has no command line or argument field. **[inferred]** The only per-task input a task carries is its `pvParameters` pointer, which the record does not expose.
- **MPU builds.** `portable/GCC/ARM_CM4_MPU/mpu_wrappers_v2_asm.c:591-609` defines `MPU_uxTaskGetSystemState`, a wrapper that switches to privileged mode for the call. **[inferred]** The output array lives in the caller's memory, so the wrapper is where the privileged write is checked; that check was not read in this pass.

### 3.4 Silent-drop points, read from source

1. **Array too small returns zero rows.** `uxTaskGetSystemState` checks `uxArraySize >= uxCurrentNumberOfTasks` at `tasks.c:4600`. When the check fails, the `else` arm is a coverage marker (line 4610) and the function returns 0 with no rows. **[read]** No error code is returned; the caller must compare the return value.
2. **Table snapshot race.** `vTaskListTasks` reads `uxArraySize = uxCurrentNumberOfTasks` before the scheduler is suspended (`tasks.c:7441`). A task created between that read and the call to `uxTaskGetSystemState` makes check 1 fail, so the table is empty with no marker. **[inferred]** The source comment at about line 7440 says the snapshot is taken "in case it changes while this function is executing", which confirms the intent but not the outcome; the empty-table result follows from the code path.
3. **Buffer full cuts the table.** When the text buffer fills, the loop sets `xOutputBufferFull = pdTRUE` (`tasks.c:7535`, `7540`) and breaks. No marker row is written. **[read]**
4. **Allocation failure.** `pvPortMalloc` returning NULL takes the `else` arm (`tasks.c:7555`, coverage marker) and writes an empty table. **[read]**
5. **Dangling name.** `pcTaskName` points into the TCB and is invalid after deletion (`include/task.h:167`). **[read]** The caller must copy the name before a deleted task is reaped.

### 3.5 Kernel-side routine sizes

| Routine | File:lines | Lines |
|---|---|---|
| `uxTaskGetSystemState` | tasks.c:4589-4618 | 30 |
| `prvTaskStatusArrayWriter` | tasks.c:4506-4513 | 8 |
| `prvCallForEachTask` | tasks.c:4533-4564 | 32 |
| `vTaskGetInfo` | tasks.c:6286-6426 | 141 |
| `prvForEachTaskInList` | tasks.c:6433-6463 | 31 |
| `vTaskListTasks` | tasks.c:7396-7559 | 164 |

## 4. Falsification answer

**Claim tested**: at least one of the three RTOSes lets user space reconstruct process or task state from raw kernel memory or from swap through its task-listing interface.

**Result**: not falsified for any of the three. The task-listing paths return kernel-produced text or kernel-filled records. The searches that could have shown raw or swap access in the listing path came back empty:

- NuttX: `grep -rn "swap" nuttx/fs/procfs` gives 0 matches. The only swap-related code in the corpus is demand paging in `nuttx/sched/paging/pg_miss.c`, and the procfs task listing does not reference it. `fs/procfs/fs_procfsmeminfo.c:352-365` reports aggregate page counts through `mm_pginfo`, not per-task state.
- Zephyr: `grep -rn "swap" zephyr/subsys/shell/modules/kernel_service zephyr/subsys/debug/thread_analyzer` gives 0 matches.
- FreeRTOS: `grep -n "/dev/mem\|argv\|swap" FreeRTOS-Kernel/tasks.c FreeRTOS-Kernel/include/task.h` gives 0 code matches.

Raw access exists beside the listings, and the report does not treat either as a listing:

- NuttX `tcbinfo` exposes a layout address and size (`fs_procfstcbinfo.c:187-225`). A decoder that reads the TCB by offset would reconstruct state from raw memory, so this is the NuttX path closest to the question. It requires a mapping that a protected-build user task does not have, which is **[inferred]**, not measured.
- Zephyr `devmem_service.c:35` dumps physical memory when `DEVMEM_SHELL` is set.

**Falsifier that would overturn the kernel-copy finding**: a NuttX `ps`, nsh, or apps tree that reads `tcb` offsets from `tcbinfo` or `/dev/mem` instead of procfs. That tree is not in the corpus, so the `ps` implementation is not verified here.

## 5. Implications for the discobsd `ps`

- The three RTOSes each choose one of: kernel-formatted text (NuttX procfs), kernel-filled record array (FreeRTOS), or kernel-printed shell output (Zephyr). None has a user-side decoder for a kernel layout.
- The silent-drop paths that matter are: NuttX pid vanishing mid-listing (`fs_procfs.c:1012-1018`), FreeRTOS small array returning zero rows (`tasks.c:4600`), FreeRTOS table buffer cut with no marker (`tasks.c:7535`). A replacement should return an explicit count or a marker row for each.
- Zephyr's zero-on-disabled runtime stats (`thread.c:1393-1407`) and NuttX's argv truncation (`task_argvstr.c:110`) are silent-substitute paths too.
- The NuttX `proc_cmdline` destination-length defect (`fs_procfsproc.c:724-726`) shows that a bounds check on the copy length is the failure to design against. **[inferred]** The defect is confirmed by reading only.

## Scope cuts and gaps

- The nsh `ps` source is not in the corpus. Its field mapping is inferred from procfs and the documented column set.
- The `up_check_tcbstack` architecture implementation was not located in `arch/arm/src/common` or `arch/arm/src/armv6-m`; its stack-read behavior is not verified.
- The NuttX `proc_cmdline` defect and the FreeRTOS MPU wrapper are read, not executed. Neither was built.
- The protected-build (`CONFIG_BUILD_KERNEL`) behavior of `addrenv_select` is read through `task_argvstr.c` and `addrenv.c`. No protected-build configuration was built.
