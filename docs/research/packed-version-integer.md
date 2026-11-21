# The packed version integer (`kern.osrev`): requirements, candidate encodings and how other systems encode a release

Source reading at origin/main 1b9a8b21 (the tree). Primary sources were retrieved on 2026-11-21 (UTC) with curl from immutable raw URLs at the upstream commit recorded for each file in `packed-version-integer/survey-sources.tsv`, with the SHA-256 of each copy. Compiler for the decoder sizes: arm-none-eabi-gcc 16.2.0 (local). The text is a research record: it answers decision D156 ("the packed-integer field widths are not decided") with a study, a survey and a recommendation, and changes no tree file. `kern.osrev` keeps its present value until the owner decides.

## Findings

1. The integer has no reader in the tree. `git grep -n -i -E 'osrev|KERN_OSREV' -- sbin usr.sbin usr.bin sys lib include share` finds the sysctl table entry (`sys/sys/sysctl.h:129` and `:164`, `{ "osrevision", CTLTYPE_LONG }`), the one producer (`sys/kern/kern_sysctl.c:231`, which returns `(long)DiscoBSD`, the year-and-month value 202608 of `sys/sys/param.h:8`) and manual pages. Every encoding below therefore costs nothing at the rename, and the choice can wait for the first consumer.
2. Every positional packing, decimal or binary, is a mixed-radix numeral, so its order equals the lexicographic order of the fields whenever each field stays inside its limit. The choice between candidates is a choice of radix, of field widths and of the source of the stage rank; order is a property of the layout, not of the choice. The harness confirms this on a boundary grid for all five reference layouts and rejects five deliberately wrong layouts, each for its own reason. Reserved zero bits between fields (the shape of OpenSSL's `0xMNN00PP0L`) keep the order and only make the layout non-compact.
3. A signed 32-bit `long` leaves 31 value bits. A layout whose major field reaches bit 31 (four bits of major at bit 28, as the OpenSSL layout `0xMNN00PP0L` allows) sets the sign bit at major 8. The harness mutant `mutant-signed` shows it.
4. Binary decoders are 22 bytes of Thumb code with no division; the decimal decoders of the candidate in `version-scheme.md` are 76 bytes and six calls to `__aeabi_uidiv` and `__aeabi_uidivmod` (Cortex-M0+ has no hardware divide; this port's helper in `lib/libc/arm/gen/aeabi_div.S` reaches the RP2040 SIO divider). The kernel itself decodes nothing: the integer is a build-time constant. The cost falls on a reader program, and no reader exists (finding 1).
5. SemVer 2.0.0 orders pre-release names by ASCII (item 11). The owner's stated stage sequences ("alpha, beta, delta", "alpha, beta, rc", "alpha, rc") are all in ASCII order, so a packed stage rank, a version string and a package tool agree on them. The sequence alpha, beta, gamma, delta, which follows the Greek alphabet, is not: SemVer places `delta` below `gamma`. A rank that follows the owner's chronology would then disagree with every tool that compares `osrelease` strings.
6. Among 17 projects whose headers were read, two put the pre-release stage inside the integer (Python, `0xMMmmppLs`; OpenSSL 1.1.1, `MNNFFPPS` with a status nibble "0 for development, 1 to e for betas ... and f for release"), and OpenSSL master stopped: its header synthesizes the integer with a zero status nibble and carries the pre-release in a string. Kernels and RTOSes pack three or four numeric fields and leave the stage to a string (Zephyr's EXTRAVERSION, FreeRTOS's trailing `+`, seL4's `17.0.0-dev`).

## Constraints

| Source | Constraint |
| --- | --- |
| D112 | A packed integer of the version fields, "always automatically updated when the version updates unless it breaks, then it stays as it was", and a hard error at compile time, not a warning. Generation from the one version source; never hand-edited. |
| D155 | No compile-time version macro for user programs; `kern.osrev` and `uname -r` at run time. The integer is a run-time interface. |
| D156 | The field widths are open; this study is owed. |
| D158 | No fixed pre-release vocabulary: the owner names each stage when it comes, and tools must not hard-code a stage list. |
| D159 | The rename is a rename; version machinery beyond what it needs waits. |
| `sys/sys/sysctl.h:164` | `KERN_OSREV` is `CTLTYPE_LONG`; `long` is 32 bits on ARMv6-M, so the value range is 0 to 2147483647 for a positive number. |
| SemVer 2.0.0 items 10 and 11 | Build metadata (`+YYYYMM`, D114) does not figure into precedence; a pre-release has lower precedence than its release; pre-release identifiers compare numerically if all digits, else lexically in ASCII order. |

## First principles

### What the integer carries

A consumer asks one question of it: is this kernel at least release X. An integer answers with a total order on releases and nothing else, so the information it must hold is the position of a release in SemVer precedence, restricted to what a rank can express:

- major, minor and patch, compared numerically in that order;
- one pre-release rank below the release (the release has the highest rank);
- no build metadata (item 10), and no pre-release identifier beyond its rank: `1.0.0-alpha.1` and `1.0.0-alpha.2` pack to one value unless a serial field exists. The integer is a coarsening of SemVer precedence, and an "at least" check inside one pre-release stage cannot separate its builds.

### Eliminated before measuring

- A release counter, bumped per release. It orders trivially and has no fields, but it cannot be derived from the version string, which D112 requires, and a table must map it back.
- A year-and-month value (`BSD 199506`, `OpenBSD 202610`, the current `DiscoBSD 202608`). It orders by build date: two releases in one month tie, and a maintenance release of an older line sorts above a newer line built earlier. `version-scheme.md` measured 10 of 13 pairs ordered.

### Mixed-radix layout

Write the fields from least to most significant as digits of a numeral whose digit *i* has radix r_i = limit_i + 1. The weight of a field is the product of the radices below it, and the value is the weighted sum. Two tuples compare as their numerals do, which is lexicographic order from the most significant field, as long as no field exceeds its limit. A field past its limit carries into its neighbor: with the weights of the layout in `version-scheme.md`, minor 100 equals major + 1. The compile-time check D112 asks for is therefore a range check of each field against its limit plus a check that no field reaches the weight above it. The harness's `guard` check performs both and the mutant `mutant-overlap` (minor limit 100 at the weight of major) fails it; reserved zero bits between fields leave a gap and pass. The separate `compact` column records whether the layout has no gap.

### Stage rank

A pre-release has lower precedence than its release (SemVer item 11), so the release takes the highest rank of the stage field and every pre-release stage a smaller one; rank 0 stays reserved for an unset or development build. The harness mutant `mutant-final-low` ranks the release below its pre-releases and fails the order check.

Where the rank comes from is the open question of D158, because a rank needs a name-to-number map and tools must not hold one. Two sources exist:

- S1, an explicit rank beside the stage name in the version source, set by the owner when a stage is named. It costs one edited line per stage and a 4-bit stage field (14 pre-release ranks).
- S2, the rank of the first letter of the name (a = 1 to z = 26), release = 31, a 5-bit field. It needs no list and no edit, and it makes the packed order follow the ASCII order that SemVer uses, so rank and string agree by construction for names with distinct initials. It narrows the grammar the version source may carry: a stage name that does not start with a lowercase ASCII letter has no rank and must stop the build, because uppercase letters, digits and hyphens sort below lowercase letters in ASCII (SemVer item 11) and a rank taken from them would disagree with the string order (`RC`, `1`, `-x` are rejected by `initial_rank`). Two names with one initial (`alpha` and `abc`) tie.

### Stage names against ASCII order

The harness orders each stage sequence with a SemVer item 11 implementation (calibrated on the specification's own chain, and on `1.0.0 < 1.0.0-alpha` as the known-bad control):

| Sequence | ASCII order equals intended order | Initial letters distinct | Initial ranks |
| --- | --- | --- | --- |
| alpha, beta, delta (D153) | yes | yes | 1, 2, 4 |
| alpha, beta, rc (D158) | yes | yes | 1, 2, 18 |
| alpha, rc (D158) | yes | yes | 1, 18 |
| alpha, beta, gamma, delta (control) | no | yes | 1, 2, 7, 4 |

A pre-release name list is thus a hidden constraint on any scheme: a stage named out of ASCII order makes a rank and a package tool disagree. Under S2 below the agreement holds by construction, because the rank is the first letter. Under S1 a build can check it only if the version source also names the previous stage: the new rank must exceed the previous rank exactly when the new name sorts above the previous name in ASCII order.

## Candidate encodings

All five layouts are checked on a grid of the boundary values of every field (0, 1, 2, limit - 1, limit; stage ranks 1, 2, release - 1, release; a serial of 0, 1, 2, limit - 1, limit on a pre-release and 0 on a release); the check sorts the tuples as SemVer does and requires strictly rising packed values. A layout without a serial field refuses a serial above 0. Source: `packed-version-integer/encodings.py`; output: `packed-version-integer/results.txt`.

| Layout | Fields (major.minor.patch.stage) | Largest value | Pre-release ranks | order | roundtrip | signed 32 | guard |
| --- | --- | --- | --- | --- | --- | --- | --- |
| dec-existing | 20.99.99999 and 1 digit | 2099999999 | 8 (1 to 8) | pass | pass | pass | pass |
| bin-7.8.12.4 | 127.255.4095 and 4 bits | 2147483647 | 14 | pass | pass | pass | pass |
| bin-7.8.11.5 | 127.255.2047 and 5 bits | 2147483647 | 30 | pass | pass | pass | pass |
| bin-python | 127.255.255, stage nibble, serial nibble | 2147483647 | 14 (serial 0 to 15, checked on its boundaries) | pass | pass | pass | pass |
| bin-gapped-0xMNN00PPS | 7.255.255 and 4 bits, bits 12 to 19 reserved: an adaptation of OpenSSL's `0xMNN00PP0L` whose low nibble carries the stage | 2146439167 | 14 | pass | pass | pass | pass (not compact) |

Values for the chain of `version-scheme.md` (decimal, with hexadecimal; the 7.8.11.5 column ranks alpha 1, beta 2 and the release 31):

| Release | dec-existing | bin-7.8.12.4 | bin-7.8.11.5 |
| --- | --- | --- | --- |
| 0.1.0-alpha | 1000001 (0x000f4241) | 65537 (0x00010001) | 65537 (0x00010001) |
| 0.1.0-beta | 1000002 (0x000f4242) | 65538 (0x00010002) | 65538 (0x00010002) |
| 0.1.0 | 1000009 (0x000f4249) | 65551 (0x0001000f) | 65567 (0x0001001f) |
| 0.1.1-alpha | 1000011 (0x000f424b) | 65553 (0x00010011) | 65569 (0x00010021) |
| 0.1.1 | 1000019 (0x000f4253) | 65567 (0x0001001f) | 65599 (0x0001003f) |
| 0.2.0-alpha | 2000001 (0x001e8481) | 131073 (0x00020001) | 131073 (0x00020001) |
| 0.2.0 | 2000009 (0x001e8489) | 131087 (0x0002000f) | 131103 (0x0002001f) |
| 1.0.0-alpha | 100000001 (0x05f5e101) | 16777217 (0x01000001) | 16777217 (0x01000001) |
| 1.0.0 | 100000009 (0x05f5e109) | 16777231 (0x0100000f) | 16777247 (0x0100001f) |

Decoders for major, minor, patch and stage, compiled with `arm-none-eabi-gcc -Os -mcpu=cortex-m0plus -mthumb -ffreestanding`:

| Layout | Bytes of .text | Calls to the division helpers |
| --- | --- | --- |
| dec-existing | 76 | 6 |
| bin-7.8.12.4 | 22 | 0 |

### Calibration

`--selftest` requires the reference layouts to pass and five mutants to fail the check each was built to fail, requires the oracle to order the SemVer specification's own chain (and to reject `1.0.0 < 1.0.0-alpha`), and requires `initial_rank` to refuse `RC`, `1`, `-x` and the empty name. The mutants: `mutant-serial-high` (serial above stage, so `alpha.15` sorts above `beta.0`) and `mutant-stage-high` (stage above major) fail order; `mutant-overlap` (minor limit 100 at the weight of major, so `0.100.0` packs equal to `1.0.0`) fails guard; `mutant-signed` (4 bits of major at bit 28) fails signed; `mutant-final-low` (the release below its pre-releases) fails order. A mutant that passed, or failed a different check than the one named, makes the selftest exit 1.

## Survey of how other systems encode a release

Each file was fetched at the upstream commit recorded in `survey-sources.tsv`, on the immutable raw URL that names that commit, and lines are those of that copy (the branch heads of 2026-11-21 returned the same bytes). "Stage in integer" is yes only where the pre-release state is a field of the integer.

| System | Kind | Integer | Layout | Stage in integer | Source (file, lines) |
| --- | --- | --- | --- | --- | --- |
| Linux | monolithic kernel | `LINUX_VERSION_CODE` | version * 65536 + patchlevel * 256 + sublevel; sublevel clamped to 255 (`KERNEL_VERSION(a,b,c)`: `((c) > 255 ? 255 : (c))`) | no | `Makefile` 1457-1468 |
| FreeBSD | monolithic kernel | `__FreeBSD_version` | `<major><two digit minor>Rxx`, "bumped every time there's a change in the base system that's noteworthy"; 1600031 at read; `BSD 199506` (year and month) remains | R digit marks branch state | `sys/sys/param.h` 43-77 |
| NetBSD | monolithic kernel | `__NetBSD_Version__` | `MMmmrrpp00` decimal, 1199001100 for 11.99.11; minor 99 marks current; `__NetBSD_Prereq__(M,m,p)` | minor 99 = current | `sys/sys/param.h` 54-87 |
| OpenBSD | monolithic kernel | `OpenBSD` | year and month, 202610, with a per-release macro `OpenBSD8_0` | no | `sys/sys/param.h` 40-45 |
| Darwin (Mach-O) | hybrid kernel | `version_min_command.version` | "X.Y.Z is encoded in nibbles xxxx.yy.zz" (16, 8, 8 bits) | no | `EXTERNAL_HEADERS/mach-o/loader.h` 1242-1253 |
| Windows NT | hybrid kernel | `NTDDI_VERSION` | four bytes: major, minor, service pack, sub-version (`NTDDI_WIN10` 0x0A000000, `NTDDI_WINXPSP2` 0x05010200, `NTDDI_WIN10_RS1` 0x0A000002; `OSVERSION_MASK` 0xFFFF0000, `SUBVERSION_MASK` 0x000000FF) | the service pack, a counter byte | mingw-w64 `sdkddkver.h` 93-164 |
| seL4 | microkernel | none | `VERSION` file: `17.0.0-dev` | string only | `VERSION` |
| Zephyr | RTOS | `KERNELVERSION`, `KERNEL_VERSION_NUMBER`, `ZEPHYR_VERSION_CODE` | (major << 24) + (minor << 16) + (patch << 8) + tweak; the 3-field form is (major << 16) + (minor << 8) + patch; EXTRAVERSION is a string | no | `cmake/modules/version.cmake` 23-25, 92-96; `VERSION` |
| FreeRTOS | RTOS | `tskKERNEL_VERSION_MAJOR`, `_MINOR`, `_BUILD` | three separate integers and `"V11.1.0+"`, where a trailing + means development | string suffix | `include/task.h` 51-60 |
| ThreadX | RTOS | `THREADX_MAJOR_VERSION` and four more | separate macros: 6, 5, 2, build 202603 (year and month), hotfix `'b'` | no | `common/inc/tx_api.h` 91-95 |
| RT-Thread | RTOS | `RTTHREAD_VERSION` | `RT_VERSION_CHECK(major, minor, revise)` = major * 10000 + minor * 100 + revise | no | `include/rtdef.h` 88-96 |
| Mbed OS | RTOS | `MBED_VERSION` | `MBED_ENCODE_VERSION` = major * 10000 + minor * 100 + patch; 99 and 999999 mark a development tree | development marker | `platform/include/platform/mbed_version.h` 44-58 |
| Pico SDK | SDK (this port's) | none | `PICO_SDK_VERSION_MAJOR`, `_MINOR`, `_REVISION` and `_STRING` | no | `pico/version.h.in` |
| Python | language runtime | `PY_VERSION_HEX` | 0xMMmmppLs: major, minor, micro bytes, release level nibble (0xA alpha, 0xB beta, 0xC candidate, 0xF final), serial nibble; "0x010502B2 == 1.5.2b2" | yes, with serial | `Include/patchlevel.h` 14-48 |
| OpenSSL | library | `OPENSSL_VERSION_NUMBER` | master: major << 28, minor << 20, patch << 4, status nibble 0x0 (layout 0xMNN00PP0L); the pre-release is the string `OPENSSL_VERSION_PRE_RELEASE`; `OPENSSL_VERSION_PREREQ` compares (major << 16) + minor. 1.1.1-stable: `MNNFFPPS`, status 0 development, 1 to e beta, f release (`0x10101180L`) | 1.1.1 yes; master keeps the nibble at 0 | `include/openssl/opensslv.h.in` 79-117 (master); `include/openssl/opensslv.h` 18-42 (1.1.1-stable) |
| SQLite | library | `SQLITE_VERSION_NUMBER` | X * 1000000 + Y * 1000 + Z, "also be larger than the release from which it is derived" | no | `src/sqlite.h.in` 126-132 |
| Boost | library | `BOOST_VERSION` | 100000 * major + 100 * minor + patch (109300) | no | `boost/version.hpp` 18-22 |
| DiscoBSD (this tree) | kernel | `DiscoBSD` | year and month, 202608 | no | `sys/sys/param.h` 8 |

Not obtained: QNX Neutrino, VxWorks and MINIX 3 (no primary header was retrieved; MINIX's `include/minix/config.h` and NuttX's `include/nuttx/version.h` returned HTTP 404 at the paths tried, and the Haiku headers `BeBuild.h` and `HaikuConfig.h` hold no version macro). Their absence (the three 404 paths are listed at the foot of `survey-sources.tsv`) limits the survey to 17 projects: six monolithic or hybrid kernels (Linux, FreeBSD, NetBSD, OpenBSD, Darwin, Windows NT), one microkernel (seL4), five RTOS cores (Zephyr, FreeRTOS, ThreadX, RT-Thread, Mbed OS), the Pico SDK and four libraries (Python, OpenSSL, SQLite, Boost).

### Patterns

- Binary byte fields of 8 or 16 bits dominate in kernels and RTOS cores (Linux, Mach-O, Windows, Zephyr, Python): a decoder is a shift and a mask, and the hexadecimal print is the readable form.
- Decimal fields dominate in embedded C libraries and small RTOSes (Mbed OS, RT-Thread, SQLite, Boost): the printed integer is the version, `3045001` reads as 3.45.1.
- The BSD integers are not release numbers. FreeBSD bumps its integer per noteworthy base-system change, NetBSD's is decimal-positional with a "current" convention in the minor field, OpenBSD's is a year and month. Only NetBSD's is a pure function of the release string.
- A stage inside the integer is rare and has been reversed once: Python keeps it; OpenSSL 1.1.1 uses a status nibble (0 development, 1 to e beta, f release; its own comment lists `0.9.3-beta2-dev` and `0.9.3-beta2` as one value, `0x00903002`) and the master header keeps the nibble at zero and carries the pre-release in a string.
- A field limit is a real failure mode: Linux clamps its patch field at 255 because stable lines exceed it, which makes the integer equal for different releases.
- Several RTOS ship no packed integer (FreeRTOS, ThreadX, Pico SDK): separate macros compare as `MAJOR > a || (MAJOR == a && ...)`.

### Research

SemVer 2.0.0 (retrieved text, items 9 to 11) fixes the precedence relation the integer must respect. Two empirical studies bound what the patch and minor positions promise: Decan and Mens, "What Do Package Dependencies Tell Us About Semantic Versioning?", IEEE Transactions on Software Engineering 2019 (doi 10.1109/tse.2019.2918315), compare semver compliance across Cargo, npm, Packagist and Rubygems and find it varies by ecosystem; Pinckney, Cassano, Guha and Bell, "A Large Scale Analysis of Semantic Versioning in NPM", MSR 2023 (doi 10.1109/msr59073.2023.00073), analyze how updates flow through every npm version. Both concern the meaning of the fields in dependency resolution, not their packing; they support one design rule here: the integer states an order and encodes no compatibility promise. A search of OpenAlex on 2026-11-21 for work on packing a release into an integer (query "version number comparison encoding software release identification", title, abstract and keywords, 23 results) returned no relevant work in its top six, and the survey above rests on source headers.

## Recommendation

The study supports three statements; the owner decides each.

1. Radix. Binary, if the first reader is a program or a person using `printf '%#x'`: order, widths and decoders are simplest, and the only cost is that the plain `sysctl` print (decimal) is unreadable (65537 for 0.1.0-alpha). Decimal, if people read the number directly: the layout of `version-scheme.md` or a SQLite-shaped one (`major * 1000000 + minor * 1000 + patch`, a stage digit appended) prints close to the version. The measured price of decimal is 54 more bytes and six helper calls in a reader.
2. Widths, for binary. 7.8.12.4 (major 0 to 127, minor 0 to 255, patch 0 to 4095, stage 4 bits) with an explicit rank per stage in the version source (S1), which accepts any SemVer pre-release identifier; or 7.8.11.5 (patch 0 to 2047, stage 5 bits) with the rank taken from the first letter of the stage name (S2), which removes the per-stage edit, ties the rank to SemVer's ASCII order and narrows the stage grammar to names that start with a lowercase letter. Patch 4095 is 16 times Linux's 255, whose stable lines have outgrown it.
3. Timing. Nothing reads the integer, so the encoding can wait for the first reader. The generator and its compile-time range checks (each field against its limit, the weights against the spans of the fields below, and the stage rank against the stage name under S1 or S2) are the same for every layout above and are the part D112 requires.

What would change this: a reader that cannot decode (decimal wins); a patch counter that approaches 4095 (widen it from the stage or minor field); a need for pre-release serials (`alpha.2`; give Python's serial nibble 4 bits from patch); two stage names with one initial in one core (S2 ties; use S1).

## Limits of this study

- Decoder sizes cover four small C functions on one compiler and one optimization level; the generator script (POSIX sh and awk in `newvers.sh`) is not written or measured.
- The survey reads the default branch of each project on one date. It states what the header says, not how the project arrived at it, and the encodings of projects not listed are not inferred from the ones that are.
- The order checks run on a boundary grid, not over all 2^31 values; the mixed-radix argument covers the rest.
- The OpenAlex search is one query; its negative says that query's top results held no encoding study.
- Nothing here changes `kern.osrev`, `param.h`, `newvers.sh` or any tool; the study states no owner decision.

## Reproduction

```sh
python3 docs/research/packed-version-integer/encodings.py --selftest
python3 docs/research/packed-version-integer/encodings.py > docs/research/packed-version-integer/results.txt
```

`survey-sources.tsv` lists the URL, HTTP status and size, SHA-256 and retrieval date of each header read.
