# Version numbering for GizmoBSD: what the tree encodes, what others publish, three schemes and a composite identifier

Source reading at origin/main 32927ed0 (merge of #620); the text is a research record. Compiler for every size: arm-none-eabi-gcc 16.2.0 (local; CI uses 14.2.1, and sizes from the two compilers are not subtracted). Published pages were retrieved with curl on 2026-11-16 and every quotation below was matched with grep against the retrieved text; an item read only through a summarizing fetch or a search result is marked (S) and carries no quotation. Inferences are marked (I). The owner decisions this note answers are quoted inline where used: D51, the owner's request of 2026-11-16 ("GizmoBSD 0.a or ... do we use 'Rev A' to get started? Research and figure out what to do"); D52, a GizmoBSD param.h macro carrying the version D51 decides, beside the kept DiscoBSD, RetroBSD and 2.11BSD lineage macros; D38, host package gizmobsd-host 2.0 with discobsd-* command aliases for one release; and the rename plan's proposals D3 (version "GizmoBSD 0.1", with "derived from DiscoBSD 2.7" in etc/COPYRIGHT and motd) and D4 (a GizmoBSD 202611 value returned by KERN_OSREV).

## Status after the owner decisions of 2026-11-16

The owner answered the first analysis with eight decisions, quoted here by number:

- D87: scheme S3, the revision letter ("Rev A"), chosen over the recommended S1.
- D88: the pre-release label appears in the version string and in `osrelease` (`uname -r`).
- D89, owner's words: "ensure 64-bit time is landed but its still Rev A alpha however the styling is for that." The tree stays "Rev A alpha" until the 64-bit `time_t` change lands (`docs/research/time-representation-past-2038.md`); the first unlabeled Rev A requires it.
- D90: OS release tags use the `gizmobsd-v*` namespace; `host-v*` stays the host package.
- D91: the label is spelled `RevA-alpha`, identically in the version string and in `osrelease`, with no space and no `newvers.sh` field change.
- D92, owner's words: "Rev A.1, A.2 with Date suffix: Rev A 2026.12 so figure this odd RevA.Semver.datever string being conconcted. Decompose to first principles and elucidate modern research!"
- D93, owner's words on `KERN_OSREV`: "Year-month probably but really you are working on an interesting string".
- D94: banners in gates and fixtures derive from `newvers.sh`, as a calibrated gate change after the rename.

The S1 analysis, its recommendation, its falsifiers and its measurements stay below as recorded analysis; D87 did not adopt S1. The S3 representation with a display value (`Rev A` in `version[]`, `RevA` in `osrelease[]`) is superseded by D91 and stays as a measurement. D92 and D93 are answered by "The composite identifier" below. The sections added after D87 were retrieved and measured on 2026-11-16 under the same rule as the first analysis: a quotation was matched against the retrieved text, an item read through a summarizing fetch or search result is marked (S), and an inference is marked (I).

## Findings

1. The tree uses neither SemVer nor a "Rev" letter. It carries five identities: the upstream DiscoBSD release counter (`2.7`), a year-and-month integer inherited from 4BSD (`202608`), a build identity (commit count and compile count), a root-image stamp from `git describe`, and a host-package number (`1.0.9`) that is SemVer-shaped. Each is set in a different file and no file states how they relate.
2. Owner decision D51 asks for a choice among "0.a", "Rev A" and a SemVer-style number. Plan decision D3 proposes "GizmoBSD 0.1". The string "0.1" is not a valid SemVer 2.0.0 version, which requires three numeric fields (item 2). "0.a" is invalid under SemVer and sorts in opposite directions across package-version algorithms (measured below). "Rev A" breaks the field split in `sys/conf/newvers.sh` (measured below).
3. The inherited release cycle (`sys/conf/newvers.sh:18-20`) keeps the release number after a release and appends "-current". Under SemVer precedence (item 11) "0.1.0-current" sorts below "0.1.0", opposite to its chronology. A SemVer adoption changes that cycle.
4. Firmware identity reads the host-tool tag. `git describe --tags origin/main` prints `host-v1.0.7-2660-g32927ed0`, and `Makefile:2236` writes that string into `/etc/release`.
5. The string-length cost of every candidate is at most 16 bytes of flash `.rodata` and 0 bytes of RAM in `vers.o` (arm-none-eabi-gcc 16.2.0; linked kernel not run).
6. The first analysis recommended S1 (SemVer 2.0.0, 0.y.z). The owner chose S3 (D87), so S1 is recorded analysis and is not adopted.
7. The compatibility letter L, the fix F, the stage P and the build month D are four facts. (L, F, P) in lexicographic order is a total order and the minimal key; D is not a key, because a maintenance release built after the next revision breaks date order. Measured on 11 release-state pairs: the packed (L, F, P) integer orders 11, a year-and-month integer 8.
8. No raw string form is ordered correctly by every algorithm. The hyphen label sorts after its release in plain lexical tools (`sort -V`, git version sort, strcmp: 5 of 11 pairs for the core form), git with `versionsort.suffix` orders 10 of 11, and the one pair every raw form fails is the letter boundary (Y to AA). Mapped forms order 11 of 11 in dpkg, pacman, RPM, PEP 440 and SemVer.
9. The recommended grammar is G1, `RevA.1-alpha+202612`: letter, optional fix, optional stage, and a trailing build month that precedence ignores. The core form (everything before `+`) is the `osrelease`, `uname -r` and tag form; the full form names artifacts.
10. `KERN_OSREV` is recommended as the packed integer `letter * 1000000 + fix * 10 + stage` (`RevA-alpha` is 1000001, above the inherited 202608), which fits a signed 32-bit `long` up to letter index 2146. The year-and-month value stays in the build string.
11. The D91 label in both strings costs +14 bytes of flash `.rodata` against `2.7` (arm-none-eabi-gcc 16.2.0) and 0 bytes of RAM; the full composite in both strings costs +28.
12. Not verified at a primary source: the ASME Y14.35 standard itself (paywalled; quoted from a 2008 presentation and a 2014 draft), illumos and OmniOS release strings, the Fedora packaging guidelines page, and any empirical study of calendar or hybrid versioning (none found). The retrieval caveats of the composite section list the rest.

## What the tree encodes

| Item | Value and site | Consumer |
| --- | --- | --- |
| OS name and release | `OST="DiscoBSD"`, `OSR="2.7"`, `sys/conf/newvers.sh:22-23` | `ostype`, `osrelease`, `version` strings (`newvers.sh:98-102`); `uname -sr` reads `KERN_OSTYPE` and `KERN_OSRELEASE` (`usr.bin/uname/uname.c:100-126`) |
| Release stage | `S=""`, with `-current` and `-beta` commented out, `newvers.sh:25-27` | appended to the `version` string only; `osrelease` carries no suffix (`newvers.sh:98,102`) |
| Build identity | commit count (`git rev-list --count HEAD`, `newvers.sh:74`) and a compile counter reset when the count changes (`newvers.sh:65-86`) | `version` ("#1 2999") and `osversion` ("PICO#1") |
| Year-and-month integer | `#define DiscoBSD 202608`, `#define DiscoBSD_2_7 1`, `sys/sys/param.h:8-9`; `#define BSD 211` (2.11 times 100), `sys/sys/param.h:6` | `KERN_OSREV` returns `(long)DiscoBSD` (`sys/kern/kern_sysctl.c:231-232`); `DiscoBSD_2_7` has no use in the tree (`git grep DiscoBSD_2_7` outside docs finds only its definition; `newvers.sh:13` lists `DiscoBSD_X_X` as a field to edit) |
| Make-level release | `OSMAJOR=2`, `OSMINOR=7`, `OSREV=${OSMAJOR}.${OSMINOR}`, `share/mk/sys.mk:4-8`; `VERSION=${RELEASE}-${BUILD}`, `sys.mk:15` | man-page footer `-Ios="DiscoBSD ${OSREV}"` (`sys.mk:208`); archive names `DiscoBSD_${OSRev}_${MACHINE}` (`etc/Makefile:165-166`); the three-field case has no Make variable |
| Root-image stamp | `git describe --always --dirty --tags`, `Makefile:2236` | `/etc/release` |
| Banner and motd text | `etc/motd:1`, `README.md:19,49,68`, `distrib/notes/ANNOUNCEMENT.md:1-17` | people |
| Literal "DiscoBSD 2.7" in checks | 48 tracked files contain the string (`git grep -lI "DiscoBSD 2\.7"`), 34 under `docs/` and 14 elsewhere: 7 gate and fixture files (the five Renode files below, `tests/build_failure/check.py`, `usr.bin/picofetch/tests/render.py`), 3 shipped text files (`README.md`, `distrib/notes/ANNOUNCEMENT.md`, `etc/motd`), 3 documents (`sys/arch/rp2040/doc/TESTING.md`, `sys/arch/rp2040/doc/research/emulation.md`, `legacy/pdp11-v6/docs/research/v6-emulator-on-discobsd.md`) and the `param.h` comment; `hwinfo_fixture.c` holds the type and the number as separate strings (lines 85-86) and is not among the 14, so the rename inventory is these files plus that fixture; Renode stages `tools/renode/check-boot.py:570,582`, `check-capacity.py:217`, `check-identity.py:142`, `check-picocalc.py:54,572,770`, `picocalc_fb.py:199`; `tests/build_failure/check.py:530`; the picofetch fixture `usr.bin/picofetch/tests/hwinfo_fixture.c:86` and `render.py:242`; `sys/arch/rp2040/doc/TESTING.md:2875` | substring match on the boot banner and `uname -sr` output; nothing parses the number into fields (a `git grep` for `osrelease`, `kern.version` and `OSRELEASE` over `tools tests usr.bin sbin bin lib/libc/gen distrib` finds display code in `usr.bin/picofetch`, the library call `lib/libc/gen/uname.c:69`, a gate stub `tests/kernel/sysctl_test.c:130` and a comment at `tests/rp2040/swap_tier_model/trace.py:184`) |
| Host package | `version = "1.0.9"` (`distrib/rp2040/host/pyproject.toml:7`), `__version__ = "1.0.9"` (`distrib/rp2040/host/src/discobsd_host/__init__.py:11`), `discobsd-host (1.0.9)` (`distrib/rp2040/host/packaging/debian/changelog:1`), `pkgver=1.0.9` (`distrib/rp2040/host/packaging/arch/PKGBUILD:14`); release workflow fires on tags `host-v*` (`.github/workflows/host-release.yml`) | pip, dpkg, pacman; the last tag is `host-v1.0.7` (`git tag -l "host-v*"`, 2026-09-16), so 1.0.8 and 1.0.9 carry no tag |
| Planned | host package `gizmobsd-host 2.0.0` with `discobsd-*` aliases for one release (owner decision D38, fixed); OS version open (D51); `param.h` macro follows D51 (D52); plan D3 proposes "GizmoBSD 0.1" with "derived from DiscoBSD 2.7" in `etc/COPYRIGHT` and motd, and D4 a `GizmoBSD 202611` value for `KERN_OSREV` (the rename plan's open proposals) | |

Upstream release history (`git tag`, tag commit dates): DISCOBSD_2_0 2022-12-31, 2_1 2023-08-31, 2_2 2024-02-29, 2_3 2024-08-11, 2_4 2025-02-11, 2_5 2025-08-11, 2_6 2026-02-11, 2_7 2026-08-11. The `OSR` history (`git log -G"^OSR="`) shows `2.2-beta` through `2.7-beta` commits between 1 and 31 days before each release tag, then upstream `2.7-current` on 2026-09-15 (upstream/master d143605c). The upstream rule is one counter step in tenths per half-year release: the number increments when the beta starts, the release keeps it, and -current keeps it after the release (`newvers.sh:18-20`).

The 2.11BSD ancestry uses patch levels. The fork's divergence point is recorded in neither tree (`docs/research/211bsd-patch-scope.md:10`); the comparison checkout there begins at the TUHS patch-level 431 tape (line 20), and the fork's files match the pre-patch text of patches 432 to 499 (docs/research/upstream-patch-survey.md, PR #645, which classifies each patch against this tree), so the fork stands at patch level 431 or earlier. The patch 499 announcement (http://www.2bsd.com/2.11BSD/499, plain HTTP; HTTPS refuses the connection) says "Later in 2026 patch 500 will be the formal announcement (/VERSION change) and release of "2.11BSD: The 35th Anniversary Edition"": the patch number is the working identity and the /VERSION file changes only at a named release. Patch 500 itself shipped on 2026-11-11 as a 3,998,518-byte release patch (same survey).

## Published critiques and counter-arguments

| Source | Complaint, in one line | Counter-argument or limit |
| --- | --- | --- |
| Rich Hickey, "Spec-ulation", Clojure/Conj 2016 (Dec 2016); transcript https://github.com/matthiasn/talk-transcripts/blob/master/Hickey_Rich/Spec_ulation.md | One number conflates growth with breakage: "Is Semantic Versioning broken? Totally. Yes, broken. Bad idea." and the major component says "you might be screwed". "Breaking changes are broken." | SemVer FAQ: "This is a question of responsible development and foresight" (https://semver.org/#faq). Hickey's remedy is accretion (add a new name, keep the old), which an OS can apply to syscalls and libc symbols but not to a changed width of `time_t` (I). |
| Hynek Schlawack, "Semantic Versioning Will Not Save You", 2 March 2021, https://hynek.me/articles/semver-will-not-save-you/ | "it is impossible to predict the ways a change can affect your users"; a project on 0.x that claims production readiness is "a paradox". | The article scopes SemVer as a signal of intentional breakage; SemVer item 1 requires a declared public API. |
| Hyrum Wright, "Hyrum's Law", https://www.hyrumslaw.com/ | "all observable behaviors of your system will be depended on by somebody"; a version number cannot cover behavior outside the contract. | An enumerated contract list (S1) states which observables are promised; the rest stays unpromised. |
| ZeroVer, 0ver.org, 1 April 2018 (S: a satire per https://nesbitt.io/2024/06/24/from-zerover-to-semver-a-comprehensive-list-of-versioning-schemes-in-open-source.html) | "Your software's major version should never exceed the first and most important number in computing: zero." Projects stay at 0.x indefinitely. | SemVer FAQ: "If your software is being used in production, it should probably already be 1.0.0." (S1 states 1.0.0 criteria.) |
| Liam Proven, The Register, "ReactOS emits release 0.4.15 -- its first since 2021", 2025-03-25, https://www.theregister.com/2025/03/25/reactos_drops_release_0415/ | Reports "skepticism about ZeroVer" for a long-lived volunteer OS still on 0.4.x and asks readers to take the project seriously despite its number. | The article separates the number from the system's capability. |
| Linus Torvalds, "Linux 4.0-rc1 out..", 22 Feb 2015, https://lkml.iu.edu/hypermail/linux/kernel/1502.2/04059.html | "So on the whole, I wouldn't read too much into the number." The kernel number reads as a counter (I). | A counter suffices where the one interface (syscalls) is promised stable for all releases; this port has no such promise yet (I). |
| calver.org (Mahmoud Hashemi) | CalVer concedes: "CalVer deliberately does not attempt to communicate breaking changes." and asks for a changelog plus a deprecation policy. | Ubuntu (YY.0M.MICRO), pip, and the IANA tz database use it; the FAQ answers the concession with "There is no substitute for a changelog and policy." |
| Patrick Kidger, "Real Semantic Versioning", 15 Jan 2025, https://kidger.site/thoughts/real-sem-ver/ | Practice differs from the specification: libraries use major bumps as marketing and minor bumps for breaking changes. | Ochoa, Degueule, Falleri and Vinju, "Breaking Bad? Semantic Versioning and Impact of Breaking Changes in Maven Central" (arXiv:2110.07889, 2021): 83.4% of 119,879 upgrades comply with SemVer and 7.9% of clients are affected by breaking changes. |
| Go modules, https://go.dev/doc/modules/version-numbers | v0 "makes no stability or backward compatibility guarantees"; the consumer adapts until v1. | Cargo reads 0.y.z as y being the breaking position: "only changes in the left-most non-zero component are considered incompatible" (https://doc.rust-lang.org/cargo/reference/semver.html). |
| "Talking to SerenityOS Contributors About a Scratch-built C++ Developer's Playground in Modern C++", JetBrains CLion blog, Nov 2021, https://blog.jetbrains.com/clion/2021/11/talking-to-serenityos-contributors/ | The project has "no versions or releases either, as everything is still changing rapidly." | Appropriate for a single-developer-community without installed users; this port ships images and a host package (I). |

Critiques aimed at hobby and small operating systems specifically are absent from the sources retrieved. Queries run on 2026-11-16, verbatim: (1) `"Semantic versioning" "ZeroVer" hobby project never reaches 1.0 critique blog`; (2) `hobby operating system version numbering "0.1" never 1.0 critique osdev forum how to version your OS`; (3) `osdev wiki OSDev forum "version number" my os naming scheme advice semantic versioning hobby kernel`; (4) `ReactOS stays at 0.4 version number never 1.0 criticism developers explain why not 1.0`; (5) `Redox OS version 0.9 when 1.0 versioning policy semver pre-1.0 "0.x" operating system`; (6) `SerenityOS no version numbers rolling why no releases versioning philosophy`. The nearest results are the ZeroVer items above and the Serenity interview. An OSDev Wiki "Code Management" page is reported by one search summary to discuss versioning; the page returned a Cloudflare challenge to curl and was not read. No retrieved source confirms a published critique aimed at hobby-project numbering; the general critiques above are the nearest.

## Schemes real systems use

| System | Scheme | Source |
| --- | --- | --- |
| NetBSD | major (N.0), minor (N.M, from the maintenance branch), "teeny" security release (N.M.P); -current is always N.99.M | https://www.netbsd.org/releases/release-map.html: "The version number is always in the form of N.99.M" |
| FreeBSD | X.Y-RELEASE, -STABLE, -CURRENT (S: FreeBSD Handbook, cutting-edge chapter); an integer `__FreeBSD_version` encoded `MMmmRxx` and bumped on every noteworthy change; `BSD 199506` is a "System version (year & month)" | https://cgit.freebsd.org/src/plain/sys/sys/param.h (matched lines "Encoding: <major><two digit minor>Rxx" and "System version (year & month)") |
| OpenBSD | a release every six months (S: https://www.openbsd.org/faq/faq5.html, read through a summarizing fetch); the +0.1 step per release is not read from a primary page | -- |
| 2.11BSD | patch level as working identity; /VERSION changes at a named release | patch 499 text, above |
| DiscoBSD (upstream) | release counter in tenths, semiannual, with beta/release/current cycle; year-and-month integer in `param.h` | tags and `newvers.sh`, above |
| Linux | a counter; 4.0 followed a poll ("People preferred 4.0, and 4.0 it shall be."); the odd/even stable-development split ended with 2.6 (S: Wikipedia, "Software versioning") | lkml 4.0-rc1 |
| TeX | digits of pi appended per maintenance release: 3.0, 3.1, 3.14, 3.141, ..., 3.141592653 (January 2021); the banner string is edited at each step (`tex.web` history comment and `@d banner==` line, https://mirrors.ctan.org/systems/knuth/dist/tex/tex.web) | tex.web |
| Haiku | "R1/beta6" release notes title (https://www.haiku-os.org/get-haiku/release-notes/); the project names a release line (R1) and counts betas inside it | release notes |
| SemVer 2.0.0 | X.Y.Z with pre-release and build metadata; 0.y.z is initial development where "Anything MAY change at any time" (item 4); 1.0.0 "defines the public API" (item 5); FAQ: "The simplest thing to do is start your initial development release at 0.1.0" | https://semver.org/spec/v2.0.0.html |
| CalVer | YYYY, YY, 0M, MM and similar components; Ubuntu YY.0M, Twisted YY.M.MICRO, IANA tz YYYYa..z | https://calver.org/ |
| Zephyr | `VERSION` file with VERSION_MAJOR, VERSION_MINOR, PATCHLEVEL, VERSION_TWEAK, EXTRAVERSION; main read as 4.5.99 on 2026-11-16 | https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/VERSION |
| MCUboot image versions | the imgtool dependency example reads "1.2.3+0": three fields plus a build number | https://raw.githubusercontent.com/mcu-tools/mcuboot/main/docs/imgtool.md |
| RP2040 silicon | stepping names B0, B1, B2 (letter and digit); the machine-readable register stores a small integer, and the Pico SDK comments "Version 1 == B0/B1": the integer cannot tell two named steppings apart | https://raw.githubusercontent.com/raspberrypi/pico-sdk/master/src/rp2040/pico_platform/platform.c |
| Hardware documents | vendor revision letters on drawings and datasheets, for example "revision G" (W25Q16JV) and "Rev B" (Abracon drawing 456603) | `docs/research/hardware-reference-coverage.md:88-89` |

Letter revisions (Rev A, Rev B) in hardware mark a change-controlled configuration. PCB-revision conventions (letters for fabricated boards, numbers before first release) appear only in forum threads that a search surfaced and that were not read (S); no rule below derives from them. RetroBSD release numbering is not documented in the tree: `README.md:18` and `docs/research/retrobsd-port-scope.md:43-46` record the lineage only, `git grep -n -i "release\|version" -- docs/research/retrobsd-port-scope.md` matches nothing, the Wikipedia page for RetroBSD returned 404, and the earliest `sys/conf/newvers.sh` in history (439e93d2) already reads `OST="DiscoBSD"`, `OSR="2.1"`. No RetroBSD scheme was retrieved.

## Measurements

### Ordering of a letter in a version string

Tools: python 3.14.9 with packaging 26.3 (PEP 440), pacman vercmp, GNU sort -V, and a Python port of dpkg's `verrevcmp` (Appendix B) calibrated on the Debian Policy 5.6.12 examples (known-good: `1.0~beta1~svn1245 < 1.0~beta1 < 1.0 < 1.0a`, `~~ < ~~a < ~ < "" < a`; known-bad control: `1.0a < 1.0` reports false). No dpkg binary is installed on this host, so the Debian row rests on the port.

| Comparison | PEP 440 | pacman vercmp | sort -V | dpkg (port) |
| --- | --- | --- | --- | --- |
| 0.a against 0.1 | 0.a is lower (normalized to `0a0`, an alpha pre-release of 0) | lower | lower | higher |
| 0.a against 0.b | lower | lower | lower | lower |
| 0.9 against 0.10 | lower | lower | lower | lower |

A letter in the position of the minor number is therefore placed on opposite sides of "0.1" by two common package-version algorithms. SemVer item 2 excludes it outright. No OS-side tool compares `osrelease` today, so the effect reaches only a package or script that copies the string (I).

### Pre-release labels in version ordering

Same tools as above, with a Python implementation of SemVer 2.0.0 item 11 (Appendix B). Known-good and known-bad controls: item 11's own example order `1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0` holds, and `1.0.0 < 1.0.0-alpha` reports false.

| Pair | SemVer item 11 | PEP 440 (packaging 26.3) | pacman vercmp |
| --- | --- | --- | --- |
| 0.2.0-alpha against 0.2.0-beta | lower | lower | lower |
| 0.2.0-beta against 0.2.0-dev | lower | higher | lower |
| 0.2.0-alpha against 0.2.0 | lower | lower | equal |
| 0.1.0-current against 0.1.0 | lower | invalid | equal |

The pacman "equal" rows follow from its package format: the hyphen starts the package-release field, so `0.2.0-alpha` is version 0.2.0 with release "alpha" and a pre-release label has no place in a pacman version (I). Debian expresses it with a tilde (Debian Policy 5.6.12: a tilde "sorts before anything, even the end of a part"). The OS string is not packaged, so these mappings concern only a package that embeds it.

### Field split in newvers.sh

`newvers.sh:90-96` pipes `$GITREV $CV $OST $OSR $ID $S` through awk with whitespace splitting. Run with `OSR="Rev A"`: the output is `version[] = "GizmoBSD RevPICO (A) #1 ..."`, `osrelease[] = "Rev"`, `osversion[] = "A#1"`. A release value with a space needs a script change; a value without a space (`RevA`) does not.

The S3 representation as specified, `Rev A` in `version[]` and `RevA` in `osrelease[]`, was built through two scratch variants of `newvers.sh` (Appendix A.2). A display value `OSD` that `version[]` prints and `osrelease[]` ignores, with `OSR="RevA"`, yields `version[] = "GizmoBSD Rev A (PICO) #1 ..."` and `osrelease[] = "RevA"`: 82 bytes of `.rodata`, +3 against DiscoBSD 2.7. Splitting the awk input on `|` instead of whitespace with `OSR="Rev A"` puts `Rev A` in both strings: 83 bytes, +4. The earlier +2 holds only for `RevA` in both. The +1 of the split form over the earlier row is the space in `version[]`; the +1 of the delimiter form over the split form is the space in `osrelease[]`. Compiler `arm-none-eabi-gcc 16.2.0`, the flags of the table below; `.data` and `.bss` are 0 in all three.

### Size cost (kernel text and kernel RAM)

`vers.o` from `newvers.sh` with a stub `git` reporting 2999 commits and `SOURCE_DATE_EPOCH=1794700000`, compiled with `arm-none-eabi-gcc 16.2.0 -mcpu=cortex-m0plus -mthumb -Os -std=c17 -ffreestanding`, sizes by `arm-none-eabi-size -A` (Appendix A). The four strings are `const char` arrays, so they land in `.rodata` (flash); `.data` and `.bss` are 0 in every row, and the existing PICO build's `vers.o` also holds 0 and 0. The linked-kernel text change is not measured (kernel builds run on the remote build host and were not run); section alignment in the link can add padding to each delta.

| OST / OSR | `.rodata` bytes | Delta from DiscoBSD 2.7 | `.data`+`.bss` |
| --- | --- | --- | --- |
| DiscoBSD / 2.7 | 79 | 0 | 0 |
| GizmoBSD / 0.1.0 (S1) | 83 | +4 | 0 |
| GizmoBSD / 0.1.0 with S="-alpha" (label in the version string only, as today) | 89 | +10 | 0 |
| GizmoBSD / 0.1.0-alpha as OSR (label in version and osrelease) | 95 | +16 | 0 |
| GizmoBSD / 2026.11 (S2) | 87 | +8 | 0 |
| GizmoBSD / `Rev A` in `version`, `RevA` in `osrelease` (S3 as specified, needs a display value) | 82 | +3 | 0 |
| GizmoBSD / `Rev A` in both strings (S3 with a delimiter fix only) | 83 | +4 | 0 |
| GizmoBSD / `RevA` in both strings (earlier S3 row, no script change) | 81 | +2 | 0 |
| GizmoBSD / 0.a | 79 | 0 | 0 |

"GizmoBSD" and "DiscoBSD" have the same length, so the rename alone costs 0 bytes. A new sysctl node for an ABI epoch would add kernel text and is not proposed; every epoch below lives in a build-time file.

## Schemes

### S1: SemVer 2.0.0 initial-development band, 0.y.z

Status: not adopted (D87); recorded analysis.

- Rules. Three numeric fields. While X is 0, y increments when an item in the contract list changes incompatibly, z increments for compatible fixes, and a label (`-alpha` on trunk, `-beta` before the cut) follows SemVer item 9. The list (I): the a.out user ABI (header and magic, system-call numbers and argument layouts, layouts and fundamental-type widths in installed headers, the installed libc interface whatever its linkage: declarations in the installed headers and the function and data symbols with their calling conventions, which are `/usr/lib/libc.a` today (`distrib/rp2040/mi.rp2040:350`; the image ships no shared libc) and a shared libc if one is added (`docs/research/xip-shared-libc` is a design, not a shipped object). A removed export or an incompatible declaration breaks the rebuild of existing programs, so it moves y and a z increment cannot carry it); on-disk and flash formats (partition map, root-file-system layout, swap); the host-visible wire (USB identity, console framing, the sysctl names the host tools read). 1.0.0 is cut when the list has not changed incompatibly across two consecutive releases and the host-link wire is documented (I).
- Release cycle. After a release the trunk moves to the next planned version with `-alpha` (0.2.0-alpha after 0.1.0), the cut candidate carries `-beta` (0.2.0-beta), and the release carries no label (0.2.0). SemVer item 11 orders these 0.1.0 < 0.2.0-alpha < 0.2.0-beta < 0.2.0, and PEP 440 agrees (measured below). The label `-dev` is excluded: ASCII order puts it above `-beta` under item 11 and below it under PEP 440. This replaces the inherited "keep the same number, append -current" step (`newvers.sh:18-20`), whose "0.1.0-current" sorts below 0.1.0.
- First value. `GizmoBSD 0.1.0`, with `etc/COPYRIGHT` and motd stating "derived from DiscoBSD 2.7" (plan D3).
- param.h. `GizmoBSD 202611` unchanged in role, returned by `KERN_OSREV` (plan D4); `GizmoBSD_0_1 1` joins `DiscoBSD_2_7`, which plan D4 keeps. The integer is monotone across the rename (202611 above 202608), which the string ordering "0.1.0" below "2.7" is not.
- Host package. Independent SemVer. `gizmobsd-host 2.0.0` (D38) stays; its README names the OS contract range it speaks ("GizmoBSD 0.1.x console and link wire"). OS releases tag as `gizmobsd-v<version>` and the root-image stamp calls `git describe --match 'gizmobsd-v*'`; `host-v*` keeps its meaning.
- Answers to the critiques. Hickey and Hynek: the number claims one thing, that the contract list changed, and 0.y.z states in the specification that anything may change. Hyrum: only the listed observables are promised. ZeroVer: the 1.0.0 criterion is written down. Go and Cargo read 0.y.z as y breaking, the same reading the list gives.
- Tree changes. `newvers.sh` (OST, OSR, cycle comment, and the awk step so `$S` reaches `osrelease` if pre-release builds must differ under `uname -r`); `sys.mk` (an `OSPATCH` variable, with `OSREV`, `OSRev` and `OSrev` built from three fields); `param.h`; `Makefile:2236` (--match); the literal banner in 7 gate and fixture files, 3 shipped text files and 3 documents (listed in the tree table); the picofetch fixture `usr.bin/picofetch/tests/hwinfo_fixture.c:85-86`, which holds the type and release as separate strings, so `render.py:242` and the fixture move together; package metadata maps the label with each format's own syntax. The banner edits occur in the rename in any case, because the name changes.
- Cost. +4 bytes flash `.rodata` and 0 bytes RAM for the unlabeled release string; a label in the version string adds 6 bytes of flash (`-alpha`), and a label also in `osrelease` adds 6 more.

### S2: CalVer, YYYY.MM

Status: not adopted (D87); recorded analysis.

- Rules. The version is the release month. Compatibility is carried by a separate integer, the ABI epoch, held in a file the image ships (`/etc/release` or a sibling) and incremented when the contract list of S1 changes incompatibly; calver.org's own guidance pairs the date with a changelog and a deprecation policy.
- First value. `GizmoBSD 2026.11` (or the release month), ABI epoch 1.
- param.h. `GizmoBSD 202611`; the digits now equal the printed version, removing today's pair of "2.7" and 202608. `DiscoBSD_2_7` has no replacement. `KERN_OSREV` = 202611.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the number never claims compatibility. ZeroVer: no 0.x band. Cost of the answer: the printed version says nothing about a break, so a user needs the epoch.
- Tree changes. As S1 (`OSPATCH` unneeded); the cycle comment changes to name a month, and `OSR` becomes a second date next to the date already in the `version` string.
- Cost. +8 bytes flash `.rodata`, 0 bytes RAM. The release month is a property of the day a release is cut; the upstream cadence (Feb and Aug) fits, a fork cadence is unknown (I).

### S3: Revision letter, hardware style, "Rev A"

Status: adopted by D87. The display-value representation below (`Rev A` in `version[]`, `RevA` in `osrelease[]`) is superseded by D91, which spells `RevA-alpha` in both strings with no `newvers.sh` change; the measurements stay.

- Rules. One uppercase letter names a change-controlled configuration. The letter advances when the contract list of S1 changes incompatibly; fixes inside a letter are identified by the build identity (`#N`, commit count). 26 letters bound the series; a continuation (AA) is a rule to add.
- First value. `Rev A` in the `version` string, written `RevA` in `osrelease`.
- param.h. `GizmoBSD_REV 1` (A is 1) returned by `KERN_OSREV`. This loses the year-and-month value of D4 in that sysctl; the date moves to the build stamp. `sys/sys/sysctl.h:129` names `KERN_OSREV` "system revision", which the letter fits literally.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the letter claims one bit and says so. Hyrum: the same contract list. ZeroVer: no 0.x band. Cost: no ordering of magnitude, no pre-release vocabulary, and tools that sort versions treat letters as in the table above.
- Tree changes. As S1, plus a second value in `newvers.sh`: a display value that `version[]` prints (`Rev A`) beside the unspaced `OSR` (`RevA`) that `osrelease[]` and `osversion[]` consume. The scratch variant changes four lines of the script (Appendix A.2); a delimiter change alone would put the space in both strings.
- Cost. +3 bytes flash `.rodata` for the specified split, +4 with `Rev A` in both strings, 0 bytes RAM. The earlier +2 measured `RevA` in both strings, which is neither form.

### The owner's two candidates by name

Recorded analysis; D87 retained "Rev A".

- "GizmoBSD 0.a". Rejected as an OS version. SemVer item 2 requires numeric fields; PEP 440, pacman, and sort -V place it below "0.1" while the dpkg algorithm places it above (measured above); the alphabet bounds the series. The intent behind it, a pre-release signal, is expressed by S1 with `0.1.0-alpha`.
- "Rev A". Retained as S3 and not recommended as the series. It has a hardware precedent (the tracked datasheet table, and the RP2040 stepping names), and the RP2040 case shows the weakness: the silicon keeps a letter label for people and a small integer for software, and the integer cannot separate B0 from B1. A letter alone leaves software without an ordered value; S1 keeps a letter-free integer in `KERN_OSREV`.

## One scheduled break through each scheme

The 64-bit `time_t` at the user interface is an incompatible user-ABI change (the owner chose the 64-bit interface, recorded at `docs/research/time-representation-past-2038.md:147`).

| Scheme | Release before | Release after | What a user learns |
| --- | --- | --- | --- |
| S1 | 0.1.0 | 0.2.0 | y moved: the contract list changed; recompile programs |
| S2 | 2026.12 (epoch 1), illustrative | 2027.01 (epoch 2), illustrative | the month moved always; the epoch file carries the break |
| S3 | Rev A | Rev B | the letter moved: the contract list changed |

## The composite identifier: compatibility letter, fix and date

Owner decision D92 asks for one string that carries a revision letter, a fix number and a date ("Rev A.1, A.2 with Date suffix: Rev A 2026.12"). This section derives which facts the string must carry, which fields are independent, what published practice and research say, and specifies a grammar with measured behavior. The harness is Appendix B; the size replay is Appendix A.3. Sources were retrieved with curl on 2026-11-16 and each quotation was matched against the retrieved text; retrieval caveats are listed at the end of the section.

### What each consumer needs from the string

| Consumer | Reads | Needs | Form that serves |
| --- | --- | --- | --- |
| A person reading the boot banner or `uname -sr` | `version[]` and `ostype` plus `osrelease` | the compatibility letter and whether the tree is pre-release; the build date already follows the build counter in `version[]` | core form |
| `uname -r` and `kern.osrelease` (`KERN_OSRELEASE`) | a string up to 255 bytes (`SYS_NMLN` is 256 in `sys/sys/utsname.h`) | stability across rebuilds of one release, so the exact-match gates in 7 gate and fixture files keep matching; no whitespace, because `newvers.sh` splits fields with awk | core form (a date in the string changes it monthly) |
| `KERN_OSREV` | a signed 32-bit `long` (`sysctl_rdlong` with `(long)DiscoBSD`, `sys/kern/kern_sysctl.c:231`; `long` is 32 bits on ARMv6-M) | one integer comparison, "is this kernel at least release X" | packed integer |
| Host tools' compatibility check | no such check exists in `distrib/rp2040/host/src` (a search for `osrelease`, `kern.version` and `uname` there matches nothing) | equality on the letter and a minimum on the fix, parsed from `uname -r` or read as the integer | core form, and the integer |
| `git describe --match 'gizmobsd-v*'` | tag names; output gains `-<n>-g<hash>` | a legal ref name, matchable by glob, parsable from the right | core form, verbatim |
| Package-version algorithms (dpkg, pacman `vercmp`, RPM, PEP 440, SemVer, `sort -V`) | the raw string, or a mapped form | an order equal to release order | mapped forms (below) |
| File names (image stamp) | any file system, FAT included | none of `<>:"/\|?*` or control characters, at most 255 bytes, no variant of the title that differs only in case | full form |

### Information content

The string carries four facts:

- L, the compatibility epoch: an ordinal 1, 2, ... shown as a letter. It advances when the contract list of S1 changes incompatibly (S3 rule).
- F, the fix sequence: an ordinal 0, 1, 2, ... within one L, shown as `.F` and omitted when zero.
- P, the stage: alpha, beta or final, ordered alpha < beta < final, each below the version it precedes.
- D, the build month: the UTC year and month of `SOURCE_DATE_EPOCH`, six digits.

The independence relations follow from how releases happen.

1. (L, F, P) in lexicographic order is a total order on release states, and it needs no other field. This is the minimal field set that keeps a total order.
2. D is monotone along the trunk and not across maintenance. A Rev A fix built in 2028-02, after Rev B in 2027-10, is dated later and is the older revision. The measured consequence: a year-and-month integer orders 8 of the 11 measured pairs (the backport pair and the two pairs whose members share a month fail) and a packed (L, F, P) integer orders 11 of 11 (below). D is therefore never a precedence key.
3. F is not derivable from D. A month holds any number of releases, and the fix sequence follows defects, not the calendar. Ubuntu shows the decoupling: its list gives "Ubuntu 24.04 LTS" with release date "Apr 25, 2024" and "Ubuntu 24.04.5 LTS" with release date "Sep 10, 2026" (https://documentation.ubuntu.com/project/release-team/list-of-releases/), so the `24.04` part names the series month and the final integer counts fixes.
4. D is derivable from (L, F, P) only through the release log, which in this repository is the tag object's date. Inside a git checkout D is derived data; in a file name or a sentence that cites a release, D is independent data, because no log travels with it.
5. A rebuild with different content under one (L, F, final) leaves the string unchanged. The rule that closes this: a published final release identifies one artifact set, and a respin takes the next F. The measured tie row shows what the date adds when the rule is broken: forms with D after the fix order the respin later, forms without D compare equal.

### Published practice and research

| Source | What it records | Supports | Argues against |
| --- | --- | --- | --- |
| Ubuntu release list (URL above) | "Releases are published by the Release Team in a time-based fashion (rather than a feature-based one), approximately every 6 months."; the `24.04` and `24.04.5` rows above | a date label for the series and a separate fix integer | a build date as a precedence key |
| NetBSD `sys/sys/param.h`, GitHub mirror https://raw.githubusercontent.com/NetBSD/src/trunk/sys/sys/param.h | `#define __NetBSD_Version__ MMmmrrpp00`; "Up to 2.0I "release" used to be "",A-Z,Z[A-Z] but numeric e.g. NetBSD-1.2D = 102040000 ('D' == 4)"; "NetBSD-2.0H (200080000) was changed on 20041001 to: 2.99.9 (299000900)"; `#define BSD 199506 /* System version (year & month). */` | converting a letter to an ordinal inside a packed integer; a letter series that continues past Z as Z followed by a letter | letters as the fix sequence: the same comment records the switch to numbers (2.0H became 2.99.9) |
| FreeBSD `sys/sys/param.h`, https://cgit.freebsd.org/src/plain/sys/sys/param.h | `Encoding: <major><two digit minor>Rxx`; "'R' is in the range 0 to 4 if this is a release branch or X.0-CURRENT before releng/X.0 is created, otherwise 'R' is in the range 5 to 9."; "__FreeBSD_version is bumped every time there's a change in the base system that's noteworthy."; `#define BSD 199506 /* System version (year & month). */` | a packed integer as the program interface, a digit that encodes the branch stage, a bump per change and not per month | the date as the integer |
| OpenBSD `sys/sys/param.h`, GitHub mirror https://raw.githubusercontent.com/openbsd/src/master/sys/sys/param.h (the project's own FAQ page refused the TLS handshake) | `#define OpenBSD 202610 /* OpenBSD version (year & month). */` beside `#define OpenBSD8_0 1 /* OpenBSD 8.0 */` | the year-and-month integer as a BSD tradition, kept next to a release macro | nothing in the file; the month orders there because releases come twice a year (I) |
| Oracle Solaris 11 IPS article https://www.oracle.com/technical-resources/articles/it-infrastructure/ips-package-versioning.html | `pkg://<publisher>/<name>@<component_version>,<release>-<branch_version>:<timestamp>`; branch version `<trunk_id>.<update>.<sru>.<reserved>.<buildid>.<nightly_id>.<idr>.<idr_id>`; timestamp "ISO 8601 UTC format" | several independent facts in one string with the build time as the last segment | -- |
| ASME Y14.35M as summarized in a 2008 conference presentation https://ndia.dtic.mil/wp-content/uploads/2008/technical/GastonEngineeringDrawingsY14_35.pdf (the standard is paywalled and was not retrieved) | "Upper case letters shall be used in sequence beginning with A and omitting letters “I”, “O”, “Q”, “S”, “X”, and “Z”."; "When the single letters have been exhausted, the revisions following “Y” shall be “AA”, “AB” through “AY”."; "Revision letters shall not exceed two characters."; "Initial issue of a drawing does not constitute need for a revision letter and may be indicated by the use of a – [dash]." The 2014 draft (https://tsp.esta.org/tsp/StandardsWatch/Y14-35_DRAFT_May2014.pdf) applies the omission "for single, double and triple revision letters" and says "letters shall not exceed three alpha characters". | the letter alphabet, the two-letter continuation, a letter that advances on change | lexical order after the continuation (measured below) |
| Debian Policy 5.6.12 https://www.debian.org/doc/debian-policy/ch-controlfields.html | "[epoch:]upstream_version[-debian_revision]."; "Epochs should be used sparingly"; "the purpose of epochs is to cope with situations where the upstream version numbering scheme changes" | an integer that dominates the rest of the string: the mapping of the letter | an epoch per revision is more frequent than "sparingly" suggests (I) |
| PEP 440 https://peps.python.org/pep-0440/ | "E!X.Y # Version identifier with epoch"; "all versions from a later epoch are sorted after versions from an earlier epoch"; "Local version identifiers are used to denote fully API (and, if applicable, ABI) compatible patched versions of upstream projects." | the epoch mapping; the date as a label that preserves compatibility | -- |
| SemVer 2.0.0 item 10 https://semver.org/spec/v2.0.0.html | "Build metadata MUST be ignored when determining version precedence." | the date in an ordering-inert position | -- |
| calver.org | Twisted "uses a three-segment CalVer scheme, with a short year in the major version slot, short month in the minor version slot, and micro/patch version in the third and final slot."; "JetBrains IDEs - YYYY.MINOR.MICRO"; "There is no substitute for a changelog and policy."; "Append an incrementing micro segment, YYYY.M.D.MICRO" | date-led forms with a trailing fix integer (grammar G4) | the same page concedes that the date does not carry compatibility |

Empirical studies, abstracts retrieved from the arXiv API and the Semantic Scholar API (full texts not read):

| Study | Finding | Bearing |
| --- | --- | --- |
| Raemaekers, van Deursen and Visser, J. Syst. Softw. 2017 (doi 10.1016/j.jss.2016.04.008) | "We find that around one third of all releases introduce at least one breaking change." | a number alone does not hold the compatibility claim; the letter needs the contract list and a gate |
| Decan and Mens, IEEE TSE 2021 (doi 10.1109/TSE.2019.2918315) | compares "semver compliance of four software packaging ecosystems (Cargo, npm, Packagist and Rubygems)" and "how this compliance evolves over time" | compliance varies by ecosystem policy |
| Decan and Mens, "Lost in Zero Space", arXiv 2101.00836 (2021) | "the version number does not provide a good indication of the maturity of a package release." | the stage belongs in an explicit label, not in digit position |
| Ochoa, Degueule, Falleri and Vinju, EMSE 2022 (arXiv 2110.07889), cited in the critiques table | 83.4% of upgrades comply, 7.9% of clients affected | as above |
| Pinckney, Cassano, Guha and Bell, arXiv 2304.00394 (2023) | security patches flow to dependents in "90.09%" of cases when semver is used correctly, with misses "due to developers' imperfect use of both semver version constraints and semver version number increments" | the fix increment is what dependents act on; a mis-set fix number has measurable cost |
| Li, Wu, Fu and Zhou, arXiv 2309.02894 (2023) | "86.3% of library upgrades follow SemVer compliance and 28.6% of no-major upgrades introduce breaking changes" | as Raemaekers |
| Bogart, Kaestner, Herbsleb and Thung, ACM TOSEM 2021 (doi 10.1145/3447245) | "Ecosystems differ in their approaches to breaking changes" | the bump rule is a local policy and is written down (the contract list) |
| Lam, Dietrich and Pearce, arXiv 2008.07069 (2020) | "it is questionable whether a version number can adequately characterize version compatibility in general" | the letter claims one bit |
| Chen, Bi, Wang and Thongtanunam, arXiv 2605.24397 (2026), a review of 97 primary studies | names "the failure of semantic versioning as a trust mechanism" as an open challenge | as above |

No empirical study of calendar versioning or of hybrid identifiers surfaced. The arXiv query `all:"version numbering" OR all:"calendar versioning" OR all:"release naming"` returned no entries, and the Semantic Scholar search endpoint answered HTTP 429, so the claim is bounded by the arXiv search of the semantic-versioning literature (query `all:"semantic versioning"`, 40 newest entries read) and the citation chains above (I).

### Candidate grammars

All four share the Y14.35 letter alphabet (20 letters) and the stage vocabulary. ABNF (RFC 5234):

```abnf
rl      = %x41-48 / %x4A-4E / %x50 / %x52 / %x54-57 / %x59  ; A-H J-N P R T-W Y
letters = rl [ rl ]
fix1    = %x31-39 *DIGIT          ; 1, 2, ...  (zero is written by omission)
fix0    = "0" / fix1
stage   = "alpha" / "beta"
year    = 4DIGIT
month   = "0" %x31-39 / "1" %x30-32
date    = year month              ; YYYYMM, UTC month of SOURCE_DATE_EPOCH

g1core  = "Rev" letters [ "." fix1 ] [ "-" stage ]
g1      = g1core [ "+" date ]                       ; RevA.1-alpha+202612
g2      = "Rev" letters "." fix0 "." date [ "-" stage ]   ; RevA.1.202612
g3      = letters "." fix0 "." year "." month [ "-" stage ] ; A.1.2026.12
g4      = "Rev" letters "." year "." month [ "." fix1 ] [ "-" stage ]  ; RevA.2026.12.1
```

- G1: the date is trailing metadata, the form of SemVer build metadata (item 10), PEP 440 local labels and RPM caret.
- G2: three dotted fields, the owner's "RevA.Semver.datever" reading: letter, fix, date, with the date in precedence position.
- G3: G2 without the "Rev" prefix and with a CalVer-style dotted date (the owner's "A.1.2026.12"). It conflicts with the D91 spelling of the label, which carries "Rev".
- G4: Ubuntu-shaped. The date is the series month, fixed when the letter's first release is cut, and the fix is the trailing micro. An alpha build names the planned series month.

The harness generates each grammar's strings for one release history, checks them against the ABNF (as regular expressions, with mutated strings that must be rejected), and compares adjacent states. The 11 measured pairs, each expected ascending: A-alpha < A-beta; A-beta < A; A-beta < A with both in one month; A < A.1-alpha; A.1-alpha < A.1; A.1-alpha < A.1 with both in one month; A.1 < A.2; A.2 < B-alpha; B-alpha < B; Y.3 < AA (the letter boundary, 2031); and A.7 built in 2028-02 < B built in 2027-10 (a backport). The harness calibrates each algorithm before any verdict: known-good pairs from its own documentation or test suite (Debian Policy 5.6.12, the dpkg `t-version.c` parse and compare vectors, the SemVer item 11 chain, the PEP 440 epoch and local-version examples, `vercmptest.sh` from pacman, `rpmvercmp.at` from RPM, `sort -V` and git version-sort pairs) and a known-bad control that the algorithm must refute.

Raw strings, ascending pairs out of valid pairs (the 11 pairs; "inv" means no pair has two valid strings; RPM rejects a hyphen in a Version, so only the 3 pairs between unlabeled strings count):

| Grammar | dpkg | pacman | RPM | sort -V | git | git with versionsort.suffix | strcmp | PEP 440 | SemVer |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| G1 core, `RevA.1-alpha` | 5/11 | 5/11 | 2/3 | 5/11 | 5/11 | 10/11 | 5/11 | inv | inv |
| G1 full, `RevA.1-alpha+202612` | 10/11 | 9/11 | 2/3 | 5/11 | 5/11 | 10/11 | 5/11 | inv | inv |
| G2 | 8/11 | 8/11 | 2/3 | 8/11 | 8/11 | 10/11 | 8/11 | inv | inv |
| G3 | 8/11 | 8/11 | 2/3 | 8/11 | 8/11 | 10/11 | 8/11 | inv | inv |
| G4 | 5/11 | 5/11 | 2/3 | 5/11 | 5/11 | 10/11 | 5/11 | inv | inv |

Mapped strings (below), ascending pairs out of 11, in dpkg, pacman, RPM, PEP 440 and SemVer: 11/11 for every grammar.

Observations:

- The hyphen label inverts pre-release order in lexical tools. `sort -V`, git version sort (default) and strcmp place `RevA-alpha` after `RevA`, and dpkg splits the hyphen into a Debian revision, which also sorts after an absent revision. The git documentation names this case: "prerelease tags appearing after the main release (e.g. "1.0-rc1" after "1.0")" (https://git-scm.com/docs/git-config, `versionsort.suffix`), and `-c versionsort.suffix=-alpha -c versionsort.suffix=-beta` repairs it (10/11 in every grammar).
- G1 full under dpkg orders 10/11 because the trailing "+date" makes the upstream strings differ before the hyphen part is compared (`RevA` against `RevA+202701`). The core form shows the underlying behavior, 5/11 (I: a mechanism read from the port and the 5/11 result).
- The pair that fails in every raw form and every algorithm is Y.3 < AA, and `RevZ.3` against `RevAA` in a 26-letter alphabet fails the same way (dpkg, strcmp, pacman, RPM and `sort -V` each return 1): the first differing character decides. Mapped to an epoch or a major number, the letter index orders correctly.
- Pacman orders G1 full 9/11 with one defect besides the letter boundary: `RevA+202701` against `RevA.1-alpha+202702` compares the date digits against the fix digit (202701 > 1) because the fix is omitted. The mapped form writes the fix. RPM's row covers only the three unlabeled pairs; it orders A.1 < A.2 and the backport pair and fails the letter boundary.
- G2 and G3 order 8/11: they fail the two same-month pairs, where the date does not separate a pre-release from its release, and the letter boundary. Their ordering rests on the date being unique and increasing, and their string changes with every month, which the `uname -r` consumers above do not tolerate.
- G4 behaves like G1 core in the plain lexical tools (5/11) and, in addition, names a month in the pre-release string that the release may miss: a slipped release renames its own alpha strings.
- Same fix, later respin (build month 2027-03 against 2027-04): G1 full, G2 and G3 order the respin later in every valid algorithm; G1 core and G4 compare equal (0). Neither returns the reversed order.

Legality, raw strings (T accepted, F rejected, w accepted with a warning), identical for all five grammars:

| Strings | git tag `gizmobsd-v...` | file name `gizmobsd-....uf2` | dpkg | pacman pkgver | RPM Version | PEP 440 | SemVer |
| --- | --- | --- | --- | --- | --- | --- | --- |
| unlabeled (`RevA`, `RevA.1`, `RevB`, `RevAA`) | T | T | w | T | T | F | F |
| labeled (`-alpha`, `-beta`) | T | T | w | F | F | F | F |

dpkg's own test suite marks an upstream version that does not start with a digit as a parse failure with a warning (`t-version.c`: "Test upstream version not starting with a digit", checked by `test_warn`); the letters-first strings therefore draw a parse result that the test suite classifies as a warning. PKGBUILD(5) states of `pkgver`: "The variable is not allowed to contain colons, forward slashes, hyphens or whitespace." PEP 440 and SemVer require numeric fields. Every mapped string below passes its own ecosystem's field rules in the harness (`mapped strings rejected by their own ecosystem's rules: none`).

Mapping, G1 (letter index becomes the epoch, or the SemVer major; stage becomes a tilde or an alpha/beta marker; the date becomes ignored or trailing metadata), shown for `RevB.3-beta+202712`:

| Ecosystem | Mapped string | Mechanism |
| --- | --- | --- |
| dpkg | `2:3~beta+202712` | epoch 2; tilde sorts below the end of the string |
| RPM | epoch 2, Version `3~beta^202712` | tilde below, caret above the base and below the next release segment (`rpmvercmp.at` lines for `1.0^20160101`) |
| pacman | `2:3beta+202712` | a letter suffix sorts below the bare number (vercmp(8): "1.0a < 1.0b < 1.0beta < 1.0p < 1.0pre < 1.0rc < 1.0") |
| PEP 440 | `2!3b0+202712` | epoch 2, beta, local label |
| SemVer | `2.3.0-beta+202712` | letter index as major, fix as minor; build metadata ignored |

git describe, measured in a scratch repository: tag `gizmobsd-vRevA.1-alpha` followed by one commit prints `gizmobsd-vRevA.1-alpha-1-g9a8ecb5`, and the pattern `^(.+)-([0-9]+)-g([0-9a-f]+)$` recovers the tag, the count and the hash.

Size (Appendix A.3, `arm-none-eabi-gcc 16.2.0`, `vers.o` `.rodata`; `.data` and `.bss` are 0 in every row; flash only):

| OSR (and display value, where split) | .rodata bytes | Delta from DiscoBSD 2.7 (79) |
| --- | --- | --- |
| `RevA-alpha` in `version[]` and `osrelease[]` (D91) | 93 | +14 |
| `RevA-alpha+202611` in both | 107 | +28 |
| `RevA-alpha+202611` in `version[]`, `RevA-alpha` in `osrelease[]` (needs the display value of A.2) | 100 | +21 |
| `RevA.1` in both | 85 | +6 |
| `RevA.1+202703` in both | 99 | +20 |
| G2 `RevA.0.202611-alpha` in both | 111 | +32 |
| G3 `A.0.2026.11-alpha` in both | 107 | +28 |
| G4 `RevA.2027.01-alpha` in both | 109 | +30 |
| `RevAA.12-beta+202612` in both (longest form) | 113 | +34 |

Kernel RAM is unchanged in every row (the strings are `const char`). The linked-kernel text delta was not measured.

### KERN_OSREV

The encoding packs the three ordering fields into decimal digits: `KERN_OSREV = L * 1000000 + F * 10 + S` with S = 1 for alpha, 2 for beta and 9 for final (gaps leave room for another stage). Decoding is `L = E / 1000000`, `F = (E % 1000000) / 10`, `S = E % 10`. The chain measured:

| String | KERN_OSREV |
| --- | --- |
| `RevA-alpha` | 1000001 |
| `RevA-beta` | 1000002 |
| `RevA` | 1000009 |
| `RevA.1-alpha` | 1000011 |
| `RevA.1` | 1000019 |
| `RevA.2` | 1000029 |
| `RevB-alpha` | 2000001 |
| `RevB` | 2000009 |

The harness orders 11 of 11 pairs with it (year-and-month: 8 of 11). The first value, 1000001, exceeds the inherited 202608, so a comparison against the old value stays monotone across the rename. Overflow: `long` is signed 32 bits on this target, maximum 2147483647. With F up to 99999 and S up to 9 the low six digits stay below 1000000, so the limit is the letter index: 2146 at most (2146999999 fits, 2147999999 does not). Single and double letters of the 20-letter alphabet reach index 420 (420999999), so overflow needs three-letter revisions, which ASME Y14.35M's two-character limit excludes. The integer never needs the date: the build month is already in `version[]` and in the file name.

D93 reads "Year-month probably but really you are working on an interesting string". The measured reason against year-month: the integer orders by build date, and build date breaks under maintenance and cannot separate two fixes in one month. The year-and-month value of the BSD lineage (`BSD 199506` in the NetBSD and FreeBSD sources quoted above, `OpenBSD 202610`) remains the build month in the string.

### Recommendation: G1

The grammar is G1, `RevA.1-alpha+202612`: the compatibility letter, the optional fix, the optional stage, and a trailing build month that precedence ignores. Three examples: `RevA-alpha+202611` (the first tree state), `RevA.1+202703` (a fix release) and `RevB.2-beta+202708` (a beta of the second fix of the next revision). The recommendation rests on:

1. Information content: only (L, F, P) is a key; G1 is the grammar that keeps the date out of precedence. G2 and G3 put the date where lexical tools and humans read order; G4 puts a future month into pre-release strings.
2. Stability of the exact-match consumers: the core form is the same string for every build of one release, so `osrelease`, tags and the gates need no monthly edit.
3. Mapping: the one grammar whose three parts map one-to-one onto the epoch, the release segment and the local or build label that dpkg, RPM, PEP 440 and SemVer define for that role.
4. Cost: +14 bytes of flash with the core in both strings (D91), no change to `newvers.sh`.

Surfaces:

| Surface | Form | Example, first tree state |
| --- | --- | --- |
| `osrelease[]`, `uname -r` (D88, D91) | core | `RevA-alpha` |
| `version[]` | core, then the existing build counter, commit count and full date | `GizmoBSD RevA-alpha (PICO) #1 3000: <date>` |
| `KERN_OSREV` | packed integer | 1000001 |
| Git tag (D90) | `gizmobsd-v` plus the core | `gizmobsd-vRevA-alpha` |
| `/etc/release` (`git describe --match 'gizmobsd-v*'`) | tag, count, hash | `gizmobsd-vRevA-alpha-4-g<hash>` |
| File names, release notes, citations | full | `RevA-alpha+202611` |

After the D89 condition (64-bit `time_t` landed) the first unlabeled release is `RevA` (1000009). The trunk then moves to `RevA.1-alpha` (1000011), the cut candidate is `RevA.1-beta`, and the release is `RevA.1` (1000019), the inherited cycle of `newvers.sh:18-20` with the next-version alpha step of S1. A change to the contract list moves the letter: `RevB-alpha` (2000001). The display form of the owner's example, "Rev A 2026.12", is the same fact set in prose: `RevA+202612`.

Rules:

- The fix is the number of the release within its letter; it resets to zero (omitted) at a letter change; a finished release takes the next fix when anything it ships changes.
- The date is the UTC month of `SOURCE_DATE_EPOCH` (the committer date of HEAD when unset), so the string is a function of the commit.
- The host tools' check reads `^Rev([A-HJ-NPRT-WY]{1,2})(\.[1-9][0-9]*)?(-(alpha|beta))?$` from `uname -r` and requires an equal letter and a fix at or above its minimum; the date never takes part.
- The letter alphabet follows ASME Y14.35M with continuation AA, AB, ... after Y. Lexical tools misorder the continuation (measured); the packed integer and the package mappings order it.

Falsifiers, each stated as an observation that refutes a part of the recommendation:

1. More than one release of the first six publishes under one (L, F, final) with different content. Then the fix is not the identity of a release, and the date or a build counter has to enter the key.
2. Two independent classifications of each of the first six release diffs against the contract list disagree on more than one release (the S1 falsifier 2 applied to the letter). Then the letter claims more than the list supports.
3. A host-tool compatibility check needs to test a feature that arrives between two fix numbers. Then (L, F) is too coarse and the integer gains a field.
4. A release workflow or packaging step orders raw core strings lexically over a directory that holds alpha or beta strings of one revision (5/11 measured), without `versionsort.suffix` or the mapping. Then the output order is wrong, and the step moves to the integer or the mapping.

Not run: the linked-kernel size, any Renode stage, dpkg itself (the host has no dpkg binary; the port is calibrated against the Policy examples and the `t-version.c` vectors), RPM's Python binding (the `rpm --eval` Lua call is used), and PEP 440 under any release of `packaging` other than 26.3.

### Retrieval caveats for this section

- OpenBSD's FAQ page refused the TLS handshake with curl; the OpenBSD facts come from the GitHub mirror of `sys/sys/param.h` (revision line `1.147 2026/07/16`). The NetBSD facts come from the GitHub mirror of NetBSD's tree. Mirror identity with the projects' own repositories was not checked.
- The ASME Y14.35 standard is paywalled. The letter rules are quoted from a 2008 conference presentation and a 2014 draft; the 2008 presentation bounds revision letters to two characters and the 2014 draft to three, so the current edition decides the limit (not checked).
- The Fedora packaging guidelines page returned an anti-bot challenge and was not read; RPM behavior comes from `rpmvercmp.cc`, `rpmvercmp.at` and the installed rpm 6.0.1.
- illumos `uname -v` and `OmniOS` release strings were not retrieved; Solaris coverage is the IPS version format.
- The GNU coreutils page on version sort ordering returned 404; `sort -V` behavior is the measured behavior of GNU coreutils 9.12, calibrated on pairs.
- The empirical studies were read as abstracts only; the figures quoted are the abstracts' figures.
- The Semantic Scholar search endpoint rate-limited the survey query (HTTP 429).

## Recommendation of the first analysis: S1 (not adopted by D87)

This section and its falsifiers stand as recorded; the owner chose S3.

S1 states the one claim the tree can verify (the contract list changed), keeps SemVer's honest initial-development band while the ABI is still moving, orders unlabeled release strings identically in every package-version algorithm measured (digits and dots only) and labeled strings alpha < beta < release under SemVer and PEP 440, and costs 4 bytes of flash and 0 of RAM. It carries a written exit criterion so the ZeroVer critique has a measurable answer. S2 is the fallback if the release process becomes calendar-driven. S3 serves hardware-style labels (a PicoCalc board revision) and not the OS series.

Falsifiers, each a count over the first six tagged OS releases:

1. y increments in five or more of the six releases. Then y counts releases, the number carries no more than S2's date, and S2 replaces S1 with the contract list becoming its ABI epoch.
2. Two independent classifications of each release diff against the contract list (the maintainer and one reviewer) disagree on more than one release. Then the list is under-specified and is rewritten before the next tag; disagreement on two further releases moves the series to S3's single-bit rule.
3. A release touching the contract list ships without a y increment. Then the rule is not enforced and a gate comparing the list's files against the bump becomes a prerequisite of the next tag.

Not run: kernel build and linked-kernel text size (remote build host), Renode stages that match the banner, the packaging of a gizmobsd-host 2.0.0 artifact, a dpkg binary comparison, retrieval of the OSDev Wiki and OpenBSD primary pages, and the RetroBSD release numbering.

## Owner decisions

### Questions of the first analysis and their answers

1. Scheme and label placement: answered by D87 (S3) and D88 and D91 (label in both strings, `RevA-alpha`).
2. The contract list as the rule for advancing the letter, and its criterion for the first unlabeled release: the criterion is D89 (64-bit `time_t` landed); the list text is the S1 list and awaits approval (residual decision R7).
3. A separate OS tag namespace and the `--match` change at `Makefile:2236`: answered by D90.
4. Banner literals in gates and fixtures reading `newvers.sh`: answered by D94.
5. The `KERN_OSREV` value: provisionally year-and-month (D93); residual decision R1 below carries the measured comparison.

### Residual decisions

Each decision lists its options with the reason for each.

**R1. KERN_OSREV encoding.**
- (a) Packed integer `letter * 1000000 + fix * 10 + stage` (recommended). Orders 11 of 11 measured pairs, separates two fixes of one month, exceeds the inherited 202608; costs a documented decode and drops the BSD-lineage year-and-month meaning from the sysctl.
- (b) Year-and-month `YYYYMM` (D93 provisional). Keeps the lineage value (`BSD 199506`, `OpenBSD 202610`) and the plan's D4; orders 8 of 11 measured pairs, fails the backport and cannot tell fixes of one month apart, and carries no letter.
- (c) Packed integer in `KERN_OSREV` and a year-and-month macro retained in `param.h` for source compatibility. Costs no runtime bytes; adds a second constant that no program reads today (I).

**R2. Date in `osrelease`.**
- (a) Core only (recommended): the string is identical for every build of one release, so exact-match gates and tags need no monthly edit; +14 bytes over `2.7`.
- (b) Full composite: shows the month in `uname -r` without reading `uname -v`; +28 bytes, and every consumer of the string changes monthly.
- (c) Core in `osrelease` and full in `version[]`: +21 bytes and a display-value change to `newvers.sh` (A.3); repeats the month that `version[]` already prints.

**R3. Date spelling in the full form.**
- (a) `+YYYYMM` (recommended): six digits, the shape of the BSD year-and-month integer, and no dot inside the file name.
- (b) `+YYYY.MM`: the owner's prose spelling ("2026.12"); one more byte and a dot that file-name tools may read as an extension separator.

**R4. Tag spelling.**
- (a) `gizmobsd-v` plus the core verbatim (`gizmobsd-vRevA.1`, recommended): one spelling of a release in the string, the integer and the tag.
- (b) `gizmobsd-v` plus the core without "Rev" (`gizmobsd-vA.1`): shorter and closer to the `v1.2` convention; a second spelling of the same release.

**R5. Letter alphabet.**
- (a) The 20 letters of ASME Y14.35 with continuation AA after Y (recommended): the hardware convention that the name "Rev" invokes; the continuation misorders in lexical tools and orders in the integer.
- (b) A to Z with continuation AA after Z: 26 steps before the continuation; the letters I, O, Q, S, X and Z are the ones the hardware convention drops for confusion with digits.
- (c) A to Y and no continuation: lexical order stays correct; the 21st incompatible break requires a new scheme decision.

**R6. First release spelling and the zero fix.**
- (a) `RevA` is fix 0 and the first fix is `RevA.1` (recommended): the first unlabeled release matches D89's "Rev A"; the zero is omitted.
- (b) The first release is `RevA.1` and `RevA` appears only as the pre-release series (`RevA-alpha`): every release carries a number; D89's "unlabeled Rev A" then reads as `RevA.1`.

**R7. Approve the contract list as the rule that advances the letter.**
- (a) Approve the S1 list (recommended): the letter claims one bit and the list states which changes set it; the first-six-release falsifier checks it.
- (b) Amend the list before approval: choose which items move the letter (for instance, whether a libc symbol removal does) before the first tag.

## Appendix A: replay of the size measurement

### A.1: the S1, S2 and baseline rows

```sh
# Run from the repository root; arm-none-eabi-gcc 16.2.0 produced the figures above.
set -eu
ROOT=$(git rev-parse --show-toplevel)
SP=$(mktemp -d); mkdir -p "$SP/bin"
printf '#!/bin/sh\nprintf "2999\\n"\n' > "$SP/bin/git"; chmod +x "$SP/bin/git"
run() { # name ost osr label
  d=$SP/$1/PICO; mkdir -p "$d"; cd "$d"
  sed -e "s/^OST=.*/OST=\"$2\"/" -e "s/^OSR=.*/OSR=\"$3\"/" -e "s/^S=\"\"/S=\"$4\"/" \
      "$ROOT/sys/conf/newvers.sh" > newvers.sh
  PATH=$SP/bin:$PATH SOURCE_DATE_EPOCH=1794700000 sh newvers.sh > vers.c
  arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=c17 -ffreestanding -c vers.c -o vers.o
  printf '%-14s ' "$1"
  arm-none-eabi-size -A vers.o | awk '/^\.rodata|^\.data|^\.bss/ {printf "%s=%s ", $1, $2} END {print ""}'
}
run base DiscoBSD 2.7 ""
run s1 GizmoBSD 0.1.0 ""
run s1-S-alpha GizmoBSD 0.1.0 "-alpha"
run s1-OSR-alpha GizmoBSD 0.1.0-alpha ""
run s2 GizmoBSD 2026.11 ""
run s3 GizmoBSD RevA ""
run zero-a GizmoBSD 0.a ""
run rev-space GizmoBSD "Rev A" ""
cat "$SP/rev-space/PICO/vers.c"
```

### A.2: replay of the S3 representation measurement

The `Rev A` display with `RevA` in `osrelease` needs two values, so `newvers.sh` gains a display value (`OSD`, empty by default and falling back to `OSR`). The variant that keeps `Rev A` in both strings splits the awk input on `|` instead of whitespace. Both are generated by a small awk script that matches whole lines by position, so the replay depends on no `sed` dialect; they are scratch copies and no tree file changes. Run from the repository root with the `git` stub of A.1 on `PATH`.

```sh
set -eu
ROOT=$(git rev-parse --show-toplevel)
SP=$(mktemp -d); mkdir -p "$SP/bin"
printf '#!/bin/sh\nprintf "2999\\n"\n' > "$SP/bin/git"; chmod +x "$SP/bin/git"
cat > "$SP/mk.awk" <<'EOF'
# mode=split: a display value OSD that version[] prints; mode=delim: fields
# split on "|" so a space inside OSR survives.
BEGIN { dq = "\"" }
mode == "split" && index($0, "OSR=") == 1 { print; print "OSD=" dq dq; next }
mode == "delim" && index($0, "echo $GITREV $CV") == 1 {
    print "echo " dq "$GITREV|$CV|$OST|$OSR|$ID|$S" dq " | \\"; next
}
index($0, "awk -v date=") == 1 {
    if (mode == "split")
        print "awk -v date=" dq "$NOW" dq " -v osd=" dq "$OSD" dq " '{"
    else
        print "awk -F'|' -v date=" dq "$NOW" dq " '{"
    next
}
mode == "split" && index($0, "printf " dq "const char version") {
    print "    if (osd == " dq dq ") osd = osr;"
}
mode == "split" && index($0, "ost, osr, status") { sub("osr", "osd") }
{ print }
EOF
awk -v mode=split -f "$SP/mk.awk" "$ROOT/sys/conf/newvers.sh" > "$SP/newvers-split.sh"
awk -v mode=delim -f "$SP/mk.awk" "$ROOT/sys/conf/newvers.sh" > "$SP/newvers-delim.sh"
run() { # name ost osr osd script
  d=$SP/$1/PICO; mkdir -p "$d"; cd "$d"
  sed -e "s/^OST=.*/OST=\"$2\"/" -e "s/^OSR=.*/OSR=\"$3\"/" -e "s/^OSD=.*/OSD=\"$4\"/" "$5" > newvers.sh
  PATH=$SP/bin:$PATH SOURCE_DATE_EPOCH=1794700000 sh newvers.sh > vers.c
  arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=c17 -ffreestanding -c vers.c -o vers.o
  printf '%-12s ' "$1"
  arm-none-eabi-size -A vers.o | awk '/^\.rodata/ {print $1 "=" $2}'
  grep 'version\[\]\|osrelease' vers.c | sed -n '1p;3p'
}
run base DiscoBSD 2.7 "" "$ROOT/sys/conf/newvers.sh"
run s3-reva GizmoBSD RevA "" "$ROOT/sys/conf/newvers.sh"
run s3-split GizmoBSD RevA "Rev A" "$SP/newvers-split.sh"
run s3-both GizmoBSD "Rev A" "" "$SP/newvers-delim.sh"
```

### A.3: replay of the composite-identifier size measurement

The rows of the composite section. The display value of A.2 is generated by a small awk script that matches whole lines by position; it is a scratch copy and no tree file changes. Run from the repository root.

```sh
# Run from the repository root; arm-none-eabi-gcc 16.2.0 produced the figures above.
set -eu
ROOT=$(git rev-parse --show-toplevel)
SP=$(mktemp -d); mkdir -p "$SP/bin"
printf '#!/bin/sh\nprintf "2999\\n"\n' > "$SP/bin/git"; chmod +x "$SP/bin/git"
cat > "$SP/split.awk" <<'EOF'
# A display value OSD that version[] prints; osrelease[] keeps OSR.
BEGIN { dq = "\"" }
index($0, "OSR=") == 1 { print; print "OSD=" dq dq; next }
index($0, "awk -v date=") == 1 {
    print "awk -v date=" dq "$NOW" dq " -v osd=" dq "$OSD" dq " '{"; next
}
index($0, "printf " dq "const char version") { print "    if (osd == " dq dq ") osd = osr;" }
index($0, "ost, osr, status") { sub("osr", "osd") }
{ print }
EOF
awk -f "$SP/split.awk" "$ROOT/sys/conf/newvers.sh" > "$SP/newvers-split.sh"
run() { # name osr osd script
  d=$SP/$1/PICO; mkdir -p "$d"; cd "$d"
  sed -e 's/^OST=.*/OST="GizmoBSD"/' -e "s/^OSR=.*/OSR=\"$2\"/" -e "s/^OSD=.*/OSD=\"$3\"/" "$4" > newvers.sh
  PATH=$SP/bin:$PATH SOURCE_DATE_EPOCH=1794700000 sh newvers.sh > vers.c
  arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=c17 -ffreestanding -c vers.c -o vers.o
  printf '%-18s ' "$1"
  arm-none-eabi-size -A vers.o | awk '/^\.rodata/ {printf "%s=%s ", $1, $2} /^\.(data|bss)/ {printf "%s=%s ", $1, $2} END {print ""}'
}
S=$ROOT/sys/conf/newvers.sh
run base-2.7 2.7 "" "$S"
run core RevA-alpha "" "$S"
run full 'RevA-alpha+202611' "" "$S"
run split RevA-alpha 'RevA-alpha+202611' "$SP/newvers-split.sh"
run fix1-core RevA.1 "" "$S"
run fix1-full 'RevA.1+202703' "" "$S"
run g2 RevA.0.202611-alpha "" "$S"
run g3 A.0.2026.11-alpha "" "$S"
run g4 RevA.2027.01-alpha "" "$S"
run double-full 'RevAA.12-beta+202612' "" "$S"
cat "$SP/split/PICO/vers.c"
```

## Appendix B: replay of the ordering measurements

Tools: python 3.14.9 with packaging 26.3 (PEP 440), pacman 7.1.0 `vercmp`, rpm 6.0.1 (`rpm --eval` with the Lua `rpm.vercmp`), GNU coreutils 9.12 `sort -V`, git 2.56.0 (`git tag --sort=version:refname`, `git check-ref-format`, `git describe`), and Python ports of dpkg's `verrevcmp` and of SemVer 2.0.0 item 11. The dpkg port splits the epoch at the first colon and the revision at the last hyphen, as dpkg's `t-version.c` parse vectors require. Each algorithm is calibrated before a verdict: known-good pairs from its documentation or test suite and a known-bad control that must be refuted (a calibration failure prints and exits non-zero). The block runs as `python3 -I` from any directory, extracted from this note; absent tools (packaging, vercmp, rpm, sort, git) are reported and their rows are not run. The first-analysis pairs (0.a, the pre-release labels) are rows of the same run, so the pacman, RPM and `sort -V` columns are scripted.

```python
# python3 -I; the optional tools (packaging, vercmp, rpm, sort, git) are reported when absent.
import os
import re
import shutil
import subprocess
import sys
import tempfile

LETTERS = "ABCDEFGHJKLMNPRTUVWY"  # ASME Y14.35 omits I, O, Q, S, X, Z
STAGE = {"alpha": 1, "beta": 2, None: 9}  # KERN_OSREV stage digit
FAILED = []


def run(argv, text=None, env=None, cwd=None):
    try:
        p = subprocess.run(argv, input=text, capture_output=True, text=True, env=env, cwd=cwd,
                           timeout=60, check=False)
    except (OSError, subprocess.SubprocessError):
        return None
    return p


def have(tool):
    return shutil.which(tool) is not None


# ---- algorithms: each cmp(a, b) returns -1, 0, 1, or None when a string is invalid ----------
def order(c):  # dpkg lib/dpkg/version.c order()
    if c == "~":
        return -1
    if c in "0123456789":
        return 0
    if c.isalpha():
        return ord(c)
    return ord(c) + 256


def verrevcmp(a, b):  # port of dpkg verrevcmp
    i = j = 0
    while i < len(a) or j < len(b):
        first = 0
        while (i < len(a) and not a[i].isdigit()) or (j < len(b) and not b[j].isdigit()):
            ac = order(a[i]) if i < len(a) else 0
            bc = order(b[j]) if j < len(b) else 0
            if ac != bc:
                return ac - bc
            i += 1
            j += 1
        while i < len(a) and a[i] == "0":
            i += 1
        while j < len(b) and b[j] == "0":
            j += 1
        while i < len(a) and a[i].isdigit() and j < len(b) and b[j].isdigit():
            if not first:
                first = ord(a[i]) - ord(b[j])
            i += 1
            j += 1
        if i < len(a) and a[i].isdigit():
            return 1
        if j < len(b) and b[j].isdigit():
            return -1
        if first:
            return first
    return 0


def dpkg_split(s):  # epoch before the first colon, revision after the last hyphen
    epoch = 0
    if ":" in s:
        e, s = s.split(":", 1)
        if not e.isdigit():
            return None
        epoch = int(e)
    up, rev = s.rsplit("-", 1) if "-" in s else (s, "")
    return epoch, up, rev


def dpkg_status(s):  # "ok", "warn" (no leading digit: dpkg parseversion warns) or "bad"
    p = dpkg_split(s)
    if p is None or not re.fullmatch(r"[A-Za-z0-9.+~:-]*", p[1] + p[2]):
        return "bad"
    return "ok" if p[1][:1].isdigit() else "warn"


def sign(x):
    return (x > 0) - (x < 0)


def dpkg_cmp(a, b):
    if dpkg_status(a) == "bad" or dpkg_status(b) == "bad":
        return None
    (ea, ua, ra), (eb, ub, rb) = dpkg_split(a), dpkg_split(b)
    if ea != eb:
        return sign(ea - eb)
    return sign(verrevcmp(ua, ub)) or sign(verrevcmp(ra, rb))


SEMVER = re.compile(r"(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?")


def semver_key(s):  # SemVer 2.0.0 item 11 (build metadata ignored); None when not SemVer
    m = SEMVER.fullmatch(s)
    if not m:
        return None
    pre = m.group(4)
    ids = None if pre is None else [(0, int(i), "") if i.isdigit() else (1, 0, i)
                                    for i in pre.split(".")]
    return tuple(int(x) for x in m.groups()[:3]), ids


def semver_cmp(a, b):
    ka, kb = semver_key(a), semver_key(b)
    if ka is None or kb is None:
        return None
    (ca, ia), (cb, ib) = ka, kb
    if ca != cb:
        return -1 if ca < cb else 1
    if ia is None or ib is None:
        return 0 if ia == ib else (1 if ia is None else -1)
    if ia == ib:
        return 0
    for x, y in zip(ia, ib):
        if x != y:
            return -1 if x < y else 1
    return -1 if len(ia) < len(ib) else 1


try:
    from packaging.version import InvalidVersion, Version
except ImportError:
    Version = None


def pep_cmp(a, b):
    try:
        va, vb = Version(a), Version(b)
    except InvalidVersion:
        return None
    return (va > vb) - (va < vb)


def pacman_cmp(a, b):
    p = run(["vercmp", a, b])
    return int(p.stdout) if p is not None and p.returncode == 0 else None


def rpm_one(a, b):
    if not re.fullmatch(r"[A-Za-z0-9.+~^_]*", a + b):
        return None  # a hyphen is illegal in an RPM Version; the rest is spliced into Lua
    lua = '%{lua: print(rpm.vercmp("' + a + '", "' + b + '"))}'
    p = run(["rpm", "--eval", lua])
    return int(p.stdout) if p is not None and p.returncode == 0 else None


def split_epoch(s):
    m = re.fullmatch(r"(?:(\d+):)?(.*)", s)
    return int(m.group(1) or 0), m.group(2)


def rpm_cmp(a, b):  # epoch compared as an integer, then rpmvercmp on the version
    (ea, va), (eb, vb) = split_epoch(a), split_epoch(b)
    return sign(ea - eb) if ea != eb else rpm_one(va, vb)


def sortv_cmp(a, b):  # GNU sort -V, stable, C locale; a tie keeps the input order
    env = dict(os.environ, LC_ALL="C")
    p1 = run(["sort", "-V", "-s"], text=a + "\n" + b + "\n", env=env)
    p2 = run(["sort", "-V", "-s"], text=b + "\n" + a + "\n", env=env)
    if p1 is None or p2 is None:
        return None
    f1, f2 = p1.stdout.split("\n")[0], p2.stdout.split("\n")[0]
    if f1 == a and f2 == a and a != b:
        return -1
    if f1 == b and f2 == b and a != b:
        return 1
    return 0


def ascii_cmp(a, b):
    return (a > b) - (a < b)


GIT_ENV = dict(os.environ, GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null",
               GIT_AUTHOR_NAME="x", GIT_AUTHOR_EMAIL="x@example.invalid",
               GIT_COMMITTER_NAME="x", GIT_COMMITTER_EMAIL="x@example.invalid")


class GitSort:
    """git tag --sort=version:refname over a set of tag names; ties are broken by refname."""

    def __init__(self, names, suffixes=()):
        self.rank = {}
        self.tmp = tempfile.TemporaryDirectory()
        d = self.tmp.name
        run(["git", "init", "-q", d], env=GIT_ENV)
        tree = run(["git", "-C", d, "mktree"], text="", env=GIT_ENV).stdout.strip()
        commit = run(["git", "-C", d, "commit-tree", tree, "-m", "m"], env=GIT_ENV).stdout.strip()
        ok = []
        for n in names:
            if run(["git", "check-ref-format", "refs/tags/" + n]).returncode == 0:
                run(["git", "-C", d, "update-ref", "refs/tags/" + n, commit], env=GIT_ENV)
                ok.append(n)
        cfg = [x for s in suffixes for x in ("-c", "versionsort.suffix=" + s)]
        out = run(["git", "-C", d] + cfg + ["tag", "-l", "--sort=version:refname"], env=GIT_ENV)
        for i, n in enumerate(out.stdout.split()):
            self.rank[n] = i

    def cmp(self, a, b):
        if a not in self.rank or b not in self.rank:
            return None
        return (self.rank[a] > self.rank[b]) - (self.rank[a] < self.rank[b])


# ---- calibration: known-good pairs from each algorithm's own documentation or test suite ------
def calibrate(name, fn, good, bad):
    for a, b, want in good:
        got = fn(a, b)
        if got != want:
            FAILED.append("%s: %s vs %s gave %r, want %r" % (name, a, b, got, want))
    for a, b, wrong in bad:  # a deliberately wrong expectation must be refuted
        if fn(a, b) == wrong:
            FAILED.append("%s: control %s vs %s was not refuted" % (name, a, b))


def calibrate_all(git_sort):
    # Debian Policy 5.6.12 examples; dpkg lib/dpkg/t/t-version.c parse and compare vectors
    dpkg_pairs = [("1.0~beta1~svn1245", "1.0~beta1"), ("1.0~beta1", "1.0"), ("1.0", "1.0a"),
                  ("~~", "~~a"), ("~~a", "~"), ("~", ""), ("", "a"), ("0:0", "1:0"), ("a", "b"),
                  ("0:0-a", "0:0-b")]
    calibrate("dpkg", dpkg_cmp, [(a, b, -1) for a, b in dpkg_pairs] + [("0:0", "0:0", 0)],
              [("1.0a", "1.0", -1), ("1:0", "0:0", -1)])
    for text, want in [("0:0-0-0", (0, "0-0", "0")), ("0:0:0-0", (0, "0:0", "0")),
                       ("5:1", (5, "1", "")), ("0:0-0", (0, "0", "0"))]:
        if dpkg_split(text) != want:
            FAILED.append("dpkg split %s gave %r" % (text, dpkg_split(text)))
    if dpkg_status("abc3-0") != "warn" or dpkg_status("0a_b") != "bad":
        FAILED.append("dpkg status control")
    chain = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
             "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"]  # SemVer item 11 example order
    calibrate("semver", semver_cmp, [(a, b, -1) for a, b in zip(chain, chain[1:])],
              [("1.0.0", "1.0.0-alpha", -1), ("1.0.0+a", "1.0.0+b", 1)])
    if semver_cmp("0.a", "0.1") is not None or semver_cmp("1.0.0+a", "1.0.0+b") != 0:
        FAILED.append("semver validity control")
    if Version is not None:  # PEP 440 epoch and local version examples
        calibrate("pep440", pep_cmp,
                  [("1.0a1", "1.0", -1), ("2014.04", "1!1.0", -1), ("1!1.0", "1!1.1", -1),
                   ("1.0", "1.0+local", -1), ("1.0.dev1", "1.0a1", -1)],
                  [("1.0", "1.0a1", -1), ("1!1.0", "2014.04", -1)])
    if have("vercmp"):  # pacman test/util/vercmptest.sh pairs
        calibrate("pacman", pacman_cmp,
                  [("1.5.0", "1.5.1", -1), ("1.5b", "1.5", -1), ("1.5", "1.5-1", 0),
                   ("0:1.0", "0:1.1", -1), ("0:1.0", "1:1.0", -1), ("1.5.0-1", "1.5.0-2", -1)],
                  [("1.5.1", "1.5.0", -1), ("1:1.0", "0:1.0", -1)])
    if have("rpm"):  # rpm tests/rpmvercmp.at pairs
        calibrate("rpm", rpm_cmp,
                  [("1.0", "2.0", -1), ("2.0.1", "2.0.1a", -1), ("1.0~rc1", "1.0", -1),
                   ("1.0~rc1", "1.0~rc2", -1), ("1.0", "1.0^git1", -1), ("1.0^git1", "1.01", -1),
                   ("1.0^20160101", "1.0.1", -1), ("0:1.0", "1:1.0", -1)],
                  [("1.0", "1.0~rc1", -1), ("1.0.1", "1.0^20160101", -1)])
    if have("sort"):
        calibrate("sort-V", sortv_cmp,
                  [("1.9", "1.10", -1), ("1.0~rc1", "1.0", -1), ("1.0", "1.0", 0)],
                  [("1.10", "1.9", -1), ("1.0", "1.0~rc1", -1)])
    calibrate("ascii", ascii_cmp, [("RevA", "RevB", -1), ("RevA", "RevA-alpha", -1)],
              [("RevB", "RevA", -1)])
    if git_sort is not None:
        calibrate("git", git_sort.cmp, [("gcal-v1.9", "gcal-v1.10", -1)],
                  [("gcal-v1.10", "gcal-v1.9", -1)])


# ---- the composite identifier: fields, grammars, mappings ------------------------------------
def lname(n):  # letter index (1 = A) to revision letters: A..Y, then AA, AB, ..., AY, BA, ...
    if n <= len(LETTERS):
        return LETTERS[n - 1]
    n -= len(LETTERS) + 1
    return LETTERS[n // len(LETTERS)] + LETTERS[n % len(LETTERS)]


def lindex(s):  # inverse of lname
    idx = [LETTERS.index(ch) for ch in s]
    return idx[0] + 1 if len(idx) == 1 else len(LETTERS) * (idx[0] + 1) + idx[1] + 1


def ym(d):
    return "%d%02d" % d


def rel(letter, fix, stage, built, series):
    return {"l": letter, "f": fix, "p": stage, "d": built, "s": series}


def pre(r, sep="-"):
    return sep + r["p"] if r["p"] else ""


# Each grammar: raw(r) is the written form; core(r) the numeric fields a package mapping carries
# in precedence position; meta(r) the part a mapping carries as ignored or trailing metadata;
# semver(r) the (major, minor, patch) triple.
GRAMMARS = {
    "G1c RevA.1-alpha": dict(
        raw=lambda r: "Rev%s%s%s" % (lname(r["l"]), "." + str(r["f"]) if r["f"] else "", pre(r)),
        core=lambda r: [r["f"]], meta=lambda r: "",
        semver=lambda r: (r["l"], r["f"], 0)),
    "G1 RevA.1-alpha+202612": dict(
        raw=lambda r: "Rev%s%s%s+%s" % (lname(r["l"]), "." + str(r["f"]) if r["f"] else "",
                                         pre(r), ym(r["d"])),
        core=lambda r: [r["f"]], meta=lambda r: ym(r["d"]),
        semver=lambda r: (r["l"], r["f"], 0)),
    "G2 RevA.1.202612": dict(
        raw=lambda r: "Rev%s.%d.%s%s" % (lname(r["l"]), r["f"], ym(r["d"]), pre(r)),
        core=lambda r: [r["f"], int(ym(r["d"]))], meta=lambda r: "",
        semver=lambda r: (r["l"], r["f"], int(ym(r["d"])))),
    "G3 A.1.2026.12": dict(
        raw=lambda r: "%s.%d.%d.%02d%s" % (lname(r["l"]), r["f"], r["d"][0], r["d"][1], pre(r)),
        core=lambda r: [r["f"], r["d"][0], r["d"][1]], meta=lambda r: "",
        semver=lambda r: (r["l"], r["f"], int(ym(r["d"])))),
    "G4 RevA.2026.12.1": dict(
        raw=lambda r: "Rev%s.%d.%02d%s%s" % (lname(r["l"]), r["s"][0], r["s"][1],
                                              "." + str(r["f"]) if r["f"] else "", pre(r)),
        core=lambda r: [r["s"][0], r["s"][1], r["f"]], meta=lambda r: "",
        semver=lambda r: (r["l"], int(ym(r["s"])), r["f"])),
}


def dotted(nums):
    return ".".join(str(n) for n in nums)


def mapped(eco, g, r):
    core, meta, p = dotted(g["core"](r)), g["meta"](r), r["p"]
    if eco == "dpkg":
        return "%d:%s%s%s" % (r["l"], core, "~" + p if p else "", "+" + meta if meta else "")
    if eco == "pacman":
        return "%d:%s%s%s" % (r["l"], core, p or "", "+" + meta if meta else "")
    if eco == "rpm":
        return "%d:%s%s%s" % (r["l"], core, "~" + p if p else "", "^" + meta if meta else "")
    if eco == "pep440":
        return "%d!%s%s%s" % (r["l"], core, {"alpha": "a0", "beta": "b0"}.get(p, ""),
                              "+" + meta if meta else "")
    ma, mi, pa = g["semver"](r)
    return "%d.%d.%d%s%s" % (ma, mi, pa, "-" + p if p else "", "+" + meta if meta else "")


def osrev(r):  # KERN_OSREV candidate: letter index, fix and stage in decimal digits
    return r["l"] * 1000000 + r["f"] * 10 + STAGE[r["p"]]


A1, A2, B1 = (2026, 11), (2027, 1), (2027, 10)
CHAIN = [  # expected strictly ascending
    ("A-alpha", rel(1, 0, "alpha", (2026, 11), A2)),
    ("A-beta", rel(1, 0, "beta", (2026, 12), A2)),
    ("A", rel(1, 0, None, (2027, 1), A2)),
    ("A.1-alpha", rel(1, 1, "alpha", (2027, 2), A2)),
    ("A.1", rel(1, 1, None, (2027, 3), A2)),
    ("A.2", rel(1, 2, None, (2027, 5), A2)),
    ("B-alpha", rel(2, 0, "alpha", (2027, 8), B1)),
    ("B", rel(2, 0, None, (2027, 10), B1)),
]
PAIRS = [(CHAIN[i][0], CHAIN[i][1], CHAIN[i + 1][0], CHAIN[i + 1][1])
         for i in range(len(CHAIN) - 1)]
PAIRS.append(("Y.3", rel(20, 3, None, (2031, 2), (2030, 6)), "AA",
              rel(21, 0, None, (2031, 9), (2031, 9))))
PAIRS.insert(2, ("A-beta same month", rel(1, 0, "beta", (2027, 1), A2), "A", CHAIN[2][1]))
PAIRS.insert(6, ("A.1-alpha same month", rel(1, 1, "alpha", (2027, 3), A2), "A.1", CHAIN[4][1]))
PAIRS.append(("A.7 built after B", rel(1, 7, None, (2028, 2), A2), "B", CHAIN[-1][1]))
TIE = (rel(1, 1, None, (2027, 3), A2), rel(1, 1, None, (2027, 4), A2))  # same fix, later respin


LET = "[A-HJ-NPRT-WY]{1,2}"
STG = "(?:-(?:alpha|beta))?"
MON = "(?:0[1-9]|1[0-2])"
GRAMMAR_RE = {  # the ABNF of each candidate, as a regular expression
    "G1c RevA.1-alpha": "Rev%s(?:\\.[1-9][0-9]*)?%s" % (LET, STG),
    "G1 RevA.1-alpha+202612": "Rev%s(?:\\.[1-9][0-9]*)?%s\\+[0-9]{4}%s" % (LET, STG, MON),
    "G2 RevA.1.202612": "Rev%s\\.(?:0|[1-9][0-9]*)\\.[0-9]{4}%s%s" % (LET, MON, STG),
    "G3 A.1.2026.12": "%s\\.(?:0|[1-9][0-9]*)\\.[0-9]{4}\\.%s%s" % (LET, MON, STG),
    "G4 RevA.2026.12.1": "Rev%s\\.[0-9]{4}\\.%s(?:\\.[1-9][0-9]*)?%s" % (LET, MON, STG),
}


def check_grammars():
    for n in range(1, 421):
        if lindex(lname(n)) != n:
            FAILED.append("letter round trip %d" % n)
    for gname, g in GRAMMARS.items():
        pat = re.compile(GRAMMAR_RE[gname])
        goods = [g["raw"](r) for _, r in CHAIN] + [g["raw"](PAIRS[-2][1]), g["raw"](PAIRS[-2][3])]
        for s in goods:
            if not pat.fullmatch(s):
                FAILED.append("%s rejects its own string %s" % (gname, s))
            for bad in (s.replace("A", "I", 1), s.replace("202703", "202713"),
                        s.replace("2027.03", "2027.13"), s.replace("alpha", "gamma"),
                        s.replace("Rev", "rev"), s + "-"):
                if bad != s and pat.fullmatch(bad):
                    FAILED.append("%s accepts %s" % (gname, bad))


def validators():
    def git_tag(s):
        return run(["git", "check-ref-format", "refs/tags/gizmobsd-v" + s]).returncode == 0

    def filename(s):
        name = "gizmobsd-" + s + ".uf2"
        return len(name) <= 255 and not re.search(r'[<>:"/\\|?*\x00-\x1f]', name)

    def dpkg(s):
        return {"ok": "T", "warn": "w"}.get(dpkg_status(s), "F")

    return {
        "git tag": git_tag, "file name": filename, "dpkg": dpkg,
        "pacman": lambda s: not re.search(r"[:/\s-]", s),
        "rpm": lambda s: re.fullmatch(r"[A-Za-z0-9._+~^]+", s) is not None,
        "PEP 440": lambda s: Version is not None and pep_cmp(s, s) is not None,
        "SemVer": lambda s: semver_key(s) is not None,
    }


def own_ok(eco, s):  # a mapped string under its own ecosystem's field rules
    epoch, rest = split_epoch(s)
    if eco == "dpkg":
        return dpkg_status(s) == "ok"
    if eco == "pacman":  # pkgver, with the epoch held apart
        return not re.search(r"[:/\s-]", rest)
    if eco == "rpm":  # Version tag, with Epoch held apart
        return re.fullmatch(r"[A-Za-z0-9._+~^]+", rest) is not None
    if eco == "pep440":
        return Version is not None and pep_cmp(s, s) is not None
    return semver_key(s) is not None


def earlier_rows(algos):  # the pairs of the first analysis, now over every algorithm
    pairs = [("0.a", "0.1"), ("0.a", "0.b"), ("0.9", "0.10"), ("0.2.0-alpha", "0.2.0-beta"),
             ("0.2.0-beta", "0.2.0-dev"), ("0.2.0-alpha", "0.2.0"), ("0.1.0-current", "0.1.0")]
    print("\nfirst-analysis pairs (-1: first is lower, 1: higher, inv: invalid for the algorithm)")
    print("%-28s" % "pair" + "".join("%-8s" % n for n, _ in algos))
    for a, b in pairs:
        cells = []
        for _, fn in algos:
            v = fn(a, b)
            cells.append("inv" if v is None else str(v))
        print("%-28s" % (a + " vs " + b) + "".join("%-8s" % c for c in cells))


def main():
    if not have("git"):
        print("git not installed: git rows and the legality rows are not run")
    universe = []
    for g in GRAMMARS.values():
        for _, r1, _, r2 in PAIRS:
            universe += [g["raw"](r1), g["raw"](r2)]
        universe += [g["raw"](TIE[0]), g["raw"](TIE[1])]
    universe += ["gizmobsd-v" + s for s in universe] + ["gcal-v1.9", "gcal-v1.10"]
    universe = sorted(set(universe))
    git_plain = GitSort(universe) if have("git") else None
    git_suffix = GitSort(universe, ["-alpha", "-beta"]) if have("git") else None
    calibrate_all(git_plain)
    check_grammars()
    if git_suffix is not None:
        calibrate("git+suffix", git_suffix.cmp, [("gcal-v1.9", "gcal-v1.10", -1)],
                  [("gcal-v1.10", "gcal-v1.9", -1)])
    for line in FAILED:
        print("CALIBRATION FAIL " + line)
    if FAILED:
        sys.exit(1)
    print("calibration passed")
    raw_algos = [("dpkg", dpkg_cmp), ("semver", semver_cmp), ("ascii", ascii_cmp)]
    if Version is not None:
        raw_algos.insert(2, ("pep440", pep_cmp))
    if have("vercmp"):
        raw_algos.append(("pacman", pacman_cmp))
    if have("rpm"):
        raw_algos.append(("rpm", rpm_cmp))
    if have("sort"):
        raw_algos.append(("sort-V", sortv_cmp))
    if git_plain is not None:
        raw_algos += [("git", lambda a, b: git_plain.cmp("gizmobsd-v" + a, "gizmobsd-v" + b)),
                      ("git+sfx", lambda a, b: git_suffix.cmp("gizmobsd-v" + a, "gizmobsd-v" + b))]
    earlier_rows([x for x in raw_algos if not x[0].startswith("git") and x[0] != "ascii"])
    names = [n for n, _ in raw_algos]
    print("\nraw strings: ascending pairs / pairs both valid, of %d pairs (inv: no valid pair)"
          % len(PAIRS))
    print("%-24s" % "grammar" + "".join("%-12s" % n for n in names))
    fails = {}
    for gname, g in GRAMMARS.items():
        row = "%-24s" % gname
        for n, fn in raw_algos:
            ok, inv = 0, 0
            for label1, r1, label2, r2 in PAIRS:
                got = fn(g["raw"](r1), g["raw"](r2))
                if got is None:
                    inv += 1
                elif got == -1:
                    ok += 1
                else:
                    fails.setdefault((gname, n), []).append(label1 + "<" + label2)
            row += "%-12s" % ("inv" if inv == len(PAIRS) else "%d/%d" % (ok, len(PAIRS) - inv))
        print(row)
    mapped_algos = [("dpkg", dpkg_cmp), ("pacman", pacman_cmp), ("rpm", rpm_cmp),
                    ("semver", semver_cmp)]
    if Version is not None:
        mapped_algos.insert(3, ("pep440", pep_cmp))
    print("\nmapped strings (letter index as epoch or major; see mapped())")
    print("%-24s" % "grammar" + "".join("%-8s" % n for n, _ in mapped_algos))
    for gname, g in GRAMMARS.items():
        row = "%-24s" % gname
        for n, fn in mapped_algos:
            ok = sum(1 for _, r1, _, r2 in PAIRS if fn(mapped(n, g, r1), mapped(n, g, r2)) == -1)
            row += "%-8s" % ("%d/%d" % (ok, len(PAIRS)))
        print(row)
    print("\nfirst failing pairs, raw strings")
    for (gname, n), lst in sorted(fails.items()):
        print("  %-24s %-8s %s" % (gname, n, ", ".join(lst)))
    print("\nsame fix, later respin (a tie in precedence; a result of 1 is a defect):")
    for gname, g in GRAMMARS.items():
        res = [(n, fn(g["raw"](TIE[0]), g["raw"](TIE[1]))) for n, fn in raw_algos]
        print("  %-24s %s" % (gname, " ".join("%s=%s" % (n, "inv" if v is None else v)
                                              for n, v in res)))
    print("\nZ to AA if Z were allowed (26 letters), raw core strings, -1 is the correct order:")
    print("  " + " ".join("%s=%s" % (n, fn("RevZ.3", "RevAA")) for n, fn in raw_algos
                          if not n.startswith("git")))
    print("\nexample strings, chain order")
    for gname, g in GRAMMARS.items():
        print("  %-24s %s" % (gname, "  ".join(g["raw"](r) for _, r in CHAIN)))
        print("  %-24s %s" % ("(dpkg map)", "  ".join(mapped("dpkg", g, r) for _, r in CHAIN)))
    print("\nlegality of the raw string: T accepted, F rejected, w accepted with a warning")
    val = validators()
    print("%-24s" % "grammar" + "%-10s" % "strings" + "".join("%-9s" % k for k in val))
    rels = {"release": [r for _, r in CHAIN if r["p"] is None] + [PAIRS[-2][3]],
            "labeled": [r for _, r in CHAIN if r["p"]]}
    for gname, g in GRAMMARS.items():
        for kind, rs in rels.items():
            cells = []
            for f in val.values():
                res = {f(g["raw"](r)) for r in rs}
                cells.append("T" if res == {True} or res == {"T"} else
                             "F" if res <= {False, "F"} else "w" if res == {"w"} else "mixed")
            print("%-24s" % gname + "%-10s" % kind + "".join("%-9s" % c for c in cells))
    own = [e for e in ("dpkg", "pacman", "rpm", "pep440", "semver") if e != "pep440" or Version]
    bad = [(e, mapped(e, g, r)) for g in GRAMMARS.values() for _, r in CHAIN for e in own
           if not own_ok(e, mapped(e, g, r))]
    print("mapped strings rejected by their own ecosystem's rules:", bad or "none")
    ex = rel(2, 3, "beta", (2027, 12), (2027, 10))
    print("\nmapping of", GRAMMARS["G1 RevA.1-alpha+202612"]["raw"](ex))
    for e in ("dpkg", "rpm", "pacman", "pep440", "semver"):
        print("  %-7s %s" % (e, mapped(e, GRAMMARS["G1 RevA.1-alpha+202612"], ex)))
    # KERN_OSREV candidates over the same pairs
    print("\nKERN_OSREV candidates, pairs ascending out of %d" % len(PAIRS))
    enc = sum(1 for _, r1, _, r2 in PAIRS if osrev(r1) < osrev(r2))
    ymi = sum(1 for _, r1, _, r2 in PAIRS if int(ym(r1["d"])) < int(ym(r2["d"])))
    print("  letter*10^6 + fix*10 + stage:", enc, " year-month YYYYMM:", ymi)
    print("  chain:", " ".join("%s=%d" % (n, osrev(r)) for n, r in CHAIN))
    print("  first value, RevA-alpha:", osrev(CHAIN[0][1]), "(DiscoBSD 202608 is below it)")
    top = (2**31 - 1 - 999999) // 10**6
    print("  int32 limit: letter index up to", top, "with fix up to 99999; double letters reach",
          len(LETTERS) * (len(LETTERS) + 1), "->", len(LETTERS) * (len(LETTERS) + 1) * 10**6)
    # git describe over a gizmobsd-v tag with a pre-release label
    if have("git"):
        with tempfile.TemporaryDirectory() as d:
            for c in (["init", "-q"], ["commit", "-q", "--allow-empty", "-m", "a"],
                      ["tag", "gizmobsd-vRevA.1-alpha"],
                      ["commit", "-q", "--allow-empty", "-m", "b"]):
                run(["git", "-C", d] + c, env=GIT_ENV)
            out = run(["git", "-C", d, "describe", "--match", "gizmobsd-v*", "--tags"],
                      env=GIT_ENV).stdout.strip()
            m = re.fullmatch(r"(.+)-(\d+)-g([0-9a-f]+)", out)
            print("\ngit describe:", out, "->", m.groups() if m else "no match")


main()
```
