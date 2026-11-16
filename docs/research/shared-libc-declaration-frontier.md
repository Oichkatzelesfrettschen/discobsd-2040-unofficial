# Shared-libc declaration and provider frontier

A bounded declaration scan at `6c08eaab4f14eb02ccdddc2cdc4c13ad50aec2bf`
finds 325 distinct external names in eight included public headers and their
dependencies. The [receipt](shared-libc-declaration-frontier.json) records every
name, declaration type, provider class, direct-header hash, compiler command,
and compiled archive identity. This discovery set supplies work for API
selection; it is not the selected shared-library export contract.

## Scope and ownership

The roots are `stdlib.h`, `string.h`, `stdio.h`, `unistd.h`, `time.h`,
`signal.h`, `setjmp.h`, and `math.h`. Clang parses them for ARM Cortex-M0+ under
C17 with `-Wall -Wextra -Werror -fno-builtin` and the repository's include tree.
No feature selector is defined. `_featuretest.h` therefore retains the default
BSD compatibility namespace independently of the C17 language selection.

The scan takes nonimplicit, nonstatic top-level `FunctionDecl` and `VarDecl`
nodes, deduplicates names, and rejects conflicting declaration type strings.
It discovers 316 functions and nine objects. The compiled provider inspector
in `tools/libc_shared_closure.py` supplies the archive definitions; all matched
libc definitions agree with the declaration's function/object kind.

| Provider class | Names | Disposition |
| --- | ---: | --- |
| `lib/libc.a` | 293 | Provider exists; API selection and semantics remain separate |
| `lib/libm.a` | 25 | Separate math archive owns the provider |
| Startup | 2 | `environ` and `__progname` require shared-state ownership |
| Compiler expansion | 1 | `alloca` requires a compiler contract rather than a normal exported function |
| Unprovided declaration | 4 | `syserrlst`, `fabs`, `frexpf`, and `ldexpf` require disposition |

The nine declared objects are `suboptarg`, `_iob`, `sys_siglist`, `environ`,
`__progname`, `optarg`, `opterr`, `optind`, and `optopt`. This header set omits
`errno.h`; its object list is therefore incomplete for the shared state ABI.
Declaration type strings also omit relevant attributes and complete layout
identity. An ABI generator must expand the header denominator and preserve
those additional obligations.

`include/alloca.h` maps GCC-family calls to `__builtin_alloca`. A declaration
alone does not establish callable address-taking or native Smaller C support.
`syserrlst` appears in the BSD-visible part of `include/string.h`, while neither
inspected archive defines it. A reviewed historical semantic contract must
precede either a provider or a declaration-policy change.

## Address-taking and signed zero

Three target C17 probes initialize volatile function pointers from `fabs`,
`frexpf`, and `ldexpf`. Compilation succeeds; linking with the production
linker script and `-lm -lc -lgcc` reports the corresponding undefined symbol.
The same command shape links the `strlen` control successfully. Function
address-taking and `-fno-builtin` keep compiler substitution from hiding the
provider gap. The receipt records those four link outcomes.

Both `lib/libm/fabs.c` and `lib/libc/gen/fabs.c` contain legacy implementations
that return `arg < 0 ? -arg : arg`. Their presence supplies no linked provider:
the maintained archive selections omit them. The comparison also preserves
negative zero because `-0.0 < 0` is false. A GNU17 host compilation of the
libm source, renamed to `donor_fabs` with builtins disabled, and a strict-C17
harness reproduce result bits `0x8000000000000000` for `fabs(-0.0)`.
The source hash in the receipt identifies that implementation.

The [Issue 8 `fabs` return-value clause](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fabs.html#tag_17_134_04),
marked MX, specifies positive zero for either signed-zero input. Adding the
omitted source to an archive therefore fails to resolve the semantic gap.
The numeric work package must provide callable functions with target precision,
signed-zero, infinity, NaN, subnormal, and exponent-boundary witnesses before
they enter the selected export contract. The deciding tests for `frexpf` and
`ldexpf` additionally cover decomposition, reconstruction, underflow, overflow,
and the actual target floating-point representation.

## Replay and remaining scope

The receipt's compiler command consumes a translation unit containing the eight
includes in the order listed above. Clang's `-Xclang -ast-dump=json` output
supplies the top-level nodes. Run archive inspection only after the distribution
completes:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
"$PYTHON" tools/libc_shared_closure.py lib/libc.a --describe
"$PYTHON" tools/libc_shared_closure.py lib/libm.a --describe
```

The raw AST, header translation unit, link probes, diagnostics, and host
signed-zero harness remain in `.build-logs/shared-public-declarations`.
The [export-closure contract](xip-shared-libc/export-closure/README.md) owns
provider resolution and its calibrated checker. This scan leaves other
headers, alternate visibility modes, compiler-generated aliases, complete
function signatures, mutable layouts, and full standards disposition open.
