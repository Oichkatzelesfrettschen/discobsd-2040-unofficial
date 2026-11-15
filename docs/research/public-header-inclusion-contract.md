# Public-header inclusion and curses boolean ABI

At main `0c021420`, the tracked root-header scan accepts 54 headers
individually. Repeated inclusion rejects `curses.h`, `mtab.h`, `regexp.h`,
`ttyent.h`, `tzfile.h`, and `vmf.h`. Curses tests an include guard without
defining it; the other five headers lack guards around structure definitions.
The calibrated `public_header_inclusion.py` compiler gate reproduces the
curses structure redefinition before production edits.

Curses defines `bool` as an integer macro. Including `stdbool.h` in either
order produces a macro conflict or changes the types of public curses objects.
The existing ARM `WINDOW` layout has 48 bytes, alignment 4, and integer fields
at offsets 16, 20, and 24. Public capability and mode globals also have integer
storage. The `sflags` pointer table in `lib/libcurses/cr_tty.c` writes those
objects through integer pointers. Converting that storage to `_Bool` changes
the ABI and is outside this repair.

The repair gives each affected header an effective guard and preserves
inherited provenance verbatim. Curses offers its historical integer boolean
name as a typedef when a boolean macro is absent. Public storage declarations,
definitions, and pointer consumers name `int` explicitly. Including
`stdbool.h` therefore controls the caller's C17 boolean spelling independently
of curses storage. Legacy callers that include only curses keep integer
boolean behavior.

The deciding gate covers every tracked root header alone and twice, all roots
in forward and reverse repeated orders, four namespace modes, and both ARM
GCC and Clang. Separate curses witnesses require integer object address types,
public field offsets and structure size, legacy integer boolean behavior,
and C17 conversion to zero or one in both include orders. Guard-removal and
field-type mutations must fail. Smaller C input, provider object identity,
matched image resources, and runtime qualification remain required separate
leaves. Nested headers are outside the 54-root denominator.

Prediction: effective guards eliminate the six repeated-inclusion failures.
The independent boolean spelling repair eliminates the two include-order
conflicts while preserving the literal ARM layout. A surviving guard mutant,
changed public layout, or unexpected provider code change refutes the repair.

## Compiler observations and remaining native boundary

The guard-only candidate clears repeated root-header inclusion and reaches
an independent macro-redefinition failure in the combined sequence. The
boolean spelling repair passes 926 GCC/Clang compiler cases: 906 valid
compilations and 20 intentional rejections, including six guard mutations,
two public-storage mutations, and the old macro conflict in each profile.

Twelve Smaller C legacy inputs compile and assemble. Three stdbool inputs
(standalone and both curses orders) reject the `_Bool` token in the unchanged
fallback typedef. This independent standalone failure establishes an existing
compiler/header mismatch. The gate records these inputs as unsupported;
they supply no native boolean acceptance or execution evidence. The next
native experiment adds a complete boolean type contract to the compiler:
storage, integer conversion to zero or one, casts, assignments, parameters,
return values, and object ABI. Merely renaming the fallback type does not
establish C17 semantics.

DragonFlyBSD `94448f622a06cde90ba42d688e9cab2448ad9ab3`,
`lib/libncurses/include/curses.head`, explicitly separates the curses boolean
representation from compiler boolean spelling. Its SHA-256 is
`56a504742bdeb2628816c8b9000cfb6fa32531d4a0b4510a90fda1099063998f`.
FreeBSD `88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da`,
`contrib/ncurses/include/curses.h.in`, carries the same ncurses lineage;
its SHA-256 is `8734f435b9ee3e3883d55d3189b11e1d2c1692040ecfcccba4fa7a516f6478d3`.
Their shared lineage counts once. Their configured byte-sized representation
does not replace DiscoBSD's integer ABI. The local change reuses the separation
principle with a guarded typedef and explicit existing storage; it introduces
no new algorithm and imports no donor source text.


The maintained `logged-build` completes in 76.6 seconds on the header candidate.
The registered header gate repeats the 926 compiler verdicts, 12 accepted
native inputs, and three unsupported native boolean inputs. C17 denominator
calibration rejects all 21 controls; accounting reports 153 accepted,
543 excluded, 62 migration-open, and 2,261 unreviewed source paths. These
checks precede current-main integration and resource qualification.

A controlled ARM GNU17 comparison compiles `curses.c` and `cr_tty.c` from
main `0c021420`, the candidate, and the candidate with a forced stdbool include.
All three views produce identical object bytes for each provider. The profile
uses `-Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-common`,
`-fshort-enums`, Cortex-M0+, Thumb, soft float, and the tree's headers without
host includes. Source arrives through standard input, so debug paths and
source filenames do not confound the comparison. The `curses.c` object has
SHA-256 `4982ae8a8471f4070197a8fb9d5a0a0cc1b56798ab0970f4716421df58850cf3`;
`cr_tty.c` has SHA-256
`e56aa89aac295359d333153caa555b79cca793e4f4f890def632331470daaf94`.
This comparison establishes those two provider objects under that profile;
matched distributions still decide total image and installed-header costs.


