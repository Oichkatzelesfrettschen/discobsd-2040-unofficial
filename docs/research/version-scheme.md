# Version numbering for GizmoBSD: what the tree encodes, what others publish, the adopted SemVer scheme and the schemes considered

Source reading at origin/main 32927ed0 (merge of #620); the text is a research record. Compiler for every size: arm-none-eabi-gcc 16.2.0 (local; CI uses 14.2.1, and sizes from the two compilers are not subtracted). Published pages were retrieved with curl on 2026-11-16 and every quotation below was matched with grep against the retrieved text; an item read only through a summarizing fetch or a search result is marked (S) and carries no quotation. Inferences are marked (I). The owner decisions this note answers are quoted inline where used: D51, the owner's request of 2026-11-16 ("GizmoBSD 0.a or ... do we use 'Rev A' to get started? Research and figure out what to do"); D52, a GizmoBSD param.h macro carrying the version D51 decides, beside the kept DiscoBSD, RetroBSD and 2.11BSD lineage macros; D38, host package gizmobsd-host 2.0 with discobsd-* command aliases for one release; and the rename plan's proposals D3 (version "GizmoBSD 0.1", with "derived from DiscoBSD 2.7" in etc/COPYRIGHT and motd) and D4 (a GizmoBSD 202611 value returned by KERN_OSREV).

## Status after the owner decisions of 2026-11-16

The owner adopted SemVer. Decision D111 supersedes D87, D91 and D92. Owner's words: "ABORT ABORT LET US JUST DO SEMVER WITH STABLE RESLEASE DATES IF WE EVER DO THAT" and "ABORT ABORT LET US JUST USE SEMVER". It applies scheme S1 of this note (0.y.z, the contract list moves y); the revision-letter scheme S3 and the composite letter grammars stay below as analysis, considered and not adopted. The decisions that bind the specification:

- D111: SemVer. "Stable release dates" is read as: a stable release may carry its build month as SemVer build metadata (`+YYYYMM`), which precedence ignores.
- D112, owner's words on `KERN_OSREV`: "design Packed letter/fix/stage but so that its always automatically updated when then version packged updates unless it breaks then it stays as it was instead of breaking the system but it can hard error in compile not just warning." Applied under SemVer: a packed integer of major, minor, patch and stage, generated at build time from the one version source, never hand-edited, with a compile-time check that fails the build. The owner chose its encoding on 2026-11-21 (D204 and the owner's answers of the same day): the decimal layout of "The adopted scheme", with the stage digit written beside the stage name in the version source. The generator merges after the rename pull request (D209), and `kern.osrev` keeps its present value until then.
- D113: `osrelease` (`uname -r`) carries the core version with its pre-release label and no date (D88 kept), for example `0.1.0-alpha`.
- D114: the build month in artifact and stable-release forms is spelled `+YYYYMM`.
- D115: tags repeat the core verbatim under the D90 namespace: `gizmobsd-v0.1.0`.
- The answers of 2026-11-19 (D152 to D159; dates of decisions in this note are UTC) to the residual decisions narrow the above: they are tabulated under "Residual decisions, answered 2026-11-19", and the specification section marks each clause they withdraw.
- D88 (the label is in `version[]` and `osrelease`), D89 (owner's words: "ensure 64-bit time is landed but its still Rev A alpha however the styling is for that."; the tree stays in its alpha state until the 64-bit `time_t` change lands, `docs/research/time-representation-past-2038.md`), D90 (the `gizmobsd-v*` namespace; `host-v*` stays the host package) and D94 (banners in gates and fixtures derive from `newvers.sh` after the rename) stand.
- Superseded: D87 (scheme S3), D91 (the spelling `RevA-alpha`), D92 (the composite letter, fix and date identifier, owner's words: "Rev A.1, A.2 with Date suffix: Rev A 2026.12 so figure this odd RevA.Semver.datever string being conconcted. Decompose to first principles and elucidate modern research!"), and D93 (owner's words: "Year-month probably but really you are working on an interesting string"; replaced by D112).

Sections marked "considered and not adopted" keep their measurements; the S3 display value (`Rev A` in `version[]`, `RevA` in `osrelease[]`) and the composite letter grammars are among them. The sections added after the first analysis were retrieved and measured on 2026-11-16 under the same rule as the first analysis: a quotation was matched against the retrieved text, an item read through a summarizing fetch or search result is marked (S), and an inference is marked (I).

## Findings

1. The tree uses neither SemVer nor a "Rev" letter. It carries five identities: the upstream DiscoBSD release counter (`2.7`), a year-and-month integer inherited from 4BSD (`202608`), a build identity (commit count and compile count), a root-image stamp from `git describe`, and a host-package number (`1.0.9`) that is SemVer-shaped. Each is set in a different file and no file states how they relate.
2. Owner decision D51 asks for a choice among "0.a", "Rev A" and a SemVer-style number. Plan decision D3 proposes "GizmoBSD 0.1". The string "0.1" is not a valid SemVer 2.0.0 version, which requires three numeric fields (item 2). "0.a" is invalid under SemVer and sorts in opposite directions across package-version algorithms (measured below). "Rev A" breaks the field split in `sys/conf/newvers.sh` (measured below).
3. The inherited release cycle (`sys/conf/newvers.sh:18-20`) keeps the release number after a release and appends "-current". Under SemVer precedence (item 11) "0.1.0-current" sorts below "0.1.0", opposite to its chronology. A SemVer adoption changes that cycle.
4. Firmware identity reads the host-tool tag. `git describe --tags origin/main` prints `host-v1.0.7-2660-g32927ed0`, and `Makefile:2236` writes that string into `/etc/release`.
5. The string-length cost of every first-analysis candidate is at most 16 bytes of flash `.rodata` and 0 bytes of RAM in `vers.o` (arm-none-eabi-gcc 16.2.0; linked kernel not run); the composite candidates cost up to +34 bytes (finding 11).
6. The owner adopted SemVer (D111): scheme S1, 0.y.z, with the first tree state `0.1.0-alpha`. The revision-letter scheme S3 and the composite letter grammars (analysis below) are considered and not adopted.
7. The adopted strings are already valid SemVer and normalize as PEP 440 without a mapping. Over 13 release-state pairs the raw strings order 13/13 under SemVer and PEP 440. dpkg, pacman and plain lexical tools (`sort -V`, git version sort, strcmp) order the labeled core form 8/13 or 6/13, because a hyphen label sorts after its release and strcmp also misorders 0.9.3 against 0.10.0. Mapped strings (tilde, caret or letter suffix) order 13/13 in dpkg, pacman, RPM and PEP 440. Appendix B.
8. The build month is ordering-inert only under SemVer (item 10). dpkg, pacman, RPM and PEP 440 order two builds of one core by their metadata, in build order and never reversed (measured tie rows), so the month is a tie-break there and never a key. A month alone does not identify a development artifact; the first specification proposed adding the commit count (`+202611.3000`), which D157 did not adopt, so development artifacts carry no version suffix.
9. The encoding of `KERN_OSREV` (D112), chosen by the owner on 2026-11-21 after the study of `packed-version-integer.md` (decimal, D204; this layout; the stage digit written beside the stage name in the version source; D158 keeps the stage list out of every tool): the packed integer `major * 10^8 + minor * 10^6 + patch * 10 + stage`, a named stage taking its digit from the version source (alpha 1 and beta 2 today) and a release taking 9. The generator merges after the rename pull request (D209), and `kern.osrev` keeps its present value until then. `0.1.0-alpha` packs to 1000001, above the inherited 202608; the tree holds no comparison against the old value. The widths (major at most 20, minor at most 99, patch at most 99999) keep the value within a signed 32-bit `long` (largest 2099999999). `newvers.sh` computes it from `OSR` and the stage digit `OSRANK` (for example `alpha=1`); a static assertion and the generator's grammar check fail the build on 15 of 15 measured mutants and pass 5 of 5 good values (Appendix C). The packed integer orders 13/13 pairs; a year-and-month integer 10/13.
10. The D113 label in both strings costs +16 bytes of flash `.rodata` against `2.7` (95 against 79 bytes of `vers.o`, arm-none-eabi-gcc 16.2.0); the generated `osrev` constant of the candidate in finding 9 would add 5 more (100); RAM is unchanged.
11. The letter-composite analysis (considered, not adopted) found: (letter, fix, stage) is the minimal total-order key and the build month is not a key, because a maintenance release built after the next revision breaks date order; no raw string form orders correctly in every algorithm; a packed (letter, fix, stage) integer orders 11 of 11 pairs and year-and-month 8.
12. The owner answered the residual decisions on 2026-11-19 (D152 to D159): no tag at the rename, no fixed stage list, no development-artifact suffix, no compile-time version macro, a wall-clock release month, the packed encoding left to a study (the owner chose it on 2026-11-21, D204 to D209) and the contract list a draft. The table under "Residual decisions, answered 2026-11-19" gives each.
13. Not verified at a primary source: the ASME Y14.35 standard itself (paywalled; quoted from a 2008 presentation and a 2014 draft), illumos and OmniOS release strings, the Fedora packaging guidelines page, and any empirical study of calendar or hybrid versioning (none found). The retrieval caveats of the composite section list the rest.

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
| Planned | host package `gizmobsd-host 2.0.0` with `discobsd-*` aliases for one release (owner decision D38, fixed); OS version: SemVer, tree state `0.1.0-alpha` (D111, D153), with no tag, no fixed stage list and no artifact suffix at the rename (D157 to D159); `kern.osrev` keeps its value until the generator merges after the rename pull request (D156, D209); no compile-time version macro for programs (D155) | |

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

S1 is described here as the first analysis proposed it. Of its rules the owner adopted the three-field number and the first value (D111); the contract list is a draft awaiting amendment (D152), and the release cycle and the point at which `0.1.0` is cut are the owner's decisions, not rules (D153, D158). "The adopted scheme" below lists what is in force.

Status: adopted by D111. The first value, the `param.h` role and the label placement below are revised by D111 to D115 and by "The adopted scheme"; the measurements stay.

- Rules. Three numeric fields. While X is 0, y increments when an item in the contract list changes incompatibly, z increments for compatible fixes, and a label (`-alpha` on trunk, `-beta` before the cut) follows SemVer item 9. The list (I): the a.out user ABI (header and magic, system-call numbers and argument layouts, layouts and fundamental-type widths in installed headers, the installed libc interface whatever its linkage: declarations in the installed headers and the function and data symbols with their calling conventions, which are `/usr/lib/libc.a` today (`distrib/rp2040/mi.rp2040:350`; the image ships no shared libc) and a shared libc if one is added (`docs/research/xip-shared-libc` is a design, not a shipped object). A removed export or an incompatible declaration breaks the rebuild of existing programs, so it moves y and a z increment cannot carry it); on-disk and flash formats (partition map, root-file-system layout, swap); the host-visible wire (USB identity, console framing, the sysctl names the host tools read). 1.0.0 is cut when the list has not changed incompatibly across two consecutive releases and the host-link wire is documented (I).
- Release cycle. After a release the trunk moves to the next planned version with `-alpha` (0.2.0-alpha after 0.1.0), the cut candidate carries `-beta` (0.2.0-beta), and the release carries no label (0.2.0). SemVer item 11 orders these 0.1.0 < 0.2.0-alpha < 0.2.0-beta < 0.2.0, and PEP 440 agrees (measured below). The label `-dev` is excluded: ASCII order puts it above `-beta` under item 11 and below it under PEP 440. This replaces the inherited "keep the same number, append -current" step (`newvers.sh:18-20`), whose "0.1.0-current" sorts below 0.1.0.
- First value (revised by D111 to D113). The first tree state is `GizmoBSD 0.1.0-alpha` and `0.1.0` is the first stable release, with `etc/COPYRIGHT` and motd stating "derived from DiscoBSD 2.7" (plan D3).
- param.h (revised by D112). The first analysis kept a year-and-month `GizmoBSD 202611` returned by `KERN_OSREV` (plan D4). D112 replaces it with the packed integer generated from the version source; "The adopted scheme" specifies it. The integer is monotone across the rename (1000001 above 202608), which the string ordering "0.1.0" below "2.7" is not.
- Host package. Independent SemVer. `gizmobsd-host 2.0.0` (D38) stays; its README names the OS contract range it speaks ("GizmoBSD 0.1.x console and link wire"). OS releases tag as `gizmobsd-v<version>` and the root-image stamp calls `git describe --match 'gizmobsd-v*'`; `host-v*` keeps its meaning.
- Answers to the critiques. Hickey and Hynek: the number claims one thing, that the contract list changed, and 0.y.z states in the specification that anything may change. Hyrum: only the listed observables are promised. ZeroVer: the 1.0.0 criterion is written down. Go and Cargo read 0.y.z as y breaking, the same reading the list gives.
- Tree changes. `newvers.sh` (OST, OSR, cycle comment, and the awk step so `$S` reaches `osrelease` if pre-release builds must differ under `uname -r`); `sys.mk` (an `OSPATCH` variable, with `OSREV`, `OSRev` and `OSrev` built from three fields); `param.h`; `Makefile:2236` (--match); the literal banner in 7 gate and fixture files, 3 shipped text files and 3 documents (listed in the tree table); the picofetch fixture `usr.bin/picofetch/tests/hwinfo_fixture.c:85-86`, which holds the type and release as separate strings, so `render.py:242` and the fixture move together; package metadata maps the label with each format's own syntax. The banner edits occur in the rename in any case, because the name changes.
- Cost. +4 bytes flash `.rodata` and 0 bytes RAM for the unlabeled release string; a label in the version string adds 6 bytes of flash (`-alpha`), and a label also in `osrelease` adds 6 more.

### S2: CalVer, YYYY.MM

Status: considered and not adopted (D111).

- Rules. The version is the release month. Compatibility is carried by a separate integer, the ABI epoch, held in a file the image ships (`/etc/release` or a sibling) and incremented when the contract list of S1 changes incompatibly; calver.org's own guidance pairs the date with a changelog and a deprecation policy.
- First value. `GizmoBSD 2026.11` (or the release month), ABI epoch 1.
- param.h. `GizmoBSD 202611`; the digits now equal the printed version, removing today's pair of "2.7" and 202608. `DiscoBSD_2_7` has no replacement. `KERN_OSREV` = 202611.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the number never claims compatibility. ZeroVer: no 0.x band. Cost of the answer: the printed version says nothing about a break, so a user needs the epoch.
- Tree changes. As S1 (`OSPATCH` unneeded); the cycle comment changes to name a month, and `OSR` becomes a second date next to the date already in the `version` string.
- Cost. +8 bytes flash `.rodata`, 0 bytes RAM. The release month is a property of the day a release is cut; the upstream cadence (Feb and Aug) fits, a fork cadence is unknown (I).

### S3: Revision letter, hardware style, "Rev A"

Status: considered and not adopted (D111; D87 had chosen it and is superseded). The display-value representation below (`Rev A` in `version[]`, `RevA` in `osrelease[]`), the fix identity by build counter `#N`, the revision-index `KERN_OSREV` (`GizmoBSD_REV`) and the 26-letter bound are superseded by the composite analysis and by D111; the measurements stay.

- Rules. One uppercase letter names a change-controlled configuration. The letter advances when the contract list of S1 changes incompatibly; fixes inside a letter are identified by the build identity (`#N`, commit count). 26 letters bound the series; a continuation (AA) is a rule to add.
- First value. `Rev A` in the `version` string, written `RevA` in `osrelease`.
- param.h. `GizmoBSD_REV 1` (A is 1) returned by `KERN_OSREV`. This loses the year-and-month value of D4 in that sysctl; the date moves to the build stamp. `sys/sys/sysctl.h:129` names `KERN_OSREV` "system revision", which the letter fits literally.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the letter claims one bit and says so. Hyrum: the same contract list. ZeroVer: no 0.x band. Cost: no ordering of magnitude, no pre-release vocabulary, and tools that sort versions treat letters as in the table above.
- Tree changes. As S1, plus a second value in `newvers.sh`: a display value that `version[]` prints (`Rev A`) beside the unspaced `OSR` (`RevA`) that `osrelease[]` and `osversion[]` consume. The scratch variant changes four lines of the script (Appendix A.2); a delimiter change alone would put the space in both strings.
- Cost. +3 bytes flash `.rodata` for the specified split, +4 with `Rev A` in both strings, 0 bytes RAM. The earlier +2 measured `RevA` in both strings, which is neither form.

### The owner's two candidates by name

Recorded analysis; D87 retained "Rev A" and D111 replaced it with SemVer.

- "GizmoBSD 0.a". Rejected as an OS version. SemVer item 2 requires numeric fields; PEP 440, pacman, and sort -V place it below "0.1" while the dpkg algorithm places it above (measured above); the alphabet bounds the series. The intent behind it, a pre-release signal, is expressed by S1 with `0.1.0-alpha`.
- "Rev A". Retained as S3 and not recommended as the series. It has a hardware precedent (the tracked datasheet table, and the RP2040 stepping names), and the RP2040 case shows the weakness: the silicon keeps a letter label for people and a small integer for software, and the integer cannot separate B0 from B1. A letter alone leaves software without an ordered value; S1 keeps a letter-free integer in `KERN_OSREV`.

## One scheduled break through each scheme

The 64-bit `time_t` at the user interface is an incompatible user-ABI change (the owner chose the 64-bit interface, recorded at `docs/research/time-representation-past-2038.md:147`).

| Scheme | Release before | Release after | What a user learns |
| --- | --- | --- | --- |
| S1 | 0.1.0 | 0.2.0 | y moved: the contract list changed; recompile programs |
| S2 | 2026.12 (epoch 1), illustrative | 2027.01 (epoch 2), illustrative | the month moved always; the epoch file carries the break |
| S3 | Rev A | Rev B | the letter moved: the contract list changed |

## The adopted scheme: SemVer 0.y.z (D111 to D115)

This section is the specification written for the decisions of 2026-11-16. The answers of 2026-11-19 (the table under "Residual decisions, answered 2026-11-19") withdrew much of it, so the section is kept as analysis, and only the following is in force:

- the core is SemVer `MAJOR.MINOR.PATCH` with an optional pre-release label, and the label is any identifier SemVer item 9 allows, which the owner names stage by stage (D111, D158);
- the tree state is `0.1.0-alpha`; when a stage ends, what follows it and when `1.0.0` arrives are the owner's decisions, with no cycle fixed in advance (D153);
- `version[]` and `osrelease` carry the core with its label and no date (D88, D113);
- a stable release may carry its release month as `+YYYYMM`, the month of the day it is released, by wall clock (D114, D154);
- tags, when the owner starts tagging, use `gizmobsd-v` plus the core (D90, D115); no tag exists at the rename (D159).

Every other clause below, including the two-label grammar, the alpha to beta to release cycle, the development form and its rule and the tagged-epoch month, is the 2026-11-16 proposal and binds nothing, and the packed `KERN_OSREV` encoding is the owner's choice of 2026-11-21 (D204 and the owner's answers of the same day); the marks on individual clauses name the decision that withdrew each. The harness is Appendix B (SemVer strings measured next to the letter grammars) and the `KERN_OSREV` generator replay is Appendix C.

