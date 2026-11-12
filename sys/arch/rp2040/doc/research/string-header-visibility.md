# String header visibility and compiler contracts

`include/string.h` selects declarations through the installed private header
`include/_featuretest.h`. The policy keeps existing BSD source compatibility
while allowing callers to select a smaller namespace explicitly. The policy
adds declarations only for implemented interfaces. `strcoll`, `strxfrm` and
other missing functions retain their separate implementation obligations.

Feature selectors precede every public header. `_POSIX_C_SOURCE` supplies a
POSIX level; `_XOPEN_SOURCE` supplies an XSI level and its associated POSIX
level. The effective POSIX level is the larger of the two requests. The header
leaves caller macros unchanged. XSI values 500, 600, 700 and 800 imply POSIX
199506, 200112, 200809 and 202405 respectively. Older POSIX selectors expose
only declarations that meet the implemented version tests.

Modern selectors take precedence over `_ANSI_SOURCE` and `_POSIX_SOURCE`.
When modern selectors are absent, `_ANSI_SOURCE` selects the ISO subset and
outranks legacy `_POSIX_SOURCE`. `_BSD_SOURCE` and `_DEFAULT_SOURCE` add BSD
visibility explicitly, including alongside a standards selector. Presence of
either extension macro selects the extension set. With selectors absent,
GNU17 and strict C17 both preserve default BSD visibility; `__STRICT_ANSI__`
alone does not select a namespace. Internal `_DISCOBSD_*` macros are private.

| Existing declarations | ISO subset | POSIX 200809 | POSIX 202405 | XOPEN 700 | XOPEN 800 | BSD |
| --- | --- | --- | --- | --- | --- | --- |
| ISO memory/string declarations, size_t, NULL | yes | yes | yes | yes | yes | yes |
| strdup, strtok_r | hidden | yes | yes | yes | yes | yes |
| memccpy | hidden | hidden | hidden | yes | yes | yes |
| memmem, strlcpy, strlcat | hidden | hidden | yes | hidden | yes | yes |
| strsep, explicit_bzero, timingsafe_bcmp, syserrlst | hidden | hidden | hidden | hidden | hidden | yes |

`strtok_r` also appears at POSIX 199506 and later. `strdup` and `memccpy`
also appear at XSI 500 and later. The existing `size_t` remains unsigned int;
`NULL`, signatures and public layouts remain unchanged. The header defines
no runtime storage. The whole-file guard prevents repeated declaration parsing.

The edition boundaries derive from the Open Group
[Issue 7 string header](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/string.h.html)
and [Issue 8 string header](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/string.h.html).
Inspection of their option markers identifies `memccpy` as XSI and the Issue 8
addition of `memmem`, `strlcpy` and `strlcat` as Base/CX. The retained Issue 7
HTML observation has SHA-256
`25b3f870cc6e35d262a1f6a0a6f4fe40c9b24daa1f7b70e6b33e08899b3134cb`.
The archive frontier identifies the retained Issue 8 source. These requirements
define visibility; compiler acceptance supplies separate implementation evidence.

## Executable evidence

With caller-selected `PYTHON`, run:

```
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-string-header-contracts
```

The root cross tier and firmware CI own the gate. The receipt at
`.build-logs/string-header-contracts.json` records source/tool hashes, compiler
arguments, generated inputs, diagnostic outcomes and explicit unrun surfaces.
The gate exercises 18 selector combinations, each as standalone, repeated,
peers-before and peers-after inclusion. Peers comprise stddef, stdlib, stdio
and strings. Every existing signature receives an exact `_Generic` check.
Forbidden standalone names receive incompatible object declarations. Peer
forms check inclusion compatibility because unconverted headers still expose
their own extension declarations.

Five mandatory profiles cover host C17, Clang C17, Clang ARM C17 and production
ARM GNU17/C17. Optional host and Clang ILP32 profiles report availability
separately. The header namespace probes add `-fno-builtin` so compiler builtins
cannot masquerade as declarations or reject a forbidden-name object fixture.
The unmodified production build separately validates actual consumer flags.
Seven changed-header controls test BSD leakage, hidden ISO declarations,
Base-visible XSI declarations, Issue 8 leakage into Issue 7, changed defaults,
wrong signatures and a disabled include guard. Repeated inclusion poisons
`strlen` between includes because duplicate compatible prototypes alone cannot
detect a missing guard. Availability controls distinguish unsupported optional
profiles from source failures.

Twenty-four hosted cases resolve the real host sys/cdefs before or after the
target header. Dependency output proves that resolution. The before form
clears host-selected public feature macros before applying each target test's
selectors; host feature defaults therefore cannot supply an accidental oracle.
Applications select their feature macros before public headers.

The gate builds Smaller C's Thumb backend on the host, preprocesses 18 inputs
with explicit Smaller C identities, compiles their typed declaration consumers
and assembles their output for Cortex-M0+. A malformed input calibrates parser
rejection. Expanded-header acceptance does not establish on-device preprocessing,
execution, or full C17 language support.

## Remaining closure

The mechanism converts `string.h`. Other public headers retain their existing
visibility policies and require individual conversions. Existing string
algorithms retain independent runtime obligations. Consumer rebuilds, matched
resource receipts, aggregate checks, and reviewed integration decide release
acceptance; host compilation establishes neither board behavior nor performance.

<!--
Copyright (c) 2026 DiscoBSD

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
-->
