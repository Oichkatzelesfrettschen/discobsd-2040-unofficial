# Root-header declaration census for shared libc

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