### Grammar and forms

```abnf
; RFC 5234. SemVer 2.0.0 restricted to the labels this port uses.
num     = "0" / %x31-39 *DIGIT          ; no leading zero (SemVer item 2)
stage   = "alpha" / "beta"              ; proposal; D158 sets no fixed list
core    = num "." num "." num [ "-" stage ]
month   = 4DIGIT ( "0" %x31-39 / "1" %x30-32 )   ; YYYYMM
count   = 1*DIGIT                       ; commit count; build metadata allows leading zeros
version = core [ "+" month [ "." count ] ]
```

- Core: `MAJOR.MINOR.PATCH` with the optional label. While MAJOR is 0, MINOR increments when the contract list of S1 changes incompatibly against the previous stable release and PATCH increments for compatible fixes. The pre-release vocabulary is `alpha` and `beta` (`-dev` and `-current` are excluded for the ordering reasons of S1).
- Stable form, `core+YYYYMM` (`0.1.0+202701`): a stable release carries its build month (D111, D114). Item 10 of SemVer: "Build metadata MUST be ignored when determining version precedence."
- Development form (not adopted, D157; kept as analysis), `core+YYYYMM.count` (`0.1.0-alpha+202611.3000`): a non-final artifact appends the commit count (the number `newvers.sh` already reads with `git rev-list --count HEAD`). Two commits of one month under one `0.1.0-alpha` then differ, and the build identifier stays outside precedence.