## Matched main qualification

Candidate `576eb09901af689872109bd54c58443125bfd18e` includes main
`abe6db173ecc72251a76cf277c5bceec5161e5cb`. Its logged build and registered
header gate pass. The matrix retains 926 compiler cases, 12 accepted native
inputs, and three unsupported native boolean inputs. Source accounting reports
153 accepted, 543 excluded, 62 migration-open, and 2,273 unreviewed paths;
its calibration rejects all 21 controls.

The matched clean distributions produce zero deltas across all 162 measured
resource surfaces and unchanged archive membership. The resource comparator
exits zero. Root free space remains 233 blocks. These receipts retain the
baseline, candidate, and comparison:

- `public-header-resources-abe6db17.json`
- `public-header-resources-576eb099.json`
- `public-header-resources-abe6db17-576eb099.json`

The measurement requires no positive-growth exception. Runtime stack, heap,
and storage traffic remain unmeasured. Emulator qualification, hosted CI, and
reviewed integration remain open; the compiler matrix supplies no board claim.


## Native driver baseline and emulator qualification

Candidate `7f4883ec88c990c034109c0c28fb4ab2204f1da9` includes main
`df48d76ddca80ea40278f2b4ff40854b84b3f532`. The registered header matrix,
source accounting, and cc-driver contract pass after a logged build. The
matrix retains 926 compiler cases, 12 accepted Smaller C inputs, and three
unsupported native boolean inputs. The cc-driver fixture passes 18 cases
and rejects its basename-and-sed mutant.

The matched clean resource comparator exits zero with all 162 deltas zero
and unchanged archive membership. Receipts are
`public-header-resources-df48d76d.json`,
`public-header-resources-7f4883ec.json`, and
`public-header-resources-df48d76d-7f4883ec.json`.
Hosted run [43657326122](https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/actions/runs/43657326122)
passes the maintained build, flash, lint and Renode UART0 tier at this head.
Other optional hosted jobs report skipped, including ILP32 execution.

The local logged flash build and Renode gate also exit zero. Renode
`1.17.0+20260907gitf1dd1b4af` uses model revision
`205a5e4b25440582008a4292074bb07f80a72328` with the retained patches;
the model assembly SHA-256 is
`789d85b68c88374e0c9b71df757d045985a75d59179e3b0c2b3358f34dae2032`.
The gate exercises boot, filesystem and process checks, memory-device refusal,
fstat, capacity and image identity, modeled MPU faults and exec residue,
signal handling, native compilation with 2046 dates, and core-1 ping/stall
reporting. The warning classifier accepts 192523 observations across
30 permitted classes and reports zero errors. These are emulator
observations, separate from physical-board behavior and runtime peak bounds.

Retained copies in `.build-logs/header-image-7f4883ec/` match these hashes:

| Artifact | SHA-256 |
| --- | --- |
| `distrib/rp2040/flash.bin` | `47b9ef3bc80854affbc9a1c60252bc1314500cf015427efacf2f32536f3f9abc` |
| `distrib/rp2040/flash.uf2` | `11ce9cebf9150d8796bac40158a3ca109fc5793448c9eef968d3ec5a80ae62af` |
| `sys/arch/rp2040/compile/PICO_UART/unix.bin` | `e469dd56881d2279725a58d30e59fc98fe80bc21969c9241ec24fb7da4e84d6b` |
| `tools/renode/vendor/results/boot/renode.log` | `a30ad545b90954fc5ba8edb8460ae03d6ea254b096aa256a6b5f1b5ff945e4d8` |

The [native boolean type mechanism](native-boolean-type.md) decomposes the
remaining Smaller C parser, normalization, storage and ABI work. Its calibrated
starting witnesses establish an unsupported type, not a completed compiler feature.


## Signal-enabled main integration

Candidate `fecd15b2` includes main `050eac89c76f291c44633bf6af6cce505d435c65`.
The combined Makefile retains both header and signal-set cross gates.
Matched clean distribution receipts are `public-header-resources-050eac89.json`,
`public-header-resources-fecd15b2.json`, and
`public-header-resources-050eac89-fecd15b2.json`. The comparator exits zero;
all 162 numeric deltas are zero and archive membership stays unchanged.
The root retains 231 free blocks, including main's approved signal costs.
The earlier emulator records retain their source identities. Combined focused
qualification and exact-head hosted CI govern integration of this merge.


At `bf1723ff`, the combined logged build and focused gates exit zero:
`check-public-header-inclusion`, `check-signal-sets`, `check-signal-sets-cross`,
`check-signal-sets-qemu`, `check-board-libc-output`,
`check-c17-source-denominator`, and `check-cc-driver`. Source accounting reports
153 accepted, 543 excluded, 62 migration-open, and 2,277 unreviewed paths.
The README correction changes documentation only and preserves the historical
compiler footprint while identifying the measured current image and missing
native type mechanisms.
