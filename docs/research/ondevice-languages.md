# Language runtimes beyond C for DiscoBSD on RP2040 Pico

Target: DiscoBSD (2.11BSD-derived) userland process on Raspberry Pi Pico,
RP2040, Cortex-M0+ (ARMv6-M, Thumb-1, no hardware divide, no FPU), no MMU.
Budget: at most 96 KB text+data+stack per process, a.out format from
`arm-none-eabi-gcc -Os -mcpu=cortex-m0plus` via `elf2aout`, 2.11BSD libc
(partial POSIX, no dynamic linking, no mmap, no threads), 840 KB root
filesystem with about 340 KB free. Already in the tree: basic, forth,
pforth, retroforth, scm, tclsh/libtcl, picoc, awk, lcc, smlrc.

Every number below is either a figure a project's own docs/mailing list
state, or is marked as an estimate/inference with the basis for the
estimate stated. Nothing here has been build-tested against DiscoBSD's
actual a.out toolchain; all port-effort steps are therefore predictions,
not measurements.

## MicroPython

MicroPython ships three relevant surfaces: `ports/minimal` (bare-metal or
host, a stripped reference port), `ports/unix` (a full-feature hosted
build), and `ports/rp2` (bare-metal RP2040, not applicable here since
DiscoBSD already owns the hardware).