Surfaces, first tree state (the tree remains `0.1.0-alpha` until the 64-bit `time_t` change lands, D89):

| Surface | Form | Example |
| --- | --- | --- |
| `osrelease[]`, `uname -r` (D113) | core | `0.1.0-alpha` |
| `version[]` | core, then the existing build counter, commit count and full date | `GizmoBSD 0.1.0-alpha (PICO) #1 3000: <date>` |
| `KERN_OSREV` (D112) | packed integer in the decimal layout below (D204 and the owner's answers of the same day); `kern.osrev` keeps its present value until the generator merges after the rename pull request (D209) | 1000001 |
| Git tag (D115) | `gizmobsd-v` plus the core | `gizmobsd-v0.1.0` at the first stable release; no tag at the rename (D159) |
| `/etc/release` | `git describe --match 'gizmobsd-v*' --tags --dirty` once a `gizmobsd-v*` tag exists; until then its present source (D159) | `gizmobsd-v0.1.0-4-g<hash>`, with `-dirty` appended for local changes |
| Development artifact names | no suffix adopted (D157) | -- |
| Stable artifact names and citations | stable form | `gizmobsd-0.1.0+202701.uf2`, "0.1.0, 2027.01" |

Release cycle: `0.1.0-alpha`, `0.1.0-beta`, `0.1.0`, then the trunk moves to `0.1.1-alpha`, and a change to the contract list moves it to `0.2.0-alpha`. The packed integers of this chain increase in that order (below).

### Rules

1. Contract baseline. The contract list is compared against the previous stable release. Alpha and beta states promise nothing: SemVer item 9 reads "A pre-release version indicates that the version is unstable and might not satisfy the intended compatibility requirements as denoted by its associated normal version." The first stable release `0.1.0` has no predecessor, so the 64-bit `time_t` interface change of D89 lands inside the pre-release states and is part of `0.1.0` without moving MINOR. After `0.1.0` an incompatible change moves MINOR.
2. 1.0.0 is not cut before the 64-bit `time_t` change has landed (D111, D89) and the criterion of S1 holds: the contract list has not changed incompatibly across two consecutive stable releases and the host-link wire is documented.
3. Canonical epoch (not adopted for stable releases, D154: the month of a stable release is the month of its release day by wall clock; the rule below is kept as analysis). The build month is the UTC month of the `SOURCE_DATE_EPOCH` in effect, not the wall clock. `newvers.sh` uses the committer date of HEAD when the variable is unset, and a caller can set it to anything, so the month is a function of the commit and the epoch. A stable release is built with the committer date of its tagged commit as the epoch, from an annotated tag whose message records the epoch; a caller-supplied epoch for a stable artifact is a defect of the release process (residual decision R3).
4. One artifact set. A published stable release identifies one artifact set; a rebuild that changes content takes the next PATCH. Development artifacts are identified by the development form, so the month alone never has to separate two builds.
5. Minimum-release check. A consumer that requires a minimum release compares the full precedence, stage included: `0.1.1-alpha` does not satisfy a minimum of `0.1.1`, and a pre-release is accepted only when the consumer names it or sets an explicit opt-in. The packed integer carries the same order (`KERN_OSREV >= 1000019` is "at least 0.1.1"; `0.1.1-alpha` is 1000011). No host tool performs such a check today (the `sramdump` identity check compares `version[]` bytes).

### Measured ordering

The harness compares 13 release-state pairs, each expected ascending: 0.1.0-alpha < 0.1.0-beta; 0.1.0-beta < 0.1.0; 0.1.0-beta < 0.1.0 with both built in one month; 0.1.0 < 0.1.1-alpha; 0.1.1-alpha < 0.1.1; 0.1.1-alpha < 0.1.1 with both in one month; 0.1.1 < 0.1.2; 0.1.2 < 0.2.0-alpha; 0.2.0-alpha < 0.2.0; 0.1.9 < 0.1.10; 0.9.3 < 0.10.0; 0.99.2 < 1.0.0-alpha; and 0.1.7 built in 2028-02 < 0.2.0 built in 2027-10 (a maintenance release). Algorithms are calibrated before any verdict, as in the composite analysis; a tool that is absent is reported as not run and its column is omitted.

Raw strings, ascending pairs out of valid pairs (RPM rejects a hyphen in a Version, so its row counts the 4 unlabeled pairs):

| Form | SemVer | PEP 440 | dpkg | pacman | RPM | sort -V | git | git with versionsort.suffix | strcmp |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| core, `0.1.0-alpha` | 13/13 | 13/13 | 8/13 | 8/13 | 4/4 | 8/13 | 8/13 | 13/13 | 6/13 |
| stable, `0.1.0-alpha+202611` | 13/13 | 13/13 | 13/13 | 13/13 | 4/4 | 8/13 | 8/13 | 13/13 | 6/13 |
| development, `0.1.0-alpha+202611.3000` | 13/13 | 13/13 | 13/13 | 13/13 | 4/4 | 8/13 | 8/13 | 13/13 | 6/13 |

Mapped strings (below) order 13/13 in dpkg, pacman, RPM, PEP 440 and SemVer for all three forms.

- SemVer and PEP 440 need no mapping: the strings are valid SemVer, and PEP 440 normalizes `-alpha` to `a0`. Git accepts every form as a tag `gizmobsd-v...`, file names accept every form, and dpkg accepts every form without a warning (the upstream version starts with a digit); pacman's `pkgver` and RPM's Version reject the hyphen of labeled strings.
- The hyphen label sorts after its release in dpkg (as a Debian revision), pacman (as a package release), `sort -V` and git version sort: five of the 13 pairs fail for the core form (each label against its own release). With `-c versionsort.suffix=-alpha -c versionsort.suffix=-beta` git orders 13/13. strcmp also fails the two digit-count pairs (`0.1.9` against `0.1.10`, `0.9.3` against `0.10.0`).
- dpkg and pacman order the stable and development forms 13/13 because the trailing "+metadata" makes their release strings differ before the label is compared; the core form shows the label behavior underneath (I: a mechanism read from the port and the 8/13 result).
- Ties. Two builds of one core (0.1.1 built 2027-03 with commit count 3160 against 2027-04 with 3190): the core form compares equal in every algorithm; the stable and development forms compare equal under SemVer (item 10) and order the later build later (-1, never reversed) under dpkg, PEP 440, pacman, RPM, `sort -V`, git and strcmp. Only SemVer ignores the metadata; elsewhere it is a tie-break in build order. This corrects the composite analysis, which stated that precedence ignores the date.

Mapping to package ecosystems, for `0.2.3-beta+202712.3500`:

| Ecosystem | Mapped string | Mechanism |
| --- | --- | --- |
| dpkg | `0.2.3~beta+202712.3500` | tilde sorts below the end of the string |
| RPM | `0.2.3~beta^202712.3500` | tilde below, caret above the base and below the next release segment |
| pacman | `0.2.3beta+202712.3500` | a letter suffix sorts below the bare number (vercmp(8): "1.0a < 1.0b < 1.0beta < 1.0p < 1.0pre < 1.0rc < 1.0") |
| PEP 440 | `0.2.3b0+202712.3500` | beta pre-release, local label |
| SemVer | `0.2.3-beta+202712.3500` | unchanged |

git describe, measured in a scratch repository with the options `Makefile:2236` passes (`--dirty --tags`) plus `--match 'gizmobsd-v*'`: at the tag commit it prints `gizmobsd-v0.1.0-alpha`; one commit later `gizmobsd-v0.1.0-alpha-1-g<hash>` (the hash varies with the scratch repository's commit time); with a modified file `-dirty` follows. The pattern `^(.+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$` recovers the tag, the count, the hash and the dirty marker.

### KERN_OSREV

The owner chose the encoding below on 2026-11-21 (decimal, D204; this layout; the stage digit beside the stage name in the version source). The generator merges after the rename (D209), and nothing here is implemented at the rename.

D112 asks for a packed integer that follows the version source and fails the build when it cannot.

Encoding, decimal fields:

| Field | Weight | Range | Digits |
| --- | --- | --- | --- |
| stage | 10^0 | 1 alpha, 2 beta, 9 final (0 and 3 to 8 reserved) | 1 |
| patch | 10^1 | 0 to 99999 | 5 |
| minor | 10^6 | 0 to 99 | 2 |
| major | 10^8 | 0 to 20 | 2 |

`KERN_OSREV = major * 100000000 + minor * 1000000 + patch * 10 + stage`. Decoding: `major = E / 100000000`, `minor = (E / 1000000) % 100`, `patch = (E % 1000000) / 10`, `stage = E % 10`. The chain: `0.1.0-alpha` 1000001, `0.1.0-beta` 1000002, `0.1.0` 1000009, `0.1.1-alpha` 1000011, `0.1.1` 1000019, `0.1.2` 1000029, `0.2.0-alpha` 2000001, `0.2.0` 2000009. The packed integer orders 13 of 13 pairs ascending; a year-and-month integer orders 10 of 13, failing the two same-month pairs and the maintenance pair.

32-bit bounds. `long` is a signed 32-bit type on this target (`sysctl_rdlong` with `(long)DiscoBSD` today; maximum 2147483647). The largest packable value, `20.99.99999` final, is 2099999999 and builds (Appendix C); major 21 reaches 2199999999 and overflows. Minor 100 would add 100000000 and collide with major + 1 (the harness confirms that `0.100.0` and `1.0.0` pack to one value), which is why the widths are enforced rather than left to arithmetic. The patch field stays below the minor weight (99999 * 10 + 9 is 999999).

First value. `0.1.0-alpha` packs to 1000001, above the inherited 202608. A search of the tree outside `docs`, `legacy` and Markdown for `KERN_OSREV`, `DiscoBSD_2_7`, `202608`, `kern.osrev` and `osrev` finds the definitions in `sys/sys/param.h:8-9`, the one reader `sys/kern/kern_sysctl.c:231`, the name in `sys/sys/sysctl.h:129` and the manual pages `share/man/man3/sysctl.3` and `sbin/sysctl/sysctl.8` (which lists `kern.osrevision` as long); no program, test, gate or script compares the value. A program outside the tree that tests `osrev >= 202608` stays true across the rename; one that tests `osrev == 202608` stops matching, and no such program is known (I). The inherited `BSD 211` macro is unrelated.

Generation mechanism, with one hand-edited value, `OSR` in `sys/conf/newvers.sh`:

| File | Role |
| --- | --- |
| `sys/conf/newvers.sh` | computes. It parses `OSR` (`MAJOR.MINOR.PATCH` with an optional `-alpha` or `-beta`), rejects anything else (a label other than alpha or beta, a numeric label, build metadata, a field that is missing, extra, non-numeric, empty or has a leading zero, and a field longer than six digits) with a non-zero exit, and emits to `vers.c` the field macros, `OSREV_PACKED`, `OSREV_STRING`, the static assertions and `const long osrev`. `osrelease` and `version[]` print from the same `OSR`. |
| `sys/arch/rp2040/conf/Makefile.rp2040` and the generated `compile/*/Makefile` | no change. `SYSTEM_LD_HEAD` already runs `sh $S/conf/newvers.sh > vers.c.tmp && mv vers.c.tmp vers.c && ${CC} ${CFLAGS} -c vers.c`, so a rejected `OSR` writes no `vers.c` and the link stops, while the previous `vers.c` and `vers.o` stay as they were. The kernel flags (`-Wall -Wextra -Werror`, `-std=gnu17`) turn every static assertion into a build error. |
| `sys/sys/systm.h` | declares `extern const long osrev;` beside `osrelease`. |
| `sys/kern/kern_sysctl.c` | `case KERN_OSREV` returns `osrev` in place of `(long)DiscoBSD`. |
| `sys/sys/param.h` | loses `DiscoBSD` and `DiscoBSD_2_7` (no reader); the hand-edited year-and-month macro and the `DiscoBSD_X_X` step of the `newvers.sh` header comment retire. Whether to keep a compile-time macro for user programs is residual decision R4. |
| `share/mk/sys.mk` | consumes. `OSMAJOR`, `OSMINOR`, `OSREV`, `OSRev` and `OSrev` are derived from the same line, for example `OSR!= sed -n 's/^OSR="\(.*\)"$/\1/p' ${TOPSRC}/sys/conf/newvers.sh`, so the manual-page footer and the archive names in `etc/Makefile` follow `OSR` (a third field and the label enter `OSREV` the way S1 states). |

D112 reads "unless it breaks then it stays as it was instead of breaking the system but it can hard error in compile": the `mv` after a successful generation leaves the previous `vers.c` in place on failure, and the `_Static_assert` lines and the generator's grammar check are the hard error.

The checks in `vers.c`, each an error at compile time: major at most 20, minor at most 99, patch at most 99999, stage in 1 to 9; `OSREV_PACKED` equals the fields combined with 32-bit `long` arithmetic; and `__builtin_strcmp` of `OSREV_STRING` against the string built from the field macros and `OSREV_PRE` equals zero, so the version string and the integer cannot disagree even if either generated line is edited by hand.

Mutants, from the replay of Appendix C (arm-none-eabi-gcc 16.2.0, the warning and standard flags of `Makefile.rp2040` without the debug options, on a scratch generator; no tree file changed). Five good values build (0.1.0-alpha, 0.1.0, 0.99.99999-beta, 20.99.99999, 1.0.0). Fifteen mutants must fail:

| Mutant | Stage that fails | Message |
| --- | --- | --- |
| `OSR=21.0.0` | compile | "OSREV major exceeds 20" |
| `OSR=0.100.0` | compile | "OSREV minor exceeds 99" |
| `OSR=0.1.100000` | compile | "OSREV patch exceeds 99999" |
| `OSREV_PACKED` edited in `vers.c` | compile | "OSREV packed value disagrees with its fields" |
| `OSREV_STAGE` edited | compile | "OSREV packed value disagrees with its fields" |
| `OSREV_MINOR` edited | compile | "OSREV packed value disagrees with its fields" |
| `OSREV_STRING` edited | compile | "OSREV string disagrees with its fields" |
| `OSR=0.1.0-gamma`, `0.1.0-alpha.1`, `0.01.0`, `0.1.0+202611`, `0.1`, `0.1.0.0`, empty, `0.1.0000000007` | generator | non-zero exit, no `vers.c` |

Cost. `vers.o` `.rodata` with `OSR=0.1.0-alpha` in the stock output (both strings, the Appendix A method): 95 bytes, +16 against `DiscoBSD 2.7` (79); with the `osrev` lines: 100 bytes (+4 for the `long` and 1 of alignment). The consumer, measured on a stand-in function with the immediate and with the variable: `.text` 16 bytes in both, `.data` 0 (the literal-pool word for 202608 becomes the pool word for the address of `osrev`, plus one load; padding absorbs it at this size). Kernel RAM is unchanged (`const`). The linked-kernel text change and `kern_sysctl.o` itself were not measured (kernel builds run on the remote build host).

### Review comments on pull request 646

Each Copilot comment was verified against the text and the harness it cites.

1. Mapped table and missing tools: applies; fixed. The harness filters every algorithm by availability (pacman by `vercmp`, RPM by `rpm`, PEP 440 by `packaging`) and reports an absent one as not run.
2. Legality table without git, PEP 440 column without `packaging`: applies; fixed. The git tag column and the PEP 440 column are omitted when their tool is absent; a run with no tools on `PATH` completes and reports the omissions.
3. Tag date cannot reconstruct the month: applies. Rule 3 defines the canonical epoch and an annotated tag that records it; the composite section's "a function of the commit" is corrected to "of the commit and the epoch".
4. The date is not precedence-inert in dpkg and PEP 440 mappings: applies. The tie rows are measured for raw and mapped strings; the claim is limited to SemVer, and elsewhere the metadata is a tie-break in build order.
5. The full form does not identify development artifacts: applies. The development form adds the commit count; the one-artifact-set rule covers stable releases.
6. D89 against the bump rule: applies. Rule 1 starts the contract at the first stable release and makes alpha and beta states promise nothing.
7. The minimum check ignores the stage: applies. Rule 5 compares the stage as part of the minimum release and requires an explicit pre-release opt-in.

None is declined.

## Considered and not adopted: the composite letter identifier

Status: considered and not adopted (D111 supersedes D87 and D92). The analysis and its measurements stay; the rules the adopted scheme also needs (contract baseline, canonical epoch, development-artifact identity, minimum-release check) are restated for SemVer in "The adopted scheme" above. Sections of this analysis cite D92 for the request.

Owner decision D92 asks for one string that carries a revision letter, a fix number and a date ("Rev A.1, A.2 with Date suffix: Rev A 2026.12"). This section derives which facts the string must carry, which fields are independent, what published practice and research say, and specifies a grammar with measured behavior. The harness is Appendix B; the size replay is Appendix A.3. Sources were retrieved with curl on 2026-11-16 and each quotation was matched against the retrieved text; retrieval caveats are listed at the end of the section.

### What each consumer needs from the string

| Consumer | Reads | Needs | Form that serves |
| --- | --- | --- | --- |
| A person reading the boot banner or `uname -sr` | `version[]` and `ostype` plus `osrelease` | the compatibility letter and whether the tree is pre-release; the build date already follows the build counter in `version[]` | core form |
| `uname -r` and `kern.osrelease` (`KERN_OSRELEASE`) | a string up to 255 bytes (`SYS_NMLN` is 256 in `sys/sys/utsname.h`) | stability across rebuilds of one release, so the exact-match gates in 7 gate and fixture files keep matching; no whitespace, because `newvers.sh` splits fields with awk | core form (a date in the string changes it monthly) |
| `KERN_OSREV` | a signed 32-bit `long` (`sysctl_rdlong` with `(long)DiscoBSD`, `sys/kern/kern_sysctl.c:231`; `long` is 32 bits on ARMv6-M) | one integer comparison, "is this kernel at least release X" | packed integer |
| Host tools' identity checks | `discobsd-sramdump` compares the ELF's `version` symbol bytes with the saved flash (`sramdump.py:130` lists `version` in `REQUIRED_SYMBOLS`; the check is at lines 323-335); `link.py` and the other tools carry a `--version` flag for the host package only, and a search of `distrib/rp2040/host/src` for `osrelease`, `kern.version` and `uname` matches nothing | today a byte-identical `version[]` between the ELF and the flash, which any form of the string satisfies; a compatibility check, if added, needs equality on the letter and a minimum on the fix, parsed from `uname -r` or read as the integer | core form, and the integer |
| `git describe --match 'gizmobsd-v*'` | tag names; output gains `-<n>-g<hash>` | a legal ref name, matchable by glob, parsable from the right | core form, verbatim |
| Package-version algorithms (dpkg, pacman `vercmp`, RPM, PEP 440, SemVer, `sort -V`) | the raw string, or a mapped form | an order equal to release order | mapped forms (below) |
| File names (image stamp) | any file system | none of `<>:"/\|?*` or control characters (the Windows reserved set), at most 255 bytes; on FAT the name is a long file name: the 8.3 article at https://en.wikipedia.org/wiki/8.3_filename lists `+` among the characters disallowed in 8.3 names and says "Other characters such as + are changed to the underscore _" when the short alias is generated | full form |

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
4. D is derivable from (L, F, P) only through a release log that records the epoch. A tag object's date can differ from the commit's committer date, a lightweight tag has no date, and a caller can set `SOURCE_DATE_EPOCH`, so the tag alone does not reconstruct the month. Inside a git checkout with such a log D is derived data; in a file name or a sentence that cites a release, D is independent data, because no log travels with it.
5. A rebuild with different content under one (L, F, final) leaves the string unchanged. The rule that closes this: a published final release identifies one artifact set, and a respin takes the next F. The measured tie row shows what the date adds when the rule is broken: forms with D after the fix order the respin later, forms without D compare equal. A development artifact needs more than the month: two commits built in one month under the same (L, F, alpha) share a full string, so a build identifier outside precedence is required (the adopted scheme adds the commit count).

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

No empirical study of calendar versioning or of hybrid identifiers surfaced. arXiv API queries (https://export.arxiv.org/api/query, retrieved with redirects followed): `all:"calendar versioning"` and `all:"semantic versioning" AND all:"calendar"` and `all:"software versioning scheme"` each returned totalResults 0; `all:"version numbering"` (39), `all:"release naming"` (6) and `all:"CalVer"` (6) returned entries from mathematics, astronomy and unrelated fields, none on software versioning schemes (titles read). The Semantic Scholar search endpoint answered HTTP 429, so that index was not searched. The claim is bounded by these arXiv queries, the arXiv search `all:"semantic versioning"` (40 newest entries read) and the citation chains above (I).

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

- G1: the date is trailing metadata, the form of SemVer build metadata (item 10), PEP 440 local labels and RPM caret. Only SemVer ignores it in precedence; dpkg, pacman, RPM and PEP 440 compare it, so two builds of one core order by month there (measured below, always in build order), and the date is a tie-break and never a key.
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

git describe, measured in a scratch repository with the options `Makefile:2236` passes (`--dirty --tags`) plus `--match 'gizmobsd-v*'`: at the tag commit it prints `gizmobsd-vRevA.1-alpha`; one commit later it prints `gizmobsd-vRevA.1-alpha-1-g<hash>` (the hash varies with the scratch repository's commit time), and with a modified file `-dirty` follows. The pattern `^(.+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$` recovers the tag, the count, the hash and the dirty marker.

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

The harness orders 11 of 11 pairs with it (year-and-month: 8 of 11). The first value, 1000001, exceeds the inherited 202608, so a comparison against the old value stays monotone across the rename. Overflow: `long` is signed 32 bits on this target, maximum 2147483647. With F up to 99999 and S up to 9 the low six digits stay below 1000000, so the limit is the letter index: 2146 at most (2146999999 fits, 2147999999 does not). Single and double letters of the 20-letter alphabet reach index 420 (420999999), so overflow needs a third letter, which the grammar (`letters = rl [ rl ]`) does not admit; ASME's own limit is two characters in the 2008 presentation and three in the 2014 draft (retrieval caveats). The integer never needs the date: the build month is already in `version[]` and in the file name.

D93 reads "Year-month probably but really you are working on an interesting string". The measured reason against year-month: the integer orders by build date, and build date breaks under maintenance and cannot separate two fixes in one month. The year-and-month value of the BSD lineage (`BSD 199506` in the NetBSD and FreeBSD sources quoted above, `OpenBSD 202610`) remains the build month in the string.

### Recommendation within the composite analysis: G1 (not adopted)

The grammar is G1, `RevA.1-alpha+202612`: the compatibility letter, the optional fix, the optional stage, and a trailing build month that SemVer precedence ignores (the ecosystems that compare it use it as a tie-break). Three examples: `RevA-alpha+202611` (the first tree state), `RevA.1+202703` (a fix release) and `RevB.2-beta+202708` (a beta of the second fix of the next revision). The recommendation rests on:

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
- The date is the UTC month of `SOURCE_DATE_EPOCH` (the committer date of HEAD when unset). A caller-supplied epoch overrides it, so the string is a function of the commit and the epoch; a release records the epoch it used (the adopted scheme states the policy).
- The compatibility contract binds from the first stable release; alpha and beta states promise nothing (SemVer item 9), so the 64-bit `time_t` change that D89 requires before the first unlabeled release lands inside the pre-release states and does not move the letter.
- A host-tool compatibility check (none exists today; the `sramdump` identity check compares `version[]` bytes) reads `^Rev([A-HJ-NPRT-WY]{1,2})(\.[1-9][0-9]*)?(-(alpha|beta))?$` from `uname -r` and requires an equal letter and a release at or above its minimum under the full order (letter, fix, stage), so `RevA.1-alpha` does not satisfy a minimum of `RevA.1`; a pre-release is accepted only when the tool names it or sets an opt-in. The date never takes part.
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

## Recommendation: S1 (adopted by D111)

The first analysis recommended S1; the owner adopted it (D111) after considering S3. The reasoning and the three falsifiers stand; the first value is now `0.1.0-alpha` (D111 to D115) and "The adopted scheme" above lists what is in force after the answers of 2026-11-19.

S1 states the one claim the tree can verify (the contract list changed), keeps SemVer's honest initial-development band while the ABI is still moving, orders unlabeled release strings identically in every package-version algorithm measured (digits and dots only) and labeled strings alpha < beta < release under SemVer and PEP 440, and costs 4 bytes of flash and 0 of RAM. It carries a written exit criterion so the ZeroVer critique has a measurable answer. S2 is the fallback if the release process becomes calendar-driven. S3 serves hardware-style labels (a PicoCalc board revision) and not the OS series.

Falsifiers, each a count over the first six tagged OS releases:

1. y increments in five or more of the six releases. Then y counts releases, the number carries no more than S2's date, and S2 replaces S1 with the contract list becoming its ABI epoch.
2. Two independent classifications of each release diff against the contract list (the maintainer and one reviewer) disagree on more than one release. Then the list is under-specified and is rewritten before the next tag; disagreement on two further releases moves the series to S3's single-bit rule.
3. A release touching the contract list ships without a y increment. Then the rule is not enforced and a gate comparing the list's files against the bump becomes a prerequisite of the next tag.

Not run: kernel build and linked-kernel text size (remote build host), Renode stages that match the banner, the packaging of a gizmobsd-host 2.0.0 artifact, a dpkg binary comparison, retrieval of the OSDev Wiki and OpenBSD primary pages, and the RetroBSD release numbering.

## Owner decisions

### Decisions recorded

1. Scheme: SemVer 0.y.z with the first tree state `0.1.0-alpha` (D111, superseding D87, D91 and D92).
2. Label placement: the core version with its label, no date, in `version[]` and `osrelease` (D88, D113).
3. Build month: `+YYYYMM` on artifact names and stable releases (D111, D114). The commit count the specification adds for development artifacts is not adopted (D157).
4. Tags: `gizmobsd-v` plus the core verbatim (D90, D115).
5. `KERN_OSREV`: a packed integer generated from the single version source with a compile-time hard error (D112); the encoding is the owner's choice of 2026-11-21 (D204 and the owner's answers of the same day), and the generation mechanism and mutants above and in Appendix C describe what the generator follows, with the stage digit written in the version source in place of a stage list in a tool (D158).
6. Banners in gates and fixtures read `newvers.sh` after the rename (D94).

The questions of the first analysis about the scheme, the label, the tag namespace and the banner literals are answered by these decisions and no longer stand open; for `KERN_OSREV` the direction (a packed integer) is answered and its encoding is chosen (D204 and the owner's answers of the same day) and its value stays at the present one until the generator merges after the rename (D209).

### Residual decisions, answered 2026-11-19

The first analysis left eight residual decisions, R1 to R8, each with options. The owner answered them on 2026-11-19 and asked that the rename stay a rename; the answers below replace the options, and where an answer departs from a recommendation of this note the sections above keep their measurements as analysis only.

| Residual | Answer | Effect on this note |
| --- | --- | --- |
| R1, the contract list that moves MINOR | Amend the list before approving it (D152); the amendments are not yet named | the S1 list is a draft |
| R2, the first stable release and the 64-bit `time_t` change | The tree stays `0.1.0-alpha` until the change lands and then moves to a value such as `0.1.1-alpha`; the owner decides each stage change and `1.0.0` (D153) | supersedes the reading of D89 that `0.1.0` follows the change; no rule in the release cycle of S1 fixes when a stage ends |
| R3, the month a stable release carries | The release month: the month of the release day by wall clock (D154), which can differ from the month the artifact was built | the reproducible-from-tag epoch policy is not adopted |
| R4, a compile-time version macro for programs | None; programs read `kern.osrev` or `uname -r` (D155) | no generated installed header |
| R5, field widths of the packed integer | Decimal; major 0 to 20, minor 0 to 99, patch 0 to 99999; the stage digit comes from the version source (D204 and the owner's answers of the same day); the generator merges after the rename (D209) | the `KERN_OSREV` section and Appendix C describe it, and `kern.osrev` keeps its present value until the generator merges |
| R6, the identifier on development artifacts | None adopted (D157) | the development form `core+YYYYMM.count` is not adopted. |
| R7, the pre-release vocabulary | No fixed list; the owner names each stage when it comes (D158) | tools must not hard-code `alpha` and `beta`; the grammar's label rule and the stage digit of the packed integer are open with R5 |
| R8, a tag at the rename | No tag (D159) | `/etc/release` keeps its present source; the `gizmobsd-v*` namespace (D90, D115) stays reserved |

At the rename the version work is the identity strings alone: `GizmoBSD` and `0.1.0-alpha` in `version[]` and `osrelease` (D111, D113).

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

Tools: python 3.14.9 with packaging 26.3 (PEP 440), pacman 7.1.0 `vercmp`, rpm 6.0.1 (`rpm --eval` with the Lua `rpm.vercmp`), GNU coreutils 9.12 `sort -V`, git 2.56.0 (`git tag --sort=version:refname`, `git check-ref-format`, `git describe`), and Python ports of dpkg's `verrevcmp` and of SemVer 2.0.0 item 11. The dpkg port splits the epoch at the first colon and the revision at the last hyphen, as dpkg's `t-version.c` parse vectors require. Each algorithm is calibrated before a verdict: known-good pairs from its documentation or test suite and a known-bad control that must be refuted (a calibration failure prints and exits non-zero; two scratch mutants, a reversed tilde order in the dpkg port and a reversed strcmp, each exit 1). The block runs as `python3 -I` from any directory, extracted from this note; absent tools (packaging, vercmp, rpm, sort, git) are reported and their rows are not run. The first-analysis pairs (0.a, the pre-release labels) are rows of the same run, so the pacman, RPM and `sort -V` columns are scripted. The run measures the adopted SemVer strings first (13 pairs, three forms, raw and mapped, ties, legality, the packed `KERN_OSREV` integer) and then the considered letter grammars (11 pairs). Every algorithm is filtered by the availability of its tool or library and an absent one is reported as not run; legality and `git describe` need git.

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


# ---- the adopted scheme: SemVer 0.y.z with a stage label, build month and build count -------
SV_STAGE = {"alpha": 1, "beta": 2, None: 9}  # packed-integer stage digit; 0 is reserved


def svrel(major, minor, patch, stage, built, count):
    return {"M": major, "m": minor, "z": patch, "p": stage, "d": built, "n": count}


def svcore(r):
    return "%d.%d.%d%s" % (r["M"], r["m"], r["z"], "-" + r["p"] if r["p"] else "")


SV_GRAMMARS = {
    "S1c 0.1.0-alpha": dict(raw=svcore, meta=lambda r: ""),
    "S1 0.1.0-alpha+202611": dict(raw=lambda r: svcore(r) + "+" + ym(r["d"]),
                                  meta=lambda r: ym(r["d"])),
    "S1d 0.1.0-alpha+202611.3000": dict(
        raw=lambda r: svcore(r) + "+" + ym(r["d"]) + "." + str(r["n"]),
        meta=lambda r: ym(r["d"]) + "." + str(r["n"])),
}


def svmapped(eco, g, r):
    core, meta, p = "%d.%d.%d" % (r["M"], r["m"], r["z"]), g["meta"](r), r["p"]
    if eco == "dpkg":
        return core + ("~" + p if p else "") + ("+" + meta if meta else "")
    if eco == "pacman":
        return core + (p or "") + ("+" + meta if meta else "")
    if eco == "rpm":
        return core + ("~" + p if p else "") + ("^" + meta if meta else "")
    if eco == "pep440":
        return core + {"alpha": "a0", "beta": "b0"}.get(p, "") + ("+" + meta if meta else "")
    return g["raw"](r)


def svosrev(r):  # KERN_OSREV: major, minor, patch and stage in decimal fields
    return r["M"] * 10**8 + r["m"] * 10**6 + r["z"] * 10 + SV_STAGE[r["p"]]


S0, S1, S2 = (2026, 11), (2027, 3), (2027, 10)
SV_CHAIN = [  # expected strictly ascending; each is (label, release state)
    ("0.1.0-alpha", svrel(0, 1, 0, "alpha", (2026, 11), 3000)),
    ("0.1.0-beta", svrel(0, 1, 0, "beta", (2026, 12), 3040)),
    ("0.1.0", svrel(0, 1, 0, None, (2027, 1), 3080)),
    ("0.1.1-alpha", svrel(0, 1, 1, "alpha", (2027, 2), 3120)),
    ("0.1.1", svrel(0, 1, 1, None, S1, 3160)),
    ("0.1.2", svrel(0, 1, 2, None, (2027, 5), 3200)),
    ("0.2.0-alpha", svrel(0, 2, 0, "alpha", (2027, 8), 3300)),
    ("0.2.0", svrel(0, 2, 0, None, S2, 3400)),
]
SV_PAIRS = [(SV_CHAIN[i][0], SV_CHAIN[i][1], SV_CHAIN[i + 1][0], SV_CHAIN[i + 1][1])
            for i in range(len(SV_CHAIN) - 1)]
SV_PAIRS += [
    ("0.1.0-beta same month", svrel(0, 1, 0, "beta", (2027, 1), 3070), "0.1.0", SV_CHAIN[2][1]),
    ("0.1.1-alpha same month", svrel(0, 1, 1, "alpha", S1, 3150), "0.1.1", SV_CHAIN[4][1]),
    ("0.1.9", svrel(0, 1, 9, None, (2028, 1), 4000), "0.1.10",
     svrel(0, 1, 10, None, (2028, 2), 4010)),
    ("0.9.3", svrel(0, 9, 3, None, (2030, 1), 5000), "0.10.0",
     svrel(0, 10, 0, None, (2030, 6), 5100)),
    ("0.99.2", svrel(0, 99, 2, None, (2040, 1), 9000), "1.0.0-alpha",
     svrel(1, 0, 0, "alpha", (2040, 6), 9100)),
    ("0.1.7 built after 0.2.0", svrel(0, 1, 7, None, (2028, 2), 3900), "0.2.0", SV_CHAIN[-1][1]),
]
SV_TIE = (svrel(0, 1, 1, None, S1, 3160), svrel(0, 1, 1, None, (2027, 4), 3190))


def validators():
    def git_tag(s):
        return run(["git", "check-ref-format", "refs/tags/gizmobsd-v" + s]).returncode == 0

    def filename(s):
        name = "gizmobsd-" + s + ".uf2"
        return len(name) <= 255 and not re.search(r'[<>:"/\\|?*\x00-\x1f]', name)

    def dpkg(s):
        return {"ok": "T", "warn": "w"}.get(dpkg_status(s), "F")

    val = {}
    if have("git"):
        val["git tag"] = git_tag
    val["file name"] = filename
    val["dpkg"] = dpkg
    val["pacman"] = lambda s: not re.search(r"[:/\s-]", s)
    val["rpm"] = lambda s: re.fullmatch(r"[A-Za-z0-9._+~^]+", s) is not None
    if Version is not None:
        val["PEP 440"] = lambda s: pep_cmp(s, s) is not None
    val["SemVer"] = lambda s: semver_key(s) is not None
    return val


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


def usable(names):
    """The algorithms whose tools are present; a missing tool is reported, never counted."""
    table = [("dpkg", dpkg_cmp, True), ("semver", semver_cmp, True), ("pep440", pep_cmp, Version),
             ("ascii", ascii_cmp, True), ("pacman", pacman_cmp, have("vercmp")),
             ("rpm", rpm_cmp, have("rpm")), ("sort-V", sortv_cmp, have("sort"))]
    out = [(n, f) for n, f, ok in table if ok and n in names]
    for n, _, ok in table:
        if not ok and n in names:
            print("not run (tool or library absent): " + n)
    return out


def report(title, grammars, pairs, tie, raw_algos, mapped_algos, mapfn, chain, rawfn=None):
    print("\n%s: ascending pairs / pairs both valid, of %d pairs (inv: no valid pair)"
          % (title, len(pairs)))
    print("%-30s" % "grammar" + "".join("%-12s" % n for n, _ in raw_algos))
    fails = {}
    for gname, g in grammars.items():
        row = "%-30s" % gname
        for n, fn in raw_algos:
            ok = inv = 0
            for l1, r1, l2, r2 in pairs:
                got = fn(g["raw"](r1), g["raw"](r2))
                if got is None:
                    inv += 1
                elif got == -1:
                    ok += 1
                else:
                    fails.setdefault((gname, n), []).append(l1 + "<" + l2)
            row += "%-12s" % ("inv" if inv == len(pairs) else "%d/%d" % (ok, len(pairs) - inv))
        print(row)
    print("\n%s, mapped strings, ascending pairs of %d" % (title, len(pairs)))
    print("%-30s" % "grammar" + "".join("%-8s" % n for n, _ in mapped_algos))
    for gname, g in grammars.items():
        row = "%-30s" % gname
        for n, fn in mapped_algos:
            ok = sum(1 for _, r1, _, r2 in pairs if fn(mapfn(n, g, r1), mapfn(n, g, r2)) == -1)
            row += "%-8s" % ("%d/%d" % (ok, len(pairs)))
        print(row)
    print("\nfailing pairs, raw strings")
    for (gname, n), lst in sorted(fails.items()):
        print("  %-30s %-8s %s" % (gname, n, ", ".join(lst)))
    print("\nsame core, later build (a tie in SemVer precedence; 1 would be a defect):")
    for gname, g in grammars.items():
        res = [(n, fn(g["raw"](tie[0]), g["raw"](tie[1]))) for n, fn in raw_algos]
        print("  raw    %-30s %s" % (gname, " ".join("%s=%s" % (n, "inv" if v is None else v)
                                                       for n, v in res)))
        res = [(n, fn(mapfn(n, g, tie[0]), mapfn(n, g, tie[1]))) for n, fn in mapped_algos]
        print("  mapped %-30s %s" % (gname, " ".join("%s=%s" % (n, v) for n, v in res)))
    print("\nexample strings, chain order")
    for gname, g in grammars.items():
        print("  %-30s %s" % (gname, "  ".join(g["raw"](r) for _, r in chain)))
    val = validators()
    print("\nlegality of the raw string: T accepted, F rejected, w accepted with a warning")
    print("%-30s" % "grammar" + "%-10s" % "strings" + "".join("%-9s" % k for k in val))
    kinds = {"unlabeled": [r for _, r in chain if r["p"] is None],
             "labeled": [r for _, r in chain if r["p"]]}
    for gname, g in grammars.items():
        for kind, rs in kinds.items():
            cells = []
            for f in val.values():
                res = {f(g["raw"](r)) for r in rs}
                cells.append("T" if res in ({True}, {"T"}) else
                             "F" if res <= {False, "F"} else "w" if res == {"w"} else "mixed")
            print("%-30s" % gname + "%-10s" % kind + "".join("%-9s" % c for c in cells))
    own = [e for e in ("dpkg", "pacman", "rpm", "pep440", "semver") if e != "pep440" or Version]
    bad = [(e, mapfn(e, g, r)) for g in grammars.values() for _, r in chain for e in own
           if not own_ok(e, mapfn(e, g, r))]
    print("mapped strings rejected by their own ecosystem's rules:", bad or "none")


def describe(tag):  # git describe as Makefile builds /etc/release (--dirty --tags)
    if not have("git"):
        print("\ngit describe: not run (git absent)")
        return
    with tempfile.TemporaryDirectory() as d:
        for c in (["init", "-q"], ["commit", "-q", "--allow-empty", "-m", "a"], ["tag", tag]):
            run(["git", "-C", d] + c, env=GIT_ENV)
        with open(os.path.join(d, "f"), "w") as fh:
            fh.write("1\n")
        for c in (["add", "f"], ["commit", "-q", "-m", "b"]):
            run(["git", "-C", d] + c, env=GIT_ENV)
        base = ["describe", "--match", "gizmobsd-v*", "--tags"]
        at_tag = run(["git", "-C", d] + base + ["HEAD~1"], env=GIT_ENV).stdout.strip()
        clean = run(["git", "-C", d] + base + ["--dirty"], env=GIT_ENV).stdout.strip()
        with open(os.path.join(d, "f"), "w") as fh:
            fh.write("2\n")
        dirty = run(["git", "-C", d] + base + ["--dirty"], env=GIT_ENV).stdout.strip()
        pat = re.compile(r"(.+)-([0-9]+)-g([0-9a-f]+)(-dirty)?")
        print("\ngit describe at the tag:", at_tag)
        for out in (clean, dirty):
            m = pat.fullmatch(out)
            print("git describe:", out, "->", m.groups() if m else "no match")


def main():
    universe = []
    for gs, ps, tie in ((GRAMMARS, PAIRS, TIE), (SV_GRAMMARS, SV_PAIRS, SV_TIE)):
        for g in gs.values():
            for _, r1, _, r2 in ps:
                universe += [g["raw"](r1), g["raw"](r2)]
            universe += [g["raw"](tie[0]), g["raw"](tie[1])]
    universe += ["gizmobsd-v" + s for s in universe] + ["gcal-v1.9", "gcal-v1.10"]
    universe = sorted(set(universe))
    git_plain = GitSort(universe) if have("git") else None
    git_suffix = GitSort(universe, ["-alpha", "-beta"]) if have("git") else None
    if git_plain is None:
        print("not run (git absent): git rows, tag legality, git describe")
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
    raw_algos = usable({"dpkg", "semver", "pep440", "ascii", "pacman", "rpm", "sort-V"})
    if git_plain is not None:
        raw_algos += [("git", lambda a, b: git_plain.cmp("gizmobsd-v" + a, "gizmobsd-v" + b)),
                      ("git+sfx", lambda a, b: git_suffix.cmp("gizmobsd-v" + a, "gizmobsd-v" + b))]
    mapped_algos = usable({"dpkg", "pacman", "rpm", "pep440", "semver"})
    mapped_algos = [x for x in mapped_algos if x[0] != "ascii"]
    earlier_rows([x for x in raw_algos if not x[0].startswith("git") and x[0] != "ascii"])

    # ---- the adopted scheme ----
    report("SemVer 0.y.z raw strings", SV_GRAMMARS, SV_PAIRS, SV_TIE, raw_algos, mapped_algos,
           svmapped, SV_CHAIN)
    ex = svrel(0, 2, 3, "beta", (2027, 12), 3500)
    g2 = SV_GRAMMARS["S1d 0.1.0-alpha+202611.3000"]
    print("\nmapping of", g2["raw"](ex))
    for e in ("dpkg", "rpm", "pacman", "pep440", "semver"):
        print("  %-7s %s" % (e, svmapped(e, g2, ex)))
    print("\nKERN_OSREV = major*10^8 + minor*10^6 + patch*10 + stage (alpha 1, beta 2, final 9)")
    enc = sum(1 for _, r1, _, r2 in SV_PAIRS if svosrev(r1) < svosrev(r2))
    ymi = sum(1 for _, r1, _, r2 in SV_PAIRS if int(ym(r1["d"])) < int(ym(r2["d"])))
    print("  packed integer ascending pairs: %d/%d; year-month YYYYMM: %d/%d"
          % (enc, len(SV_PAIRS), ymi, len(SV_PAIRS)))
    print("  chain:", " ".join("%s=%d" % (n, svosrev(r)) for n, r in SV_CHAIN))
    print("  0.1.0-alpha =", svosrev(SV_CHAIN[0][1]), "; above the inherited 202608:",
          svosrev(SV_CHAIN[0][1]) > 202608)
    mx = svosrev(svrel(20, 99, 99999, None, (1, 1), 0))
    print("  widths major<=20 minor<=99 patch<=99999: largest value", mx, "of", 2**31 - 1,
          "; major 21 reaches", svosrev(svrel(21, 99, 99999, None, (1, 1), 0)))
    print("  minor 100 would add", 100 * 10**6, "to the major field, i.e. collide with major + 1:",
          svosrev(svrel(0, 100, 0, None, (1, 1), 0)) == svosrev(svrel(1, 0, 0, None, (1, 1), 0)))
    describe("gizmobsd-v0.1.0-alpha")

    # ---- the considered and not adopted revision-letter composite ----
    report("Composite letter grammars, raw strings", GRAMMARS, PAIRS, TIE, raw_algos, mapped_algos,
           mapped, CHAIN)
    print("\nZ to AA if Z were allowed (26 letters), raw core strings, -1 is the correct order:")
    print("  " + " ".join("%s=%s" % (n, fn("RevZ.3", "RevAA")) for n, fn in raw_algos
                          if not n.startswith("git")))
    exl = rel(2, 3, "beta", (2027, 12), (2027, 10))
    g1 = GRAMMARS["G1 RevA.1-alpha+202612"]
    print("\nmapping of", g1["raw"](exl))
    for e in ("dpkg", "rpm", "pacman", "pep440", "semver"):
        print("  %-7s %s" % (e, mapped(e, g1, exl)))
    print("\nKERN_OSREV candidates for the letter grammar, pairs ascending out of %d" % len(PAIRS))
    enc = sum(1 for _, r1, _, r2 in PAIRS if osrev(r1) < osrev(r2))
    ymi = sum(1 for _, r1, _, r2 in PAIRS if int(ym(r1["d"])) < int(ym(r2["d"])))
    print("  letter*10^6 + fix*10 + stage:", enc, " year-month YYYYMM:", ymi)
    print("  chain:", " ".join("%s=%d" % (n, osrev(r)) for n, r in CHAIN))
    top = (2**31 - 1 - 999999) // 10**6
    print("  int32 limit: letter index up to", top, "with fix up to 99999; double letters reach",
          len(LETTERS) * (len(LETTERS) + 1), "->", len(LETTERS) * (len(LETTERS) + 1) * 10**6)
    describe("gizmobsd-vRevA.1-alpha")


main()
```

## Appendix C: replay of the KERN_OSREV generator and mutants

A scratch generator that mirrors the `newvers.sh` addition of the adopted scheme, with its static assertions, compiled by `arm-none-eabi-gcc 16.2.0` with the warning and standard flags of `Makefile.rp2040` (`-Wall -Wextra -Werror -Wshadow -Wundef -Wformat=2`, `-std=gnu17`). Each row is calibrated: five good values must build and fifteen mutants must fail, at the stage and with the message listed in the KERN_OSREV section. The last rows measure the consumer stand-in and `vers.o` with and without the generated lines. Run from the repository root; no tree file changes.

```sh
set -eu
SP=$(mktemp -d)
cat > "$SP/gen.sh" <<'EOF'
# gen.sh: validate OSR as SemVer 0.y.z with an alpha or beta label and emit the vers.c lines.
fail() { echo "gen.sh: $1" >&2; exit 1; }
OSR=$1
case $OSR in
    *-alpha) ST=1; PRE=-alpha; CORE=${OSR%-alpha} ;;
    *-beta)  ST=2; PRE=-beta;  CORE=${OSR%-beta} ;;
    *-*|*+*) fail "unsupported label or build metadata in $OSR" ;;
    *)       ST=9; PRE=;       CORE=$OSR ;;
esac
set -f; IFS=.; set -- $CORE; unset IFS; set +f
[ $# -eq 3 ] || fail "$OSR is not major.minor.patch"
for f in "$1" "$2" "$3"; do
    case $f in ''|*[!0-9]*|0?*) fail "field '$f' of $OSR is empty, not numeric or has a leading zero" ;; esac
    [ ${#f} -le 6 ] || fail "field '$f' of $OSR is too long to pack"
done
PACKED=$(($1 * 100000000 + $2 * 1000000 + $3 * 10 + ST))
cat <<EOT
#define OSREV_MAJOR $1
#define OSREV_MINOR $2
#define OSREV_PATCH $3
#define OSREV_STAGE $ST
#define OSREV_PRE "$PRE"
#define OSREV_PACKED ${PACKED}L
#define OSREV_STRING "$OSR"
#define OSREV_STR_(x) #x
#define OSREV_STR(x) OSREV_STR_(x)
_Static_assert(OSREV_MAJOR <= 20, "OSREV major exceeds 20");
_Static_assert(OSREV_MINOR <= 99, "OSREV minor exceeds 99");
_Static_assert(OSREV_PATCH <= 99999, "OSREV patch exceeds 99999");
_Static_assert(OSREV_STAGE >= 1 && OSREV_STAGE <= 9, "OSREV stage outside 1..9");
_Static_assert(OSREV_PACKED == OSREV_MAJOR * 100000000L + OSREV_MINOR * 1000000L +
    OSREV_PATCH * 10L + OSREV_STAGE, "OSREV packed value disagrees with its fields");
_Static_assert(__builtin_strcmp(OSREV_STRING, OSREV_STR(OSREV_MAJOR) "." OSREV_STR(OSREV_MINOR)
    "." OSREV_STR(OSREV_PATCH) OSREV_PRE) == 0, "OSREV string disagrees with its fields");
const char osrelease[] = OSREV_STRING;
const long osrev = OSREV_PACKED;
EOT
EOF
CC="arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu17 -ffreestanding -Wall -Wextra \
-Werror -Wshadow -Wundef -Wformat=2"
try() { # label expected osr [sed-script on vers.c]
  d=$SP/case; rm -rf "$d"; mkdir "$d"
  if ! sh "$SP/gen.sh" "$3" > "$d/vers.c" 2> "$d/err"; then got=generator-rejects
  else
    [ -z "${4:-}" ] || sed -i "$4" "$d/vers.c"
    if $CC -c "$d/vers.c" -o "$d/vers.o" 2> "$d/err"; then
      size=$(arm-none-eabi-size -A "$d/vers.o" | awk '/^\.rodata/ {print $2}')
      got="builds rodata=$size"
    else got="compile-error: $(sed -n 's/.*static assertion failed: //p' "$d/err" | head -1)"; fi
  fi
  case $got in "$2"*) v=ok ;; *) v=WRONG ;; esac
  printf '%-5s %-14s %-17s %s\n' "$v" "$1" "$3" "$got"
}
echo "known-good"
try good-alpha builds 0.1.0-alpha
try good-final builds 0.1.0
try good-beta-max builds 0.99.99999-beta
try good-max-major builds 20.99.99999
try good-1.0.0 builds 1.0.0
echo "mutants that must fail the build"
try major-width compile-error 21.0.0
try minor-width compile-error 0.100.0
try patch-width compile-error 0.1.100000
try label generator-rejects 0.1.0-gamma
try numeric-label generator-rejects 0.1.0-alpha.1
try leading-zero generator-rejects 0.01.0
try metadata generator-rejects 0.1.0+202611
try two-fields generator-rejects 0.1
try four-fields generator-rejects 0.1.0.0
try empty generator-rejects ""
try too-long generator-rejects 0.1.0000000007
try packed-edit compile-error 0.1.0-alpha 's/OSREV_PACKED 1000001L/OSREV_PACKED 1000002L/'
try string-edit compile-error 0.1.0-alpha 's/OSREV_STRING "0.1.0-alpha"/OSREV_STRING "0.1.0-beta"/'
try stage-edit compile-error 0.1.0-alpha 's/OSREV_STAGE 1/OSREV_STAGE 2/'
try minor-edit compile-error 0.1.0-alpha 's/OSREV_MINOR 1/OSREV_MINOR 2/'
echo "stand-in for the KERN_OSREV consumer: immediate (today) against a const long (proposed)"
cat > "$SP/imm.c" <<'EOF'
extern int rd(void *, long);
int f(void *p) { return rd(p, (long)202608); }
EOF
cat > "$SP/var.c" <<'EOF'
extern int rd(void *, long);
extern const long osrev;
int f(void *p) { return rd(p, osrev); }
EOF
for v in imm var; do
  $CC -c "$SP/$v.c" -o "$SP/$v.o"
  printf '%-4s ' "$v"; arm-none-eabi-size "$SP/$v.o" | awk 'NR == 2 {print "text=" $1 " data=" $2}'
done
echo "vers.o of the stock newvers.sh output with OSR=0.1.0-alpha, with and without the osrev lines"
ROOT=$(git rev-parse --show-toplevel)
mkdir -p "$SP/bin" "$SP/stock/PICO"
printf '#!/bin/sh\nprintf "2999\\n"\n' > "$SP/bin/git"; chmod +x "$SP/bin/git"
(cd "$SP/stock/PICO" &&
 sed -e 's/^OST=.*/OST="GizmoBSD"/' -e 's/^OSR=.*/OSR="0.1.0-alpha"/' "$ROOT/sys/conf/newvers.sh" > newvers.sh &&
 PATH=$SP/bin:$PATH SOURCE_DATE_EPOCH=1794700000 sh newvers.sh > vers.c)
cp "$SP/stock/PICO/vers.c" "$SP/stock/vers-plain.c"
{ sh "$SP/gen.sh" 0.1.0-alpha | grep -v '^const char osrelease'
  grep -v '^const char osrelease' "$SP/stock/PICO/vers.c"
  echo 'const char osrelease[] = OSREV_STRING;'; } > "$SP/stock/vers-osrev.c"
for v in plain osrev; do
  $CC -c "$SP/stock/vers-$v.c" -o "$SP/stock/vers-$v.o"
  printf '%-6s ' "$v"; arm-none-eabi-size -A "$SP/stock/vers-$v.o" | awk '/^\.(rodata|data|bss)/ {printf "%s=%s ", $1, $2} END {print ""}'
done
```
