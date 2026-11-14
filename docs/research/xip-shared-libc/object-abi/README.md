# Executable-tail libc object identity experiment

The fixture establishes one prerequisite for shared libc: application references
and position-independent library references reach the same process-local public
objects when the linker reserves library state after application BSS. The
experiment compiles real data owners, converts each executable through the
production `elf2aout`, and executes ARMv6-M instructions in QEMU user mode.
The implementation lives in `tests/libc_xip_object_abi`. It implements an
experiment, not an admitted shared executable format.

## Inputs and mechanism

The production source baseline is
`e5ec645d8f4f0782c5fc361a026d5ddec514ce7d`. The fixture compiles these owners:

| Input | Retained data |
| --- | --- |
| `lib/libc/stdio/exit.c` | `errno` |
| `lib/libc/stdio/findiop.c` | Eight-entry `_iob` and its standard-stream initializers |
| `lib/startup-arm/crt0.c` | `environ` and `__progname` |
| `lib/libc/arm/sys/sbrk.c` | `_curbrk`, initialized from the application end |

Section garbage collection retains the exercised objects and fixture functions.
The surrounding exit, allocation, startup and stream implementations remain
outside the execution claim. The five imported objects keep their public names.
Three additional pointer cells exercise initialized and zero-valued state.

Library text occupies `0x10020000`; its writable link-time addresses begin at
zero. GCC compiles the library with `-fPIC -msingle-pic-base -mpic-register=r9
-mno-pic-data-is-text-relative`. The application reserves `r9` and uses ordinary
absolute object imports. ELF inspection derives object offsets, sizes, GOT
origin and relocation locations. A generated linker fragment reserves an
eight-byte-aligned `.libc_state` after BSS, using `lib/elf32-arm.ld` and, for
multicall, `tools/generate_multicall_bss_overlay.sh`.

For state base B and object offset d, both paths must produce B + d. Startup
copies initialized bytes, zeros the remaining reservation and adds B only to
the identified writable state references. `_curbrk` then equals the final
application `_end`. Initialized application pointers require no runtime text
patching. Callback execution crosses into the application and back into the
library while preserving the same object identities.

The tested library uses 272 bytes of state, including fixture-specific objects
and GOT entries for the null-import controls.
That value measures neither full-libc residency nor available process capacity.
Function addresses come from the linked fixture rather than stable ABI slots.
The layout description is experimental and lacks a production compatibility
identity and complete object-alignment metadata.

## Deciding observations

The compiler's `R_ARM_GOT32` displacements use `.got` as their origin in the
tested layout. The driver derives this origin from the linked ELF, then checks
displacement alignment, containment and the referenced value for each observed
GOT relocation. A guessed base is insufficient even when the resulting pointer
stays inside reserved state.

Every GOT cell requires a symbol owner from a retained relocation. Allocated
writable sections inside the state reservation identify relocatable objects;
allocated fixed sections belong to the flash image. Undefined weak symbols
retain null values, and absolute symbols retain fixed values. The named `_end`
symbol identifies the state end and relocates with it. Conflicting ownership,
unclassified cells and unresolved strong imports fail inspection. A pointer
value alone cannot distinguish a state object at offset zero from a null import.
The fixture exercises both an undefined weak import and an absolute-zero import
alongside the defined state object at offset zero. Weak declarations preserve
the compiler's nullable-address contract for these linker experiments.

The production executable's adjacent RX and RW segments share a Linux page.
Direct Linux ELF loading removes execute permission from the entry page. The
runtime-only copy therefore combines process segments into one RWX mapping;
the driver verifies that all admitted executable and initialized bytes remain
unchanged. ELF and a.out admission checks use the original executable. The
separate library mapping changes from RW during loading to RX before calls.
This envelope establishes no MPU or application text-protection claim.

Linker-symbol spans use `uintptr_t` subtraction under the ARM flat-address ABI;
subtraction between separate C array declarations has no portable C guarantee.
Clang's analyzer checks both C fixtures. Its fixed-address-dereference warning
has one local suppression at the write into the checked `mmap2` result because
the analyzer does not model the assembly mapping call.

## Calibration and replay

Five successful layouts cover zero application data/BSS, initialized pointers,
31-byte BSS, 4,097-byte BSS, and overlapping multicall applet BSS extents of
33 and 257 bytes. Each original ELF reserves state in writable `PT_LOAD.p_memsz`,
ends all application/overlay storage before it, and converts to an a.out whose
`a_bss` reaches the same final `_end`.

Eleven controls have named rejection paths:

