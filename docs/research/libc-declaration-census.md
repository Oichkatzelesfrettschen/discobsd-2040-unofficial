# Declaration census for shared libc

The retained census describes its recorded revision. The current installed
terminal interface uses the 19-control-character layout in `include/termios.h`.
The earlier, uninstalled 32-character layout remains in
`legacy/termios/termios-todo.h`. Keeping the draft outside the public header
directory lets the live joint-header compile check the shipped interface.


The shared-libc declaration frontier covers eight root headers in one default
namespace. The maintained root-header denominator contains 54 tracked headers.
The installed root-header set contains 53 names: `include/Makefile` leaves
`termios-todo.h` outside installation. Source presence and installed interfaces
therefore retain separate sets.
The census expands declaration discovery across that denominator, four feature
modes and both 32-bit and 64-bit time views. The existing migration plan owns
the implementation sequence and final ABI requirements.

Clang parses each header separately, the complete ordered header set and the
installed header set for
ARM Cortex-M0+ under C17. A declaration records its public name, compiler
linkage name, observed type spellings, parameter types, attribute kinds and
the root headers that expose it. Exposure includes transitive dependencies;
it does not identify the header that directly owns a declaration. Static and
implicit declarations stay outside the external-name set.
Installed exposure has independent witnesses and flags. A source-only header
cannot silently become part of the selected public interface.

Linkage names matter independently of C names. The current wide-time `time`
declaration binds to `__time64`; `ctime` binds to `__ctime64`. Provider lookup
uses the compiler linkage name. A narrow `time` provider alone therefore
supplies no evidence for a wide-time call. The compiler AST and compiled
archive definitions provide separate observations.

The census records every declaration as unreviewed for shared ABI selection.
Provider observations distinguish a matching provider, multiple providers,
a kind mismatch and an unprovided linkage name. Each class retains a deciding
next action. Ten ARM ELF archives form the declared provider corpus; the
expanded `lib/Makefile` owner set calibrates its denominator. Startup and
native a.out libraries remain separate. Missing providers receive no automatic startup, compiler-owned,
absent-standard or unselected-option disposition. Macro interfaces, public
headers below the root, complete attribute semantics, object layouts, native
compiler input, API semantics and final export selection remain explicit work.

The command binds header content, transitive compiler dependencies, compiler
identity, profile flags, AST hashes and archive bytes. Header inputs and
archive bytes remain unchanged across the scan. Archive hashes establish
artifact identity; clean build receipts separately establish source binding.
Compiler errors, diagnostics, malformed AST output, escaped dependencies and
incomplete root exposure fail the scan. A failure supplies no absence claim.

The first complete scan rejects the assumption that the combined header set
contains every standalone name. `stdlib.h` alone declares `alloca`, while an
earlier `alloca.h` expands that name to `__builtin_alloca`. The census therefore
uses the union of standalone and combined observations. It records
`standalone-only`, `joint-only` and `both` exposure classes and keeps a deciding
action for each difference. The exact union is verified against retained
symbol witnesses; removing a required declaration still fails verification.

## Installed include-tree observation

The live gate reads `DESTDIR/usr/include` after the RP2040 build. The
expanded `include/Makefile` selections own root, network, architecture, and
`sys/` headers. The independent `lib/libreadline/Makefile` selection owns
`readline/`. The gate compares installed paths and bytes with those tracked
sources and checks the root aliases and the `machine/` alias for `rp2040/`.

Each profile records standalone compiler results for every installed spelling.
The combined installed-tree witness attempts the successfully parsed spellings
in root-first order. For either kind of witness, a Clang header diagnostic
records a rejection and supplies zero declarations. Every root header must
parse. A failed driver invocation, malformed AST, or unowned compiler input
fails the gate. Per-header declarations retain C and linker names, types,
attributes, and archive observations without merging observations from
different headers that use the same name.

The installed tree contains kernel headers. For example,
`sys/syscall_dispatch.h` declares `void close(void)` for the kernel table,
and `unistd.h` declares `int close(int)` for libc callers. A standalone parse
can succeed for both while the combined parse reports conflicting types.
Kernel and user declaration ownership, headers that require context, and a
combined user ABI view remain open. Archive matches supply provider evidence
for review. Selecting exports requires a separate ownership decision.

A clean build at base `6719f9d7a` supplies 171 installed header paths and
28 additional `machine/` spellings. Clang 23.1.4 parses 166 standalone
spellings and rejects 33 in each of the eight profiles. Each combined attempt
rejects the kernel dispatch declarations against user prototypes. For the
default 32-bit time view, both `close` declarations match the same archive
symbol by linkage name, and their distinct types identify the ownership
conflict. Both installed readline headers parse under all eight profiles.

The regression controls predict that a missing readline header, changed
installed bytes, or a substitute symlink fails staged ownership. A nested
header with a forced diagnostic records no declarations, even when Clang
emits a partial AST. A missing witness, a truncated combined input, and a
driver failure also fail calibration.

## Implementation and deciding checks

- [x] Add failing tests for AST declaration filtering, assembler aliases,
  provider kinds, missing providers and exact header exposure.
- [x] Implement the root-header and profile census with retained compiler
  witnesses and source/archive identity checks.
