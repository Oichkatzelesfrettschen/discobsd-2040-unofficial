# Task listing and task state in four microkernel source trees

Scope: how a monitoring tool obtains the list of tasks (threads, components) with their state, name and CPU time in Genode, seL4, Fiasco.OC and L4Re, and whether any of them lets user space reconstruct another task's state from raw kernel memory or swap. The question is raised by a tiny Unix kernel (Cortex-M0+, 128 KB kernel) whose `ps(1)` reads raw `/dev/mem` and `/dev/swap`; the rows drop silently because user space does not know the swap ring layout or which RAM regions the memory driver refuses.

Corpus root (read-only, shallow clones; nothing was built or executed):
`corpora/`, one directory per upstream repository

Tree-relative paths below are written from that root: `genode/repos/...`, `seL4/...`, `fiasco/src/...`, `l4re-core/...`.

Evidence tags: `[read path:line]` marks a claim read in the source at that location. `[inferred]` marks a claim that follows from read lines but whose producing or consuming side was not read.

## Answer to the falsification question

Prediction tested: if any of the four trees lets user space reconstruct another task's state from raw kernel memory or swap, the tree contains a user-level read of kernel memory, a `/dev/mem`-style device path, or a kernel-internal layout that user space parses. Observed: none of the four does. Every per-task datum reaches user space through one of three channels: a kernel syscall that copies a fixed-size value into the caller's IPC buffer or UTCB; a trusted user-level server (Genode core's TRACE service, the Genode sandbox library) that copies a fixed record into a session buffer or a read-only ROM; or console output from a kernel debugger. [read, see per-system sections]

Deviation from the prediction: the only system that returns a full named task list to an unprivileged component is Genode, through the TRACE service and the sandbox state report. seL4 has no task list in user space at all. Fiasco.OC returns CPU time per thread, but not a list. L4Re returns nothing per task; Moe and Ned know only the tasks they created.

Two findings bear directly on the port:
- Genode's Fiasco build reports zero CPU time for every thread, because the kernel call that would supply it is disabled in source. [read `genode/repos/base-foc/src/core/platform_thread.cc:29-37`, `genode/repos/base-foc/src/core/thread_start.cc:63`]
- The Fiasco kernel information page (KIP) publishes the memory map as read-only, offset-relative descriptors, which is the pattern that would replace the port's inference of RAM regions from the memory driver. [read `l4re-core/l4sys/include/kip.h:36-80`]

## Method and grep patterns

Searches run from the corpus root. Each absence claim below names the pattern that produced it.

- Genode listing path: `grep -rln "Subject_info" genode/repos`, `grep -rln "for_each_subject_info\|subject_infos()" genode/repos`, `grep -rn "trace_source_info" genode/repos --include=*.cc --include=*.h`, `grep -rn "Execution_time\|execution_time" genode/repos/base*`.
- Genode sandbox state report: `grep -rln '"state"' genode/repos/os/src`, `grep -n "report_state\|produce_state_report\|generate_state_report" genode/repos/os/src/lib/sandbox/*.cc`.
- Genode raw memory: `grep -rl '/dev/mem\|/dev/kmem\|/dev/swap\|/proc/' genode/repos` returned six files, none in a listing path (`libports/run/fetchurl.inc`, `base/src/test/sub_rm/main.cc`, `os/run/vmm_x86.run`, and three others in run and test scripts).
- seL4 debug and benchmark syscalls: `grep -rn "DebugDumpScheduler\|DebugNameThread\|BenchmarkGetThreadUtilisation\|BenchmarkDumpAllThreadsUtilisation" seL4`, `grep -rln "seL4_DebugDumpScheduler\|DebugDumpScheduler" seL4`.
- seL4 user-space process table: `grep -rln "process.table\|process_table\|ps_list\|struct process" seL4/src seL4/include seL4/libsel4 seL4/tools`. The only tools match is `seL4/tools/helpers.cmake`, a CMake macro whose text matched the pattern on the word "process" (line 615 and 620 area); no task table exists in the corpus. `grep -rn "process" seL4/src seL4/include seL4/libsel4/include -l` lists only ACPI, VT-d, APIC, state-data and CPU-identification files, none of which is a task list.
- Fiasco thread time: `grep -rn "stats\|consumed_time" fiasco/src/kern/thread_object.cpp`, `grep -rln "sys_thread_stats\|consumed_time" fiasco/src`, `grep -rn "Thread::stats" fiasco/src` (no match; the entry point is `Thread_object::sys_thread_stats`).
- Fiasco JDB CPU time: `grep -rn "consumed\|stats" fiasco/src/jdb` returns only `fiasco/src/jdb/jdb_tcb.cpp:682` and `:1044`. The JDB thread list (`jdb_thread_list.cpp`) prints no CPU time column.
- L4Re stats callers: `grep -rln "stats_time" l4re-core/moe l4re-core/ned l4re-core/libkproxy l4re-core/l4re` returned no file.
- L4Re raw memory: `grep -rl '/dev/mem\|/dev/kmem\|/dev/swap\|/proc/' l4re-core/moe l4re-core/ned` returned no file. The 56 matches across `l4re-core` that were sampled are libc and libstdc++ `paths.h` and `c++config.h` path constants, not reads.

## Genode

### 1. How a monitor obtains tasks, state, name and CPU time

Two user-level paths exist. Both start from a kernel-side source per thread and end in a bounded copy.

Path A, the TRACE service (the richest listing):

- The client call is `Trace::Connection::for_each_subject_info`, which calls the RPC `subject_infos` and then iterates the returned records. [read `genode/repos/base/include/trace_session/connection.h:138-156`]
- The core-side RPC `Session_component::subject_infos()` maps the client's argument buffer and fills `Subject_info` records plus `Subject_id` values into it. [read `genode/repos/base/src/core/trace_session_component.cc:48-67`, call at `:62`]
- The registry method `Subject_registry::subjects(Subject_info*, Subject_id*, size_t)` iterates subjects, strips the session filter prefix from each label, and copies each `info()` into the output array. [read `genode/repos/base/src/core/include/trace/subject_registry.h:449-473`]
- `Subject::info()` reads the bound source's `Source::Info` (label, name, execution time, affinity) and attaches the state from `Subject::_state()`. [read `subject_registry.h:290-307`]
- `Subject::_state()` maps the source to one of `INVALID, UNATTACHED, ATTACHED, TRACED, FOREIGN, ERROR, DEAD`. [read `subject_registry.h:161-180`; enum at `genode/repos/base/include/base/trace/types.h:96`]
- Execution time is produced per thread by `Core::Cpu_thread_component::trace_source_info()`, which returns `_platform_thread.execution_time()`. [read `genode/repos/base/src/core/include/cpu_thread_component.h:180-185`]
- The `Subject_info` record carries: session label, thread name (`String<32>`, `types.h:26`), state, policy id, `Execution_time` (thread context and scheduling context time, quantum, priority, `types.h:66-86`), and affinity. [read `genode/repos/base/include/base/trace/types.h:92-140`]

Path B, the sandbox state report (structure, not per-thread time):

- The `init` component owns a reporter named `state` and regenerates it from the sandbox library. [read `genode/repos/os/src/init/main.cc:41-92`, reporter construction at `:62`, generation at `:80-82`]
- `Sandbox::Library::generate_state_report` delegates to `State_reporter`, which calls `produce_state_report`. [read `genode/repos/os/src/lib/sandbox/library.cc:139`, `:269-271`]
- `Sandbox::Child::report_state` emits one `<child>` node per child with `name`, `binary`, `version`, optional `id`, `state` (`incomplete` when stuck), `exited`, and `ram` and `caps` sub-nodes taken from the child's Pd session (`ram_quota` and `used_ram`, `cap_quota` and `used_caps`). [read `genode/repos/os/src/lib/sandbox/child.cc:362-437`, child node at `:367`, ram at `:389-395`, caps at `:403-408`; Pd accessors at `genode/repos/base/include/pd_session/pd_session.h:270-298`]
- The state report carries no per-thread entry and no CPU time. Grepping `report_state` and its neighbors for `thread` or `cpu` returns nothing in the report body. [read; absence checked in `child.cc:362-437`]

Consumers of path A, all user level:

- `genode/repos/os/src/app/top/main.cc:82-107` (update loop; deletes DEAD subjects whose time did not change; the header comment at `:299-310` sets the argument buffer to 12 pages for about 190 records).
- `genode/repos/os/src/app/trace_subject_reporter/main.cc:87-115` builds a `subject` node per record; the Reporter named `trace_subjects` is created at `:154-155`; `generate` is called at `:218`. The README states the report is consumed by top-like applications.
- `genode/repos/os/src/app/trace_logger/main.cc:105-108` skips DEAD subjects.

### 2. What is copied, and by whom

- Core writes the fixed records into the session's argument buffer. The buffer is a RAM dataspace that the session component allocates from its own quota (`trace_session_component.cc:184`) and attaches locally (`:185-193`). The record is written at `:59-62`. [read]
- The client reads through its own attach of the same buffer, in `connection.h:146-156`. [read for the client side; the buffer-sharing mechanics beyond these lines are inferred from the standard Genode argument-buffer pattern]
- The record is pointer-free: its fields are fixed-size strings, enums, integers and a location (`types.h:92-140`). [read; absence of pointers checked against the member list at `types.h:114-119`]
- The execution time value comes from the kernel:
  - base-hw: `Platform_thread::execution_time()` reads `_kobj->execution_time()` [read `genode/repos/base-hw/src/core/platform_thread.h:304-311`]. The idle-thread source reads `Kernel::main_read_idle_thread_execution_time`, which looks up the CPU and returns `cpu.idle_thread().execution_time()` [read `genode/repos/base-hw/src/core/platform.cc:281-289`, `genode/repos/base-hw/src/core/kernel/main.cc:207-213`].
  - base-foc: `Platform_thread::execution_time()` returns `{ us, 0, 10000, _prio }` with `us = 0`, because the `l4_thread_stats_time` call is commented out [read `genode/repos/base-foc/src/core/platform_thread.cc:29-37`]. The core-thread source in `base-foc/src/core/thread_start.cc:51-75` wraps the same call in `if (0)` [read `:63`]. The stated reason is a remote-CPU hang, issue #4357 [read `:59-61`]. Result on Fiasco: every thread reports zero CPU time.
  - base-sel4: `base-sel4/src/core/thread_start.cc:46-74` and `base-sel4/src/core/platform.cc:677-699` produce the value through the platform's own accessors [read the call sites; the seL4 side of those accessors was not read].
- The sandbox record is generated in init by walking the child tree and reading each child's Pd session quota numbers; it is then published as a ROM the parent serves to readers [read `child.cc:389-395`, `init/main.cc:80-82`].

### 3. Who holds the authority, and how a component without it is refused

- The TRACE session is available only through the parent's route. A child whose `<service>` node is absent is refused with `Service_denied` (`genode/repos/os/src/lib/sandbox/server.cc:76-88`), and a request that matches no child is refused the same way (`server.cc:108-114`). [read]
- Inside core, the session filter is a label prefix. The top-level label `init -> ` yields an empty filter, granting global reach; any other label filters subjects to those whose source label starts with the session label [read `genode/repos/base/src/core/include/trace/session_component.h:57-60`, `subject_registry.h:400-402` and `:413`, and the prefix strip at `:449-473`]. A monitor therefore sees only the subtree that its route names.
- The TRACE root refuses a session whose argument buffer exceeds its RAM quota with `INSUFFICIENT_RAM` [read `genode/repos/base/src/core/include/trace/root.h:33-39`].
- Reading the sandbox state ROM requires the ROM to be routed to the reader by the init configuration [inferred from the route model in `os/src/lib/sandbox/route_model.h`; the refusal path for ROM routes was not read].
- Kernel calls that expose idle-thread time are reached from core [read `base-hw/src/core/platform.cc:281-289`]. The kernel-side privilege check for that entry point was not read [not run].

### 4. Approximate size of the producing routines (lines of the function)

| Routine | Location | Lines |
|---|---|---|
| `Session_component::subject_infos()` | `base/src/core/trace_session_component.cc:48-67` | 20 |
| `Subject_registry::subjects(Subject_info*, ...)` | `base/src/core/include/trace/subject_registry.h:449-473` | 25 |
| `Subject::info()` | `subject_registry.h:290-307` | 18 |
| `Subject::_state()` | `subject_registry.h:161-180` | 20 |
| `Cpu_thread_component::trace_source_info()` | `base/src/core/include/cpu_thread_component.h:180-185` | 6 |
| base-foc `Platform_thread::execution_time()` | `base-foc/src/core/platform_thread.cc:29-37` | 9 |
| base-foc core-thread `trace_source_info()` | `base-foc/src/core/thread_start.cc:51-75` | 25 |
| base-hw idle-thread `trace_source_info()` | `base-hw/src/core/platform.cc:281-289` | 9 |
| `Sandbox::Child::report_state()` | `os/src/lib/sandbox/child.cc:362-437` | 76 |
| `top` update loop | `os/src/app/top/main.cc:82-107` | 26 |

## seL4

### 1. How a monitor obtains tasks and state

- The syscall table defines the debug and benchmark calls, and their configuration gates. [read `seL4/libsel4/include/api/syscall.xml:41-50`, `:82-87`]
  - `DebugPutChar`, `DebugDumpScheduler`: gated by `CONFIG_PRINTING` (`syscall.xml:41-43`).
  - `DebugHalt`, `DebugCapIdentify`, `DebugSnapshot`, `DebugNameThread`: gated by `CONFIG_DEBUG_BUILD` (`syscall.xml:46-50`).
  - `BenchmarkDumpAllThreadsUtilisation`, `BenchmarkResetAllThreadsUtilisation`: gated by `CONFIG_DEBUG_BUILD` and `CONFIG_BENCHMARK_TRACK_UTILISATION` (`syscall.xml:82-87`).
- `seL4_DebugDumpScheduler` dispatches to `debug_dumpScheduler()` under `CONFIG_DEBUG_BUILD` [read `seL4/src/api/syscall.c:99-109`]. The function is `static inline` in `seL4/include/api/debug.h:136-162` and calls `printf` for a header, a separator, and one row per TCB from `ksDebugTCBs` through `debug_printTCB` (`debug.h:78-134`). The output is the kernel's `printf`; no user buffer receives it [read; kernel printf target inferred from the absence of any user-buffer write in `debug.h:136-162`].
- `seL4_BenchmarkGetThreadUtilisation(tcb)` dispatches at `seL4/src/api/syscall.c:229-230` and calls `benchmark_track_utilisation_dump()` [read `seL4/src/benchmark/benchmark.c:91-95`]. That function writes a fixed set of 64-bit counters into the caller's IPC buffer (`seL4/src/benchmark/benchmark_utilisation.c:16`, `:33-53`). The counters are the requested thread's utilisation, schedule count, kernel time and kernel entries, the idle thread's counters for the local and the thread's CPU, and totals. The index enum is `seL4/libsel4/include/sel4/benchmark_utilisation_types.h:12-40`. The record has no name and no state.
- `seL4_BenchmarkDumpAllThreadsUtilisation` is a text dump. It prints the totals and then one JSON-like object per debug TCB, including `NAME` from `tcbName`, under `CONFIG_DEBUG_BUILD` [read `seL4/src/benchmark/benchmark.c:119-144`, loop at `:128`]. The output is kernel `printf`, not a user return.
- `DebugSnapshot` prints a line and calls `debug_capDL()` [read `syscall.c` debug branch; `capdl.c` body is kernel `printf`, [inferred]].
- User-space process table: none in the corpus. The `grep` in the Method section returns no task list in `seL4/src`, `seL4/include` or `seL4/libsel4`. The root task that would hold one is outside this tree [inferred].

### 2. What is copied, and by whom

- Debug dumps: the kernel formats text to its console. No record crosses to user space. [read `debug.h:136-162`, `benchmark.c:119-144`]
- Benchmark utilisation: the kernel writes a fixed array of `uint64_t` into the caller's IPC buffer at `lookupIPCBuffer(true, ...)` [read `benchmark_utilisation.c:16`, `:33-53`]. The record is pointer-free.
- Name copy: `DebugNameThread` copies a name from the caller's IPC buffer into the TCB with `strlcpy` at `seL4/src/object/tcb.c:2085-2088`. It is a write into kernel debug state, not a read [read].

### 3. Who holds the authority, and how a component without it is refused

- `DebugDumpScheduler` and `DebugSnapshot` take no capability. The only gate is the kernel build configuration [read `syscall.xml:41-50`, `syscall.c:99-109`]. Any thread in a debug kernel can invoke them, and the output goes to the console, so the console is the effective authority [inferred].
- `BenchmarkGetThreadUtilisation` requires a TCB capability in the caller's CSpace. The kernel resolves the capability with `lookupCap` and checks `cap_thread_cap` [read `benchmark_utilisation.c:18-28`]. Refusal: on a non-TCB capability, the code calls `userError` and returns without writing the IPC buffer [read `benchmark_utilisation.c:25-28`]. The message reads "SysBenchmarkFinalizeLog: cap is not a TCB, halting", which names the wrong syscall and says "halting" while the code returns. The message text is a copy-paste defect, flagged here for the upstream report, not a behavior change.
- `BenchmarkResetThreadUtilisation` follows the same check and also returns silently on a non-TCB capability [read `benchmark.c:97-115`].
- `DebugNameThread` halts the system on misuse (`syscall.c:132-157`, `halt()` at `:143`, `:149`, `:155`). It is a debug aid, not a read path.

### 4. Approximate size of the producing routines

| Routine | Location | Lines |
|---|---|---|
| `handleUnknownSyscall` (holds the debug branches) | `seL4/src/api/syscall.c:97-268` | 172 |
| `DebugDumpScheduler` branch | `syscall.c:104-109` | 6 |
| `debug_printTCB` | `seL4/include/api/debug.h:78-134` | 57 |
| `debug_dumpScheduler` | `debug.h:136-162` | 27 |
| `benchmark_track_utilisation_dump` | `seL4/src/benchmark/benchmark_utilisation.c:14-68` | 55 |
| `handle_SysBenchmarkGetThreadUtilisation` | `seL4/src/benchmark/benchmark.c:91-95` | 5 |
| `handle_SysBenchmarkResetThreadUtilisation` | `benchmark.c:97-115` | 19 |
| `handle_SysBenchmarkDumpAllThreadsUtilisation` | `benchmark.c:119-144` | 26 |

## Fiasco.OC

### 1. How a monitor obtains tasks and state

Two channels exist. Neither returns a task list to user space.

Kernel debugger (JDB, in-kernel, console):

- The thread list is `Jdb_thread_list`. `list_threads_show_thread` prints one row per thread: UID, home CPU and current CPU, name, priority, space debug id, partner, timeout, and the long state line [read `fiasco/src/jdb/jdb_thread_list.cpp:636-719`, row fields at `:638-716`]. The row does not print CPU time [read; `grep -n "consumed" fiasco/src/jdb/jdb_thread_list.cpp` returns nothing].
- Iteration: `Jdb_thread_list::iter` at `jdb_thread_list.cpp:427-530`, scheduler-queue walks for each policy (`:218-420`), and `list_threads` at `:730-851`. The module is registered as the `INFO` group (`:555-558`, `action` at `:561-618`).
- The per-thread detail view `Jdb_tcb::show` prints `cpu time` from `consumed_time()` [read `fiasco/src/jdb/jdb_tcb.cpp:606-900`, time at `:682-683`]. The other use is at `:1044`.
- The idle statistics module prints per-CPU idle and deep-sleep counts [read `fiasco/src/jdb/jdb_idle_stats.cpp:22-38`].

Kernel object interface (user space, capability-gated):

- The stats entry is `Op::Stats` in `Thread_object::invoke` [read `fiasco/src/kern/thread_object.cpp:215-217`]. It calls `sys_thread_stats`, which returns the thread's consumed time in microseconds [read `thread_object.cpp:786-803`]. The L4 interface comment names it `kernel-thread.thread-stats_time` [read `thread_object.cpp:781`]. No symbol named `Thread::stats` exists in the tree [read; grep above].
- A remote-CPU thread is read through a deferred request that runs on the thread's home CPU [read `thread_object.cpp:763-778`]. The local case first updates the consumed time when the thread is current [read `:793-797`].
- Consumed time is accrued per quantum (`fiasco/src/kern/context.cpp:1117`) and converted from CPU clock units at `context.cpp:1126-1139` [read].
- The user side is `l4_thread_stats_time_u` in `l4re-core/l4sys/include/thread.h:1052-1068`, opcode `L4_THREAD_STATS_OP = 3` at `thread.h:851`. The result is read from the UTCB as `mr64` [read `thread.h:1062-1066`].

Kernel debug objects:

- The debugger object `Jdb_object` decodes operations by group in `kinvoke`: group 1 is kernel-object debug, group 2 is trace-buffer operations, group 4 is coverage, group 5 is kernel memory statistics, and the default is JDB operations [read `fiasco/src/kern/jdb_object.cpp:361-393`]. Non-debug builds compile a stub that accepts only group 4 [read `jdb_object.cpp:455-478`].
- Group 1 (`sys_kobject_debug`, `jdb_object.cpp:345-356`) dereferences a capability named in the caller's message and passes it to `sys_invoke_debug` (`fiasco/src/jdb/jdb_kobject.cpp:154-169`). It is an invocation path into another object, not a listing.
- Group 2 maps trace-buffer status and slot pages read-only (`Rights::UR()`) into the calling task (`jdb_object.cpp:197-219`). The block is compiled only under `rt_dbg && debug` (`jdb_object.cpp:87`).
- Group 5 dumps the kernel allocator statistics to the console (`jdb_object.cpp:425-431`, `Kmem_alloc::allocator()->debug_dump()` at `:430`), compiled under `JDB_KMEM_STATS` (`fiasco/src/Kconfig:769-775`).

### 2. What is copied, and by whom

- The stats entry writes one value into the caller's UTCB: `reinterpret_cast<Utcb::Time_val *>(out->values)->t = value` [read `thread_object.cpp:800`]. The record is one 64-bit word, pointer-free.
- The JDB rows and the TCB view are written to the kernel console by the kernel [read `jdb_thread_list.cpp:636-719`, `jdb_tcb.cpp:606-900`].
- The KIP is the one fixed, read-only record that describes the whole system. It carries version, root and sigma0 instruction pointers, clock value, CPU frequency, ACPI and device-tree addresses, and the memory descriptors (`mem_descs`, `mem_descs_num`, at offset 0x50, "relative to Kip") [read `l4re-core/l4sys/include/kip.h:36-80`]. It carries no per-thread entries [read; absence checked by reading all of `l4_kernel_info_t`]. The KIP producer in Fiasco (`fiasco/src/kern/kip_init.cpp`) was not read [not run].

### 3. Who holds the authority, and how a component without it is refused

- Stats: the entry is reached through `Syscall`/`invoke` on a capability that resolves to a thread object. `Obj_cap::deref` returns the object and its rights [read `fiasco/src/kern/syscalls.cpp:31-33`, `thread_object.cpp:40-79`]. `Thread_object::invoke` passes `rights` only to `sys_control`, `sys_vcpu_control` and `sys_pf_tramp_control`; `sys_thread_stats` receives the tag and UTCB only [read `thread_object.cpp:171-243`, call sites at `:207`, `:215-216`]. `grep -n check_rights fiasco/src/kern/thread_object.cpp` returns nothing. Result: any holder of a capability to the thread object can read its consumed time, and no rights bit gates it [read; the rights bit semantics were not checked against the L4 manual].
- Refusal: a capability that does not resolve to a live object returns `Not_existent` (`syscalls.cpp:35`) [read].
- JDB: the debugger object needs its capability and an `L4_msg_tag::Label_debugger` message (`jdb_object.cpp:369-370`, `check_basics`). Group 5 is documented as requiring the JDB capability (`fiasco/src/Kconfig:769-775`). The JDB console itself needs physical access to the kernel console [inferred].
- Trace buffer mapping: `Tbuf_map_status` and `Tbuf_map_slots` map kernel buffers into the calling task only when the debug build is configured [read `jdb_object.cpp:87`, `:197-219`].

### 4. Approximate size of the producing routines

| Routine | Location | Lines |
|---|---|---|
| `Thread_object::sys_thread_stats` | `fiasco/src/kern/thread_object.cpp:786-803` | 18 |
| `Thread_object::sys_thread_stats_remote` | `thread_object.cpp:763-771` | 9 |
| `Thread_object::invoke` | `thread_object.cpp:171-243` | 73 |
| `Jdb_thread_list::list_threads_show_thread` | `fiasco/src/jdb/jdb_thread_list.cpp:636-719` | 84 |
| `Jdb_thread_list::list_threads` | `jdb_thread_list.cpp:730-851` | 122 |
| `Jdb_thread_list::iter` | `jdb_thread_list.cpp:427-530` | 104 |
| `Jdb_tcb::show` | `fiasco/src/jdb/jdb_tcb.cpp:606-900` | 295 |
| `Jdb_object::sys_kobject_debug` | `fiasco/src/kern/jdb_object.cpp:345-356` | 12 |
| `Jdb_object::kinvoke` (debug) | `jdb_object.cpp:361-393` | 33 |
| `sys_invoke_debug` | `fiasco/src/jdb/jdb_kobject.cpp:154-169` | 16 |

## L4Re (Moe and Ned)

### 1. How Moe and Ned know about tasks

Moe and Ned know only the tasks they created or were told about. Neither queries kernel time, and neither exports a list.

- Moe creates each task. `Moe_app_model` allocates the task capability from the object pool (`l4re-core/moe/server/src/loader.cc:287-291`).
- Moe's `App_task` owns the task and its region map. The constructor allocates the region map (`l4re-core/moe/server/src/app_task.cc:55-63`), and the destructor frees the thread and task capabilities (`:65-72`).
- Moe learns a task has ended only from its exit signal. `App_task::op_signal` handles signal 0 by printing a non-zero exit value, optionally dumping coverage, and deleting the task (`app_task.cc:17-53`, `delete this` at `:46`) [read].
- Moe's object pool keeps every server object on a `life` list (`l4re-core/moe/server/src/obj_reg.h:58`). `handle_irq` removes any object whose capability no longer validates (`obj_reg.h:86-95`). This is a liveness sweep over capabilities, not a task list, and it is not exported.
- Moe's name space registers `sigma0`, `mem`, `jdb` (only when the debugger capability validates) and `kip` as trusted or read-write entries (`l4re-core/moe/server/src/main.cc:801-808`). The root name space is dumped only at boot, when `boot.is_active()` (`main.cc:810-814`), through `Name_space::dump` (`moe/server/src/name_space.cc:290`).
- Ned creates tasks through the factory and region-map allocation (`l4re-core/ned/server/src/app_task.cc:39`, `:115-121`). Its `App_task` holds the Moe-side task and receives the exit signal through `Parent_receiver::op_signal` (`ned/server/src/app_task.cc:51-85`). `App_model` holds an `App_task *` (`ned/server/src/app_model.h:49`) [read].
- Ned's Lua library `Info` reads only KIP strings (`l4re-core/ned/server/src/lua_info.cc:1-80`: `kipstr`, `archstr`, `platformstr`, `mword_bits`) [read].
- Ned's Lua debug object calls `L4Re::Debug_obj::debug(n)` (`ned/server/src/lua_debug_obj.cc:26-35`). Moe implements `Debug_obj` for its memory allocator only, and only under `!NDEBUG` (`moe/server/src/alloc.cc:486-507`), printing quota and commit totals [read].

### 2. What is copied, and by whom

- Thread time: the kernel writes the value to the caller's UTCB; `l4_thread_stats_time_u` reads it back as `mr64` (`l4re-core/l4sys/include/thread.h:1052-1068`).
- Moe and Ned write debug text to their own log (`Dbg` at `alloc.cc:486-507`). No record crosses to another component.
- No pointer-free per-task record exists in Moe or Ned. The only fixed record in the L4Re tree is the KIP (`kip.h:36-80`).

### 3. Who holds the authority, and how a component without it is refused

- Thread time: holding a thread capability (see Fiasco section 3). L4Re clients reach it through `l4_thread_stats_time` (`thread.h:1149-1151`). No L4Re server calls it: `grep -rln "stats_time" l4re-core/moe l4re-core/ned l4re-core/libkproxy l4re-core/l4re` returns no file.
- Moe name space: entries carry flags such as `F_trusted` and `F_rw` (`main.cc:801-808`). The entry flags determine what a client receives; the lookup code that enforces them was not read [inferred].
- Debug object: the `debug` call needs the object capability; a failed lookup returns an error through the name space [inferred from `ned/server/src/lua_debug_obj.cc:29-32`].

### 4. Approximate size of the producing routines

| Routine | Location | Lines |
|---|---|---|
| `App_task::op_signal` (exit path) | `l4re-core/moe/server/src/app_task.cc:17-53` | 37 |
| `App_task` constructor | `moe/server/src/app_task.cc:55-63` | 9 |
| `Allocator::op_debug` | `moe/server/src/alloc.cc:486-507` | 22 |
| `Object_pool::handle_irq` | `moe/server/src/obj_reg.h:86-95` | 10 |
| `App_task::Parent_receiver::op_signal` | `ned/server/src/app_task.cc:51-85` | 35 |
| `l4_thread_stats_time_u` | `l4sys/include/thread.h:1052-1068` | 17 |
| `__debug` (Lua) | `ned/server/src/lua_debug_obj.cc:26-35` | 10 |

## Cross-system summary

| System | Who lists tasks | Record | Copied by | Authority | Producer size |
|---|---|---|---|---|---|
| Genode | `top`, `trace_subject_reporter`, `trace_logger` via TRACE `subject_infos` | Fixed `Subject_info` (label, name, state, policy, execution time, affinity), pointer-free | Core writes into the session argument buffer | Parent route; label-prefix filter; `init -> ` for global reach | 18 to 25 lines per routine; 76 for the sandbox report |
| Genode sandbox | `init` state ROM | XML: child name, binary, RAM and cap quota and use; no threads, no CPU | Sandbox library in init, served as ROM | Route to the ROM in init configuration | 76 lines |
| seL4 | None in user space. Debug dumps go to the console. Utilisation counters per TCB | Fixed `uint64` array (utilisation counters); text dump for debug builds | Kernel writes to the caller's IPC buffer, or prints | Debug build for dumps (no capability); TCB capability for counters | 55 lines (counters); 57 and 27 (dump) |
| Fiasco.OC | JDB console list; no user-space list | One 64-bit consumed-time word per call; KIP for the system | Kernel writes to the caller's UTCB; JDB prints to console | Capability to the thread object (no rights check on stats); debugger object for JDB | 18 lines (stats); 84 and 122 (JDB) |
| L4Re | None. Moe and Ned know only what they created | None per task; KIP for the system | Not applicable | Thread capability for time; none for listing | 35 to 37 lines for exit handling |

## Implications for the DiscoBSD `ps(1)`

The four trees agree on one pattern: the kernel or a trusted server publishes a fixed, bounded record, and the caller holds a capability or a route that names the subject. None of them lets the caller parse kernel memory. For the port, the analogue is a kernel-produced, pointer-free per-process snapshot (a fixed record array with a version field) reached through a syscall whose authority is the process's own capability or uid. The swap ring and the memory-driver refusal list then stay private to the kernel and the swap driver. The Genode finding on Fiasco is the warning for the port: a per-thread time value that the kernel can refuse to return must be reported as unavailable, not as zero. [inferred from the four patterns; no port code was read]

## Checks not run

- No build, boot or execution of any tree (read-only task).
- Fiasco KIP producer (`fiasco/src/kern/kip_init.cpp`) and the KIP mapping: not read.
- Kernel-side privilege check for the base-hw idle-time kernel call: not read.
- seL4 `debug_capDL` body (`seL4/src/arch/*/machine/capdl.c`): sampled only; the console-output claim rests on the `printf` calls seen at `capdl.c:91-102` for the ARM 32-bit file.
- Fiasco capability rights bit semantics for `Op::Stats`: not checked against the L4 manual.
- The 56 `/dev/mem`-pattern matches in `l4re-core`: sampled; the libc and libstdc++ path constants were the ones inspected.