| Control | Required observation |
| --- | --- |
| Incorrect imported `errno` address | QEMU exit 13, object identity |
| Incorrect `r9` GOT origin | QEMU exit 13, object identity |
| Missing initialized stream-pointer relocation | QEMU exit 15, initialization |
| State outside writable segment extent | Layout rejection names `PT_LOAD` containment |
| `_end` before reserved state | Layout rejection names `_end` |
| One non-PIC library function beside valid PIC code | Relocation inspection rejects an absolute state reference |
| Application definition of imported `errno` | Import inspection rejects interposition |
| Relocation of fixed GOT entries | QEMU exit 15, null-import initialization |
| Missing GOT relocation owner | Inspection rejects an unclassified cell |
| Conflicting GOT relocation owners | Inspection rejects conflicting state/fixed ownership |
| Undefined strong GOT import | Inspection rejects the unsupported unresolved import |

The host needs Python with `pyelftools`, ARM GNU GCC/binutils, QEMU user mode,
POSIX shell and built `tools/bin/rp2040/elf2aout`. The observed toolchain is
GCC 16.2.0, QEMU 11.1.3, pyelftools 0.33 and Clang 23.1.4 for static analysis;
cross-version compatibility remains a separate CI result. Run from the root:

```sh
export PYTHON=/usr/bin/python
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-libc-xip-object-abi
```

The root gate belongs to Linux `check-qemu` in `firmware.yml`. macOS supplies no
QEMU user-mode execution for this gate. Direct invocation accepts `--cc`,
`--qemu`, and an optional unique `--label`. Labels allow only ASCII letters,
digits, underscores and hyphens, with an alphanumeric first character. Existing
evidence directories fail before execution. Default labels use UUIDs.

Each executing invocation retains command outputs, ELF/a.out artifacts, `layout.json` and
`receipt.json` under `.build-logs`. Receipts hash the direct production sources,
linker/overlay tools, converter binary and fixture sources. Those hashes do not
cover every transitive header dependency; reproduce against the named Git tree
and build before interpreting them. Execution failures retain failed receipts;
dependency and argument errors can precede receipt allocation.

## Remaining implementation boundaries

The next ABI experiment derives an explicit public API export closure and
rejects a library that omits a required API even when installed programs never
reference it. Production work also requires stable function slots, complete
mutable-object alignment and identity, native import veneers, syscall/ROM-float
rewriting, current/minimum break ownership, and versioned kernel admission for
raw and packed images. Signal, setjmp/longjmp, fork, swap restoration, complete
stack/image fit, matched flash geometry and board performance remain open.

The engineering contribution is the verified composition of executable-tail
object imports with DiscoBSD BSS overlays and a.out conversion. PIC, GOTs and
link-time symbol imports have established prior art; this experiment establishes
no new algorithm or full POSIX/UNIX conformance claim.

## Shared syscall error state

`SYS_ERROR` in `lib/libc/arm/sys/SYS.h` stores the error value from `r0` and
returns `-1`. Explicit `DISCOBSD_SHARED_LIBC=1` assembly obtains the `errno`
address from the process-local GOT through `r9`. The path uses caller-saved
`r1` and `r2`, leaves `r9`, the stack and the return address intact, and places
its GOT-offset literal after the return. Static assembly keeps the absolute
literal. The specialized `pipe`, `_brk`, `ptrace` and `sigaction` paths share
this mechanism; `ptrace` also uses the same address mechanism when clearing
`errno` before its supervisor call, with argument registers saved.

The gate checks static `R_ARM_ABS32` and shared `R_ARM_GOT_BREL` errno
relocations in the generated and specialized wrappers. Its runtime fixture
executes the production error-return macro with error value 9 and checks
`-1`, the public `errno` value and object identity across five application
layouts. An absolute-address error-return fixture fails the code-state
relocation verifier before runtime. The supervisor-call transition, kernel
carry flag delivery and shared signal return remain separate obligations.
The shared selector is experimental and has no production build-mode wiring.

[The ROM dispatch register experiment](rom-dispatch-registers.md) establishes a
hot-call candidate with preserved double-precision operands and identifies the
paired cold-descriptor and resolver changes that remain before integration.

The production ROM macros and resolver provide the explicit shared selector.
[The ROM gate contract](../../../../sys/arch/rp2040/doc/TESTING.md) owns its
descriptor layout, bounds, modeled execution and remaining qualification.
`check-libc-xip-rom` exercises the cold and hot paths; the earlier register
experiment remains the retained isolated-sequence measurement.
