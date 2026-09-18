# Warning policy audit

## Baseline and scope

The measured baseline is `99c79a8472bb1296d8de518da55cbd22399ea247`,
with source tree `93649058db3eccfa64dc5ea106c031e845217ad4`. The source
snapshot was checked against that tree before any warning edits. Later
unrelated main-branch changes are not part of these baseline measurements.

The RP2040 kernel had `-Wall -Werror`, but not `-Wextra`. The root
Makefile orchestrates builds rather than compiling C itself; adding a
flag there without following sub-makes would not establish enforcement.
`share/mk/sys.mk` and `tools/Makefile.inc` supplied compiler commands
without fatal-warning defaults. Several leaves replace `CFLAGS`,
including the native a.out libc and Smaller C compiler. That replacement
is intentional and must not erase the warning policy or the native
assembler route.

## Measured diagnostic inventory

Building both PICO configurations with `CWARNFLAGS='-Wall -Wextra'`
recorded 231 warnings, or 122 distinct diagnostic strings. These are
not 231 independent defects: the two kernels share many sources, and
macro-expansion diagnostics can name the same source line repeatedly.

| GCC diagnostic | Occurrences across both configurations |
| --- | ---: |
| unused-parameter | 108 |
| sign-compare | 100 |
| implicit-fallthrough | 11 |
| clobbered | 8 |
| empty-body | 2 |
| type-limits | 2 |
| Total | 231 |

The inventory invocation intentionally made warnings nonfatal so the
whole set could be inspected. Qualification uses the default policy,
not that invocation.

## Repairs

The RP2040 template and both regenerated makefiles use
`-Wall -Wextra -Werror`. GNU assembler invocations additionally use
`-Wa,--fatal-warnings`; a real `.warning` fixture demonstrates why the
compiler flag alone is insufficient. Linker warning policy is unchanged.

Target severity lives alongside `CC` in `share/mk/sys.mk`, and host-tool
severity alongside `CC` in `tools/Makefile.inc`. The shared default
`HOST_CC` also covers userland's host-side code generators. Replacing a
leaf's `CFLAGS` therefore retains severity. Existing warning groups and
specific compatibility exceptions remain unchanged. The native a.out
`-B`/`-Wa,-x` route, target ABI flags, and C dialect selection remain
intact. This is not a claim that every historic userland source has been
converted to `-Wextra`.

The source changes explicitly consume unused ABI parameters, use count
and length types appropriate to bounded loops, and mark real switch
fallthrough. Dhara's descending `level` remains signed because `-1` is
meaningful. Its error lookup uses an unsigned range test that also
rejects invalid negative enum values. The resource-map overflow test
uses unsigned addition and wrap detection, avoiding integer-promotion
ambiguity with the compact 16-bit descriptors.

The syscall-table selection and ioctl device identifier are initialized
once. Two values live at kernel `setjmp` sites use narrowly applied
volatile storage rather than disabling `-Wclobbered`. Stored kernel
structure layouts and public function interfaces are not widened. The
small exec-header comparisons now also reject negative header lengths.

Warnings must reach the build result. The native a.out library loop and
its enclosing library-install loop no longer ignore sub-make failures.
The generated-version recipe uses `&&` so successful cleanup cannot
mask failed generation or compilation of `vers.c`.

The existing kernel `.params` prerequisite now records the compiler
commands, effective C/assembly flags, include flags and configuration.
Changing those inputs rebuilds objects; repeating an unchanged invocation
does not. Shell quoting is tested with metacharacters. Replacing a compiler
binary in place still requires a clean build: the signature records the
command, not a digest of the executable. Host tools and userland likewise
require a clean build when qualifying a new policy/toolchain.

## Calibration and local results

`tests/warning_policy/check.py` includes or copies the production
makefiles into temporary fixtures. It accepts known-good sources and
rejects injected C/assembler warnings. It checks CFLAGS replacement,
actual native-assembler flags, root-to-tool error propagation, both
library recursion layers in serial and jobs modes, failed `vers.c`
compilation, flag-change invalidation, and shell-safe flag recording.
`check-warning-policy` belongs to the root `check-cross` tier.

The original tree was also supplied to the gate with `--root` as a
negative control. It failed the enforcement checks rather than merely
failing to find the compiler. Calibration logs accompany the handback.

Local toolchain: ARM GCC 13.2.1 (20231009), host GCC 14.2.0, bmake
20200710. Both kernels compile and link with their strict defaults and
zero diagnostics. The kernel host gate passes 977 assertions; the a.out
header and config-generated Makefile checks also pass.

| Kernel | Baseline text bytes | Patched text bytes | Data bytes, unchanged | BSS bytes, unchanged |
| --- | ---: | ---: | ---: | ---: |
| PICO | 103058 | 103042 | 248 | 39544 |
| PICO_UART | 90928 | 90904 | 192 | 14528 |

These are measurements on the pinned baseline with the same compiler,
not a promise about later commits or other compilers. They are not a
byte-identity claim: 14 non-version objects per configuration have
changed allocated-section contents. The handback retains section hashes.
The local whole-world build rebuilt the host tools and reached userland,
then stopped because this environment lacks bison. No parser substitution
was made. Local picotool, QEMU, Renode and board results are not claimed.
The PR's CI results establish any additional runner qualification.

## Retained debt and boundaries

`struct proc.p_uid` remains a signed short while `uid_t` is unsigned.
Explicit casts preserve the existing comparison semantics; they do not
repair the high-UID representation problem. Changing that representation
requires a separate credential/ABI audit. Similarly, making the existing
rwuio comparison explicit does not establish overflow-safe accumulation
of arbitrary iovec lengths. Warning-free compilation is not a proof of
those algorithms.

No broad `-Wno-extra`, `-Wno-sign-compare`, `-Wno-unused-parameter`, or
`-Wno-clobbered` was added. Existing per-component exceptions are not
removed without their own qualification. The independent STM32 and PIC32
kernel templates are not certified by an RP2040 build. Explicit command
line compiler/warning overrides remain caller-controlled and are outside
the default-policy claim. Physical boot, USB behavior, interrupt timing,
and other compiler versions require their own results.

No build dependency on `discobsd-2040-notes` was introduced.
