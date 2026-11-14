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
