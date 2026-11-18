# Archive mechanisms and their successors

The table covers the eleven archive tips present at main `0a211d15e`.
Commit IDs identify the exact inputs even when the branch names are retired.
The linked-image and build checks qualify the mined mechanisms. The archived
hardware observations apply to the revisions and workloads that produced them.

| Archive tip | Disposition |
| --- | --- |
| `7eff0461` (board libc pselect) | The shipped archive includes `pselect`. `sleep-timeout-bound.md` carries the old GCC 14.2.1 packed-size observation. The current adminbox builds under its packed-size and margin checks. The 26-block experiment applies to the older measured build. |
| `7aed0038` (C17 and Thumb layout) | Native assembly owns ISO preprocessing and confines a.out object outputs to their archive directory. The linked syscall check covers trace name spans and their configured omission. Strict prototypes already reach the four kernel units through `C17_PROTO_FLAGS`. The archive holds the compact-dispatch and UART scratch experiments described in `kernel-footprint-pr435-closeout.md`. Compact dispatch needs whole-kernel workload evidence and scratch packing needs a current measurement before either changes a shipped configuration. |
| `8edf8af0` (accounting headers) | The live record-header gate checks the current declarations, both time views and each installed record layout. It passes 672 valid compiler cases and rejects 45 controls, with 56 additional Smaller C inputs. The prototype pins `lastlog.ll_time` to the earlier type; the current record uses `utime32_t` under both time views. |
| `55b79ded` (stdio EOF evidence) | `stdio-eof-fixed-point-qualification.md` cites the exact source and approved resource vector. The evidence ref preserves their correspondence. |
| `9c623881` (signed 64-bit time prototype) | `time-representation-past-2038.md` cites this build experiment and its ABI gaps. The current libc carries a 64-bit view through wrappers that preserve the existing kernel and file interfaces. |
| `0db61ca4` (unsigned 32-bit time prototype) | The same time-representation study cites this exact measurement input. The prototype changes the public type in place and remains a size comparison. |
| `96d761e7` (PicoCalc hardware checklist) | The configuration-retirement criteria live in `rp2040-single-kernel.md` and the panel result in `sys/arch/rp2040/doc/PICOCALC.md`. The old reverts would remove current MPU and capacity fixtures. Cold-start keyboard, physical swap and the core 1 ownership decision remain open. |
| `2c35eff2` (free-list sweep) | The ref preserves the deferred-sweep workload oracle and the trim experiment at its ancestor `2d154315`. The current collector and recovery gates cover the landed policy. See `unimplemented-storage-work.md` for the workload that reopens the sweep. |
| `7fb26dbe9` (desktop font trial) | MATEzinho uses the proportional-advance mechanism for tile titles with the installed bitmap glyphs. The host pixel fixture and QEMU frame comparisons cover that path. The external-font trial depends on generated glyph tables absent from the source tip. |
| `2d154315` (trim at free) | This exact tip is an ancestor of the retained sweep ref. On the measured bare Pico, eight compiles erased 1,592 root sectors with trim at free and 1,374 without it. The reset-cut oracle stays with the trim implementation. |
| `af9841e0` (swap headroom snapshot) | `swap-exhaustion/review-06.md` carries the measured successor of the trial. Current in-place exchange allocates the tail before media writes. `check-swap-exchange-headroom` rejects reintroduction of the resident flash route. |

The archive-only replays remain revision-scoped. A firmware build or emulator
run establishes its configured recipe and the exercised behavior. A board
observation supports the measured device and workload.