- **Code size.** The `ports/minimal` README states that removing the
  bytecode compiler (`MICROPY_ENABLE_COMPILER=0`, i.e. import-only,
  precompiled `.mpy` execution) saves "about 20k on a Thumb2 machine, and
  about 40k on 32-bit x86" relative to the full minimal build
  (https://github.com/micropython/micropython/tree/master/ports/minimal).
  That implies the minimal-with-compiler Thumb2 binary lands in the
  neighborhood of 100-150 KB of code -- MicroPython does not publish an
  ARMv6-M-specific number, and Thumb2 (Cortex-M3/M4) is not Thumb-1
  (Cortex-M0+): Thumb-1's narrower encoding space and the absence of
  hardware divide typically make Cortex-M0 code larger than the same
  source built Thumb2, so treat 100-150 KB as optimistic for ARMv6-M.
- **RAM floor.** MicroPython's own guidance for constrained ports treats a
  64 KiB heap as a practical minimum on ESP32-class targets that split
  heap from the interpreter's own static/BSS footprint
  (https://docs.micropython.org/en/latest/develop/memorymgt.html,
  https://github.com/orgs/micropython/discussions/12316). STM32F405-derived
  ports, MicroPython's original reference target, run comfortably only
  with roughly 115 KB of heap out of 128 KB total RAM
  (https://python.otthydromet.com/html/reference/constrained.html). A 96
  KB text+data+stack ceiling has to hold the interpreter's code AND its
  heap in the same budget (DiscoBSD gives no separate heap region for a
  user process); no MicroPython port has ever shipped in that combined
  envelope. Even the most aggressive minimal-port number (compiler
  stripped, ~80-100 KB code) leaves at most a few KB for gc heap, which is
  not enough to parse and run a nontrivial precompiled `.mpy` module, let
  alone hold the compiler.
- **Unix-hosted vs. bare metal.** `ports/unix` is the correct model for a
  DiscoBSD *process* (it runs under a host OS, uses argv/stdio, and can in
  principle target `armv6m` per MicroPython's own architecture list on
  Wikipedia's summary of supported targets), but the *standard* variant
  pulls in `libffi` via pkg-config for its `ffi` module and expects POSIX
  facilities (regex, threads optionally, dynamic module loading) that
  2.11BSD's libc does not provide
  (https://docs.circuitpython.org/en/stable/ports/unix/README.html). The
  `minimal` unix variant avoids libffi, but MicroPython's readline/os
  glue still assumes POSIX termios and a real `fork`/`exec` environment
  for some subsystems even when features are `#undef`'d out; none of this
  has been verified against 2.11BSD's libc surface specifically.
- **libc needs.** setjmp/longjmp (used for the VM's exception handling),
  malloc (MicroPython manages its own heap inside one big `malloc`'d or
  statically-declared arena), basic stdio, and -- for the unix port's
  default build -- libffi and pkg-config at build time (host-side, not
  runtime, but the resulting binary links against ffi that 2.11BSD lacks
  as a package).
- **License.** MIT
  (https://github.com/micropython/micropython/blob/master/LICENSE).
- **Maintenance.** Very active; github.com/micropython/micropython has
  continuous commits and releases as of 2026.
- **Port-effort estimate (imperative).**
  1. Build `ports/unix` with `VARIANT=minimal` and
     `MICROPY_ENABLE_COMPILER=0` for `arm-none-eabi-gcc -Os
     -mcpu=cortex-m0plus -mthumb`, cross-linked against 2.11BSD's libc
     stubs rather than glibc/musl.
  2. Stub or remove every unix-port file operation MicroPython assumes is
     POSIX-complete (ioctl-based readline, `os.stat` fields DiscoBSD's
     libc does not fill, any thread/select use).
  3. Replace elf2aout's expectations: confirm the resulting ELF has no
     PLT/GOT relocations MicroPython's build system might introduce via
     libffi -- drop libffi entirely, which removes the `machine`/`ffi`
     modules.
  4. Measure the actual a.out text+data size; if it exceeds roughly 80 KB
     leaving under 16 KB for gc heap, strip further built-in types
     (float, complex, most of `sys`) or accept the port cannot run
     anything beyond a "hello world" script.
  5. Expect this to be a multi-week port with a real risk of ending at a
     working image too memory-starved to be useful -- the 96 KB ceiling
     is the binding constraint, not compiler flags.

**Verdict: does not fit.** Even the most stripped MicroPython
configuration documented anywhere is sized against budgets 1.3-2x the
96 KB combined ceiling here, and that's before subtracting stack and any
heap for user code.

## Lua 5.4 and small-footprint configurations

- **Code size.** Lua 5.3.4 full build with `-Os` compiles to about 230 KB
  including the standard library set
  (http://lua-users.org/lists/lua-l/2018-01/msg00053.html). Excluding
  unused standard libraries (leaving base/table/string, which Lua 5.4
  defaults to for minimal builds) brings the *core* engine to roughly 100
  KB, and removing the parser/compiler entirely (running only
  precompiled bytecode via `luac`) brings it to about 40 KB
  (http://lua-users.org/lists/lua-l/2018-01/msg00053.html,
  https://lua-l.lua.narkive.com/C4OklKfV/size-optimizations-for-lua-5-3-vm-on-arm-cortex-m4).
  These are x86/generic figures from mailing-list reports, not
  ARMv6-M-measured; Thumb-1's code density is worse than Thumb2/x86 for
  branch-heavy interpreter loops, so expect these numbers to grow, not
  shrink, on Cortex-M0+.
- **RAM floor.** Historical Lua 5.1.4 RAM consumption at interpreter
  startup was about 17 KB, rising past 25 KB on some eLua platforms with
  extra modules (http://wiki.eluaproject.net cited via
  https://eluaproject.net/doc/v0.9/en_arch_ltr.html). eLua's "LTR" (Lua
  Tiny RAM) patch specifically targets cutting this for embedded use.
  Lua 5.4 can be built with 32-bit integers and single-precision floats
  (`LUA_32BITS`) to shrink both code and per-value RAM cost, which is
  directly relevant on an FPU-less, no-hardware-divide core
  (https://www.lua.org/manual/5.4/manual.html).
- **libc needs.** Lua's core needs malloc/realloc/free, setjmp/longjmp
  (for `pcall`), string.h, and stdio only if `lauxlib`/`lbaselib`'s
  print/io functions are compiled in; it has no threading or mmap
  dependency, which matches 2.11BSD's libc well.
- **License.** MIT (https://www.lua.org/license.html).
  Actively maintained by PUC-Rio; releases continue on a slow, stable
  cadence.
- **Fit estimate.** A parser-included, base+string+table-only Lua 5.4
  build at `-Os` for Thumb-1, with `LUA_32BITS`, plausibly lands in the
  60-90 KB code range (extrapolating upward from the ~100 KB x86 core
  figure to account for Thumb-1 density loss), leaving 6-36 KB for
  data+stack+heap in a 96 KB process -- tight but not obviously
  impossible for small scripts, unlike MicroPython. A bytecode-only Lua
  (parser stripped, scripts precompiled on the host with `luac` and
  shipped as `.luac` files) at an estimated 30-50 KB code leaves 45-65 KB
  for heap, which is workable for real programs.
- **Port-effort estimate (imperative).**
  1. Configure Lua 5.4 with `LUA_32BITS` and a custom `luaconf.h` that
     drops `os`/`io`/`debug`/`utf8` libraries, keeping base+string+table.
  2. Cross-compile with `arm-none-eabi-gcc -Os -mcpu=cortex-m0plus
     -mthumb`, replacing Lua's POSIX `os.*` shims (time, clock) with
     2.11BSD equivalents or `#ifdef`-excise them.
  3. Run through `elf2aout`; measure actual text+data; if over ~70 KB,
     rebuild `luac`-only (strip `lparser.c`/`lcode.c`/`llex.c`) and ship
     precompiled chunks.
  4. Verify setjmp/longjmp-based error handling works under 2.11BSD's
     libc (2.11BSD has BSD-heritage setjmp; should be fine, but confirm
     `LUAI_THROW`/`LUAI_TRY` map cleanly with no `ucontext` dependency).
  5. This is a days-to-one-week port given Lua's minimal external
     dependencies -- the most tractable of the interpreters surveyed.

**Verdict: fits, with a parser-stripped or feature-trimmed build; the
best-supported candidate here after Forth/Tcl already in the tree.**

## Tiny C++

There is no C++ *interpreter* comparable to picoc for C; the honest
answer is cross-compiling C++ on the host with `arm-none-eabi-g++` and a
minimized libstdc++, never running a C++ toolchain on-device.

- **Flags.** The Raspberry Pi Pico SDK itself defaults to
  `-fno-exceptions -fno-unwind-tables -fno-rtti -fno-use-cxa-atexit`
  unless a project opts back in
  (https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/discussions/23),
  which is the standard recipe for keeping libstdc++ out of a
  size-constrained ARM build; exceptions specifically pull in unwind
  tables and `__cxa_throw`/personality routines that dominate the size
  delta between plain C and "exceptions-on" C++.
- **What a minimal build drags in.** Even with exceptions and RTTI off,
  a trivial C++ program that touches only `<cstdio>`-style printf-based
  I/O and no `<iostream>`/`<string>`/`<vector>` avoids most of
  libstdc++; the moment `<iostream>` is included, static global
  constructors for `std::cout`/`std::cin` and their locale machinery pull
  in tens of KB regardless of `-fno-exceptions`, because iostream's
  formatting and locale code is exception-based internally in stock
  libstdc++. A hello-world using only `printf` under `extern "C"` linkage
  and no libstdc++ headers is essentially identical in size to the
  equivalent C hello-world; a hello-world using `std::cout` is
  substantially larger (no single authoritative number was found in this
  research for the specific arm-none-eabi hello-world case -- treat any
  size claim here as unverified without a local build).
- **License.** GCC/libstdc++: GPLv3 with the GCC Runtime Library
  Exception (i.e., linking against it does not GPL the application) --
  a build-time toolchain concern only, not something shipped on the
  DiscoBSD image.
- **Fit estimate.** A C++ subset restricted to no exceptions, no RTTI, no
  iostream, no STL containers with heap churn (i.e., "C with classes and
  templates," similar to what embedded RP2040 firmware projects already
  do) compiles to code sizes close to equivalent C and is a viable
  on-device *target* for cross-compiled binaries; it is not a
  ready-made interpreter for the tree and does not compete with the
  awk/lcc/smlrc/picoc slot -- it is orthogonal, since lcc already covers
  "compile on host, run a.out on device" for C, and a similarly
  configured arm-none-eabi-g++ would cover the same for restricted C++
  without adding an on-device runtime at all.
- **Port-effort estimate (imperative).**
  1. Confirm `arm-none-eabi-g++` (or extend lcc/build a small cfront-style
     subset compiler) is available in the existing cross toolchain used
     for DiscoBSD's a.out target.
  2. Compile a smoke test with `-fno-exceptions -fno-rtti
     -fno-use-cxa-atexit -Os -mcpu=cortex-m0plus`, linking a minimal
     libstdc++ subset or none at all (avoid `<iostream>`,
     `<string>`, STL containers; use `<cstdio>` and raw arrays/structs).
  3. Verify `elf2aout` handles any C++-specific sections (`.init_array`
     for global constructors) the same way it already handles C's;
     2.11BSD's crt0 needs to run global constructors if any are used, or
     the build needs `-fno-use-cxa-atexit` plus manual constructor
     avoidance.
  4. This is not a new runtime to "port" -- it is a toolchain
     configuration and libc-compatibility exercise, on the order of a day
     once the C a.out toolchain (already proven by lcc/smlrc) is
     confirmed to handle C++ object sections.

**Verdict: no on-device C++ interpreter exists or is appropriate; the
correct answer is restricted cross-compiled C++ with libstdc++'s
exception/iostream machinery avoided, which is a toolchain task, not a
runtime-port task.**

## Forth alternatives

DiscoBSD already carries forth, pforth, and retroforth.

- **Mecrisp / Mecrisp-Stellaris.** A native-code (not interpreted)
  Forth compiler family for MSP430, ARM Cortex-M0/M3/M4, RISC-V, and
  others; Forth cores need under 20 KiB flash and 4 KiB RAM on ARM
  targets (https://mecrisp.sourceforge.net/). License: GPLv3
  (https://sourceforge.net/projects/mecrisp/). Mecrisp compiles Forth
  source to native machine code with its own tiny assembler baked in
  rather than running as a bytecode interpreter under a host OS; it is
  designed to *be* the firmware on bare metal, talking to hardware UART
  directly, not to run as a hosted Unix-style process reading argv/files
  through a libc -- porting it to run as a DiscoBSD process is a much
  larger rewrite than picoc/pforth already required, since the entire
  premise (Mecrisp *is* the boot image) conflicts with "ordinary process
  under an OS."
- **pForth vs. what's present.** pForth (already in the tree) is public
  domain and measured at about 204 KB
  (https://en.wikipedia.org/wiki/PForth), which is larger than
  Mecrisp's flash footprint but pForth is a portable ANSI-C
  *interpreter* meant to run hosted -- which is exactly the shape needed
  here, and it is already in the tree. There is no evidence any other
  actively maintained Forth is both smaller than pforth/retroforth *and*
  designed to run as a hosted process rather than bare-metal firmware.
- **Verdict: no net-new Forth is worth adding.** Mecrisp is the
  standout alternative on paper (sub-20 KB) but its bare-metal design
  fights the "ordinary DiscoBSD process" requirement harder than the
  size savings justify; forth/pforth/retroforth already in the tree
  cover the hosted-Forth niche.

## Scheme, Lisp, BASIC alternatives to scm

- **s7 Scheme.** A fork of TinyScheme by Bill Schottstaedt (CCRMA/Stanford),
  used as the embedded scripting engine in Snd, Common Music, Grace, and
  Radium; described by its users as "fast, small, and easy to hack on and
  to embed" (https://scheme-for-max-docs.readthedocs.io/en/latest/s7.html).
  License: BSD
  (https://github.com/radiganm/s7,
  https://cm-mail.stanford.edu/pipermail/cmdist/2015-January/007131.html).
  It is mostly R4RS plus music-scripting extras. No authoritative
  compiled-size number for ARMv6-M was found in this research; s7 is
  distributed as a single large `s7.c` (known to be substantially bigger
  than TinyScheme's original ~1500-line core because of s7's added
  numeric tower and extras) -- treat s7 as *not* smaller than the scm
  already in the tree without a build-and-measure step.
- **femtolisp.** A Scheme-like Lisp by Jeff Bezanson (later the seed for
  Julia's own femtolisp-derived parser), under 150 KB of C source with 12
  special forms and 33 builtin functions, proper tail calls, and BSD-3
  license (https://github.com/JeffBezanson/femtolisp). Source size is not
  compiled-binary size, but a sub-150 KB C source with a deliberately
  minimal core is a reasonable candidate for a compiled footprint
  competitive with or smaller than scm's. Maintenance: femtolisp itself
  sees infrequent standalone commits (its main continued life is inside
  Julia's internals), which is a risk for long-term support independent
  of the codebase's small size.
- **chibi-scheme.** A small, no-external-dependency R7RS-base library
  scheme meant for embedding in C programs, actively maintained on GitHub
  (ashinn/chibi-scheme), BSD/MIT-style license
  (https://github.com/ashinn/chibi-scheme). Chibi supports a full numeric
  tower, Unicode, and hygienic macros -- more feature-complete than scm,
  which likely means a larger, not smaller, footprint; chibi's own
  documentation markets it as "very small" relative to full R7RS
  implementations, not relative to a minimal Scheme like the tree's scm.
- **BASIC.** basic is already in the tree; no evidence surfaced of a
  smaller or better-maintained standalone BASIC interpreter worth adding
  alongside it -- most modern small-BASIC projects (e.g., various
  "TinyBasic" forks) target microcontroller firmware, not hosted Unix
  processes, and are not more capable than what is present.
- **Verdict: no clearly superior replacement for scm.** femtolisp is the
  most plausible smaller/comparable option on source-size grounds (BSD,
  <150 KB C, minimal core) but its size was not confirmed against a
  compiled Thumb-1 `-Os` binary in this research, and its niche
  maintenance status (mostly living on inside Julia) is a real risk.
  s7 and chibi-scheme both trend toward *larger*, not smaller, than the
  existing scm because they add feature completeness scm does not have.

## JavaScript engines for microcontrollers

- **Elk (Cesanta).** About 20 KB on flash/disk, about 100 bytes RAM for
  the core VM, described as running from 8-bit microcontrollers up to
  64-bit servers (https://github.com/cesanta/elk). License: dual
  AGPLv3 / commercial (https://github.com/cesanta/elk/blob/master/LICENSE)
  -- AGPLv3 is a materially different obligation than the MIT/BSD
  licenses on everything else in the tree and needs an explicit decision
  before inclusion, not an assumption that "small and open source" is
  sufficient. Elk implements a small usable ES6 subset (no closures over
  mutable state in some versions, limited standard library) -- check the
  README's feature list against what target scripts actually need before
  committing; it is not full JavaScript.
- **mJS (Cesanta).** About 50 KB flash, under 1 KB RAM on 32-bit ARM
  (https://github.com/cesanta/mjs). License: dual GPLv2 / commercial
  (https://github.com/cesanta/mjs/blob/master/LICENSE) -- GPLv2 is
  copyleft but less viral than Elk's AGPLv3; still a license-posture
  question for the tree, which otherwise favors MIT/BSD/public-domain
  permissive licenses (2.11BSD, MIT MicroPython/Lua, BSD femtolisp/s7,
  public-domain pforth). mJS supports a wider (though still partial) ES6
  subset than Elk, at roughly 2.5x Elk's flash cost.
  Both mJS and Elk are Cesanta-maintained and see ongoing but modest
  commit activity; both are designed explicitly for embedding in C/C++
  applications via a small API, which fits "ordinary DiscoBSD process"
  well -- both are libraries meant to link into a host program, so the
  actual runtime footprint on device is this library's size plus
  whatever wrapper program embeds it (comparable in shape to how tclsh
  wraps libtcl in the existing tree).
- **Espruino.** Needs at least 128 KB flash and 8 KB RAM at the
  absolute low end, and is designed and documented overwhelmingly as
  bare-metal microcontroller firmware, not a hosted Unix process
  (https://github.com/espruino/espruino,
  https://en.wikipedia.org/wiki/Espruino). License: MPL 2.0
  (https://github.com/espruino/Espruino/blob/master/LICENSE). No
  evidence found of an actively used "Espruino as a Unix binary" mode
  comparable to MicroPython's `ports/unix` -- Espruino can be *built*
  under Linux for development/testing purposes per its own build docs,
  but that is a host development target, not a shipped process runtime
  analogous to what this survey needs. Espruino's 128 KB flash floor
  alone exceeds the 96 KB combined budget before RAM is even counted.
- **Verdict on JS engines:** Elk is the only one that plausibly fits the
  96 KB budget with real headroom (20 KB code, ~100 B RAM core, leaving
  the rest of the budget for the engine's own working set and any
  embedding glue) -- but AGPLv3 is a hard licensing mismatch against a
  tree that is otherwise permissively licensed, and its JS subset is
  limited. mJS is the second choice on size (50 KB) with the lesser
  copyleft (GPLv2) and a broader subset. Espruino does not fit the
  process model or the size budget at all.
- **Port-effort estimate for Elk/mJS (imperative).**
  1. Pull the single-file (or near-single-file) C source; both engines
     are designed for drop-in embedding with minimal build
     configuration.
  2. Write a thin `main.c` wrapper (argv-driven script loader, akin to
     tclsh's role over libtcl) and compile with `arm-none-eabi-gcc -Os
     -mcpu=cortex-m0plus`.
  3. Confirm the engine's libc assumptions (malloc, setjmp for error
     handling, no threads) match 2.11BSD; both engines' embedding
     documentation claims minimal libc dependencies by design.
  4. Resolve the license question explicitly before adding to the image
     -- AGPLv3 (Elk) or GPLv2 (mJS) vs. commercial licensing options
     Cesanta offers, and document the choice since it diverges from the
     tree's existing permissive-license pattern.
  5. This is close to a one-to-two-day port given both engines' stated
     design goal of dropping into exactly this kind of constrained C
     embedding target.

## Ranked shortlist (at most three)

1. **Lua 5.4, parser-stripped or feature-trimmed (base+string+table,
   `LUA_32BITS`).** MIT license matches the tree's existing pattern
   (2.11BSD, MicroPython, femtolisp all MIT/BSD). Minimal libc surface
   (malloc, setjmp, no threads/mmap) is the best match for 2.11BSD's
   libc of anything surveyed. Documented core sizes (40-100 KB
   depending on parser inclusion) leave real headroom in 96 KB, and Lua
   is a materially more capable general-purpose language than anything
   currently in the tree except Tcl. Estimated port effort: days, not
   weeks.
2. **Elk, if AGPLv3 is acceptable, else mJS under GPLv2.** Smallest
   footprint of any full-language runtime surveyed (20 KB / ~100 B RAM
   for Elk; 50 KB / <1 KB RAM for mJS), purpose-built for exactly this
   embedding scenario, and would give the tree a JavaScript option it
   currently lacks entirely. The license is the blocking question, not
   the technical fit -- resolve it before committing engineering time.
3. **femtolisp, contingent on a build-and-measure step.** The only
   Scheme/Lisp candidate whose source size (<150 KB C, BSD-3) plausibly
   undercuts or matches the tree's existing scm, but no compiled Thumb-1
   size figure was found in this research, and its maintenance now lives
   mostly inside Julia's internals rather than as a standalone project --
   treat this as the option to prototype first and discard quickly if
   the measured binary does not actually beat scm.

**Explicitly rejected:** MicroPython (every documented configuration,
even maximally stripped, runs 1.3-2x over the 96 KB combined
code+heap ceiling, and no ARMv6-M-hosted-Unix number exists to suggest
otherwise); Espruino (128 KB flash floor alone exceeds the budget, and
it is not designed to run as a hosted process); Mecrisp (smallest Forth
by far, but architecturally a bare-metal firmware generator, not a
hosted-process interpreter, which fights the "ordinary DiscoBSD process"
requirement); chibi-scheme and s7 (both trend larger than the existing
scm due to added feature completeness, not smaller).
