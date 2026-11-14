# MATEzinho implementation validation

Baseline: 11a3f970 (main). Host: Debian 13 x86-64; GCC ARM 14.2.1; QEMU user
10.0.13; Renode 1.17.0+20260907gitf1dd1b4af; .NET 8. Source and commands are
owned by sys/arch/rp2040/doc/MATEZINHO.md. No physical device was accessed.

| Evidence | Result |
| --- | --- |
| Full `logged-build` | Passed; the installed native app was also rebuilt after its final input-adapter change |
| Native app ELF | 13,863 text + 204 data + 8,024 BSS = 22,091 bytes; runtime stack excluded |
| QEMU app ELF | 43,684 text + 1,760 data + 37,952 BSS = 83,396 bytes; includes 16 KiB heap, 8 KiB stack, bounded argument bytes/pointers |
| Host state/render gate | Passed ASan/UBSan, 1500 seeded actions, bounded note input and incremental/full equality |
| Asset/storage gate | Passed hash verification, malformed/truncated refusal, existing-image refusal, sparse capacities and notice companion creation |
| QEMU application gate | Seven frames identical to native; note save and argument-limit refusal passed |
| QEMU CPU controls | Thumb-2 SIGILL and unaligned-load SIGBUS on cortex-m0; max CPU accepts the negative controls |
| Interactive wrapper | Tk/Xvfb and actual QEMU: open Notes, type, save, return to desktop and quit passed |
| SD protocol gate | Production sdspi.c against FileSD: 16 MiB, 32 MiB, approximately 32 GB; identification, capacity, last-sector read/write, read-only refusal; disconnected transport rejected |
| Root filesystem profile | Built and booted; 98 files, 648 used and 331 free blocks reported by guest fsck |
| Renode integration | Login at 10.5 virtual seconds, keyboard-driven desktop launch, Files card matched reference, 75 SPI sector reads |
| Renode Notes | Captured typed `Hello PicoCalc` body compared with native reference: maximum RGB-channel difference 2 |
| Frame negative control | A corrupted captured pixel exceeded the tolerance and was rejected |
| Lint and source bookkeeping | Root ShellCheck/ruff passed; C17 denominator synchronized for six added C/header files; 21 denominator mutation controls rejected |

The first keyboard injection intentionally failed through the modeled FIFO:
a long unpaced command overran its 31 press/release events. Bounded 12-character
bursts, with virtual-time drains and zero-drop checks, then launched the app.
The first staged desktop entry inherited a data-only manifest mode; the shell
refused execution. Explicit mode 0775 corrected the profile, and the rebuilt
root passed the subsequent launch. Those failed runs are not counted as passes.

LeakSanitizer is incompatible with the execution host's ptrace arrangement;
ASAN_OPTIONS=detect_leaks=0 was applied only to the local sanitizer invocation.
AddressSanitizer and UndefinedBehaviorSanitizer remained enabled. The core uses
no heap allocation, and no LeakSanitizer result is claimed.

Broader gates blocked by host setup: `logged-ilp32` cannot compile/execute its
first 32-bit binary because host multilib is absent. `check-architecture-isolation`
encounters the existing STM32 /usr/bin cross-compiler path and missing
picotool-generated UF2 artifacts. These are recorded blockers, not passing
checks. Physical PicoCalc testing, full 32 GB guest filesystem addressing,
cycle-accurate timing, panel colorimetry, PTY terminals and monotonic syscall
support are outside the established evidence.

The fetched RP2040 model project emits 25 pre-existing third-party C# build
warnings. FileSD compiled against real Renode interfaces without a new warning;
its standalone driver-seam project treats warnings as errors. Existing model
warnings during boot retain the limits already described by picocalc-emulation.md.
The whole-system GUI capture is docs/screenshots/matezinho-renode.png.