- [x] Calibrate a clean header, an invalid header and a declaration whose
  public C name has a different linker symbol through real Clang execution.
- [x] Run the complete current root-header matrix and inspect every provider
  gap before recording its next action.
- [x] Register the gate with the cross tier, testing documentation and CI
  owner; run focused lint, calibration and source-accounting gates.
- [ ] Publish the implementation and measured census through a separate PR.

The prediction is that name-based lookup misclassifies an alias whose C name
has a provider but whose linker name does not. The calibrated alias fixture
must preserve the linker name and report the missing provider. Removing a
required header exposure must fail reconciliation. Existing source controls
already compile the complete 54-header set in all eight profile/time pairs;
the census adds executable symbol and provenance accounting.

## Provider and interface dispositions

The initial ten-archive scan observes 577 distinct names in the default
64-bit-time profile. Of these, 559 have one matching provider, three have
multiple archive providers and 15 have no provider in that corpus.
The installed view contains 575 names and excludes the two termios declarations
from the source-only header. All selections remain unreviewed for the shared ABI.

The [complete receipt](libc-declaration-census.json) binds clean-start generator
source `aaefae19` to all 448 compiler cases, header/dependency hashes, observed
declarations, tool identities and archive bytes. The
[qualification record](libc-declaration-census-qualification.json) preserves
25 census calibration tests, 33 archive-closure tests, 47 inventory checks,
profile calibration and repository lint. The complete parallel cross tier
passes in 215.814 seconds with zero terminal queue loss. Its tracked source
stays frozen; the generated census receipt remains the sole untracked artifact
outside build inputs. That run preserves the existing CoreMark namespace
omission, which separate PR 560 repairs.

| Selector mode | Time view | Tracked-root names | Installed-view names |
| --- | ---: | ---: | ---: |
| Default BSD | 32 | 576 | 574 |
| Default BSD | 64 | 577 | 575 |
| ISO selector | 32 | 563 | 561 |
| ISO selector | 64 | 564 | 562 |
| Issue 7 selector | 32 | 569 | 567 |
| Issue 7 selector | 64 | 570 | 568 |
| Issue 8 selector | 32 | 573 | 571 |
| Issue 8 selector | 64 | 574 | 572 |

The labels name compiler selectors. Their counts establish observed exposure,
rather than a verdict on standard namespace visibility.

| Observed names | Source-backed interpretation | Deciding next action |
| --- | --- | --- |
| `environ`, `__progname` | Startup supplies these objects outside the examined archives | Bind executable-tail state to the generated object ABI and its lifecycle witnesses |
| `alloca`, `__builtin_alloca` | Standalone declaration and combined macro expansion have different names | Keep the compiler expansion contract separate from callable exports and Smaller C |
| `fabs`, `frexpf`, `ldexpf` | The callable math-provider gaps retain the earlier frontier's witnesses | Implement exceptional-value and precision contracts before selecting exports |
| `regcomp`, `regexec`, `regsub`, `regerror` | Installed `regexp.h` declares the legacy V8 interface | Resolve legacy names and callers before adding the incompatible POSIX BRE/ERE interface |
| `UPPERCASE` | `curses.h` declares the object; the examined archives supply no definition | Audit legacy callers and the intended curses state contract |
| `syserrlst` | The BSD-visible string declaration retains its provider gap | Derive and witness the historical error-string contract |
| `tcgetattr`, `tcsetattr` | Only source-only `termios-todo.h` exposes these declarations | Keep terminal implementation and installed-header admission in their own work package |
| `putenv`, `setenv`, `unsetenv` | `libc` and `libtcl` each define these names | Resolve environment ownership and Tcl trace behavior before shared imports select providers |

The environment overlaps come from `libc/setenv.o` and `libtcl/tclenv.o`.
`lib/libtcl/tclenv.c` propagates environment changes to interpreter variables;
its `EnvTraceProc` also calls its environment setters. Selecting the libc
provider merely by symbol name therefore leaves a behavioral obligation.
The existing program-object namespace scan and this archive-provider scan
cover different collision surfaces.

`include/regexp.h` declares `regexp *regcomp()` and `void regerror()`.
Those declarations do not express POSIX `regcomp` and `regerror` signatures.
Source search finds Tcl consumers using their local `regexp.h`; the installed
legacy header and Tcl's actual providers retain separate ownership. The
regular-expression work package reconciles this interface history explicitly.

The source inspections use `rg` over `include/alloca.h`, `include/stdlib.h`,
`include/regexp.h`, `include/curses.h`, `include/termios-todo.h` and
`lib/libtcl/tclenv.c`. Compiler ASTs identify declarations and linker names;
`tools/libc_shared_closure.py` identifies compiled archive definitions. A
separately compiled alias fixture imports the symbol the AST names, as read by
GNU `nm`. These checks establish their inspected representations and leave
runtime semantics to their own witnesses.

The contribution is the source-bound composition of declaration exposure,
installation selection, linker identity and provider ownership under the
DiscoBSD ABI. Compiler aliases, macro expansion and finite-set reconciliation
have established prior art. This census supplies no new algorithm or completed
shared-libc implementation.
