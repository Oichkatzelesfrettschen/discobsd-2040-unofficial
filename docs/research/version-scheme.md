# Version numbering for GizmoBSD: what the tree encodes, what others publish, and three schemes

Source reading at origin/main 32927ed0 (merge of #620); the text is a research record. Compiler for every size: arm-none-eabi-gcc 16.2.0 (local; CI uses 14.2.1, and sizes from the two compilers are not subtracted). Published pages were retrieved with curl on 2026-11-16 and every quotation below was matched with grep against the retrieved text; an item read only through a summarizing fetch or a search result is marked (S) and carries no quotation. Inferences are marked (I). The owner decisions this note answers are quoted inline where used: D51, the owner's request of 2026-11-16 ("GizmoBSD 0.a or ... do we use 'Rev A' to get started? Research and figure out what to do"); D52, a GizmoBSD param.h macro carrying the version D51 decides, beside the kept DiscoBSD, RetroBSD and 2.11BSD lineage macros; D38, host package gizmobsd-host 2.0 with discobsd-* command aliases for one release; and the rename plan's proposals D3 (version "GizmoBSD 0.1", with "derived from DiscoBSD 2.7" in etc/COPYRIGHT and motd) and D4 (a GizmoBSD 202611 value returned by KERN_OSREV).

## Findings

1. The tree uses neither SemVer nor a "Rev" letter. It carries five identities: the upstream DiscoBSD release counter (`2.7`), a year-and-month integer inherited from 4BSD (`202608`), a build identity (commit count and compile count), a root-image stamp from `git describe`, and a host-package number (`1.0.9`) that is SemVer-shaped. Each is set in a different file and no file states how they relate.
2. Owner decision D51 asks for a choice among "0.a", "Rev A" and a SemVer-style number. Plan decision D3 proposes "GizmoBSD 0.1". The string "0.1" is not a valid SemVer 2.0.0 version, which requires three numeric fields (item 2). "0.a" is invalid under SemVer and sorts in opposite directions across package-version algorithms (measured below). "Rev A" breaks the field split in `sys/conf/newvers.sh` (measured below).
3. The inherited release cycle (`sys/conf/newvers.sh:18-20`) keeps the release number after a release and appends "-current". Under SemVer precedence (item 11) "0.1.0-current" sorts below "0.1.0", opposite to its chronology. A SemVer adoption changes that cycle.
4. Firmware identity reads the host-tool tag. `git describe --tags origin/main` prints `host-v1.0.7-2660-g32927ed0`, and `Makefile:2236` writes that string into `/etc/release`.
5. The string-length cost of every candidate is at most 16 bytes of flash `.rodata` and 0 bytes of RAM in `vers.o` (arm-none-eabi-gcc 16.2.0; linked kernel not run).
6. The recommendation is scheme S1 below: SemVer 2.0.0 in its initial-development band (0.y.z), three fields, with an enumerated contract list that decides when y increments, the year-and-month integer kept in `KERN_OSREV`, and a separate tag namespace for OS releases.

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
| 0.1.0-current against 0.1.0 | lower | invalid | not run |

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

- Rules. Three numeric fields. While X is 0, y increments when an item in the contract list changes incompatibly, z increments for compatible fixes, and a label (`-alpha` on trunk, `-beta` before the cut) follows SemVer item 9. The list (I): the a.out user ABI (header and magic, system-call numbers and argument layouts, layouts and fundamental-type widths in installed headers, the installed libc interface whatever its linkage: declarations in the installed headers and the function and data symbols with their calling conventions, which are `/usr/lib/libc.a` today (`distrib/rp2040/mi.rp2040:350`; the image ships no shared libc) and a shared libc if one is added (`docs/research/xip-shared-libc` is a design, not a shipped object). A removed export or an incompatible declaration breaks the rebuild of existing programs, so it moves y and a z increment cannot carry it); on-disk and flash formats (partition map, root-file-system layout, swap); the host-visible wire (USB identity, console framing, the sysctl names the host tools read). 1.0.0 is cut when the list has not changed incompatibly across two consecutive releases and the host-link wire is documented (I).
- Release cycle. After a release the trunk moves to the next planned version with `-alpha` (0.2.0-alpha after 0.1.0), the cut candidate carries `-beta` (0.2.0-beta), and the release carries no label (0.2.0). SemVer item 11 orders these 0.1.0 < 0.2.0-alpha < 0.2.0-beta < 0.2.0, and PEP 440 agrees (measured below). The label `-dev` is excluded: ASCII order puts it above `-beta` under item 11 and below it under PEP 440. This replaces the inherited "keep the same number, append -current" step (`newvers.sh:18-20`), whose "0.1.0-current" sorts below 0.1.0.
- First value. `GizmoBSD 0.1.0`, with `etc/COPYRIGHT` and motd stating "derived from DiscoBSD 2.7" (plan D3).
- param.h. `GizmoBSD 202611` unchanged in role, returned by `KERN_OSREV` (plan D4); `GizmoBSD_0_1 1` joins `DiscoBSD_2_7`, which plan D4 keeps. The integer is monotone across the rename (202611 above 202608), which the string ordering "0.1.0" below "2.7" is not.
- Host package. Independent SemVer. `gizmobsd-host 2.0.0` (D38) stays; its README names the OS contract range it speaks ("GizmoBSD 0.1.x console and link wire"). OS releases tag as `gizmobsd-v<version>` and the root-image stamp calls `git describe --match 'gizmobsd-v*'`; `host-v*` keeps its meaning.
- Answers to the critiques. Hickey and Hynek: the number claims one thing, that the contract list changed, and 0.y.z states in the specification that anything may change. Hyrum: only the listed observables are promised. ZeroVer: the 1.0.0 criterion is written down. Go and Cargo read 0.y.z as y breaking, the same reading the list gives.
- Tree changes. `newvers.sh` (OST, OSR, cycle comment, and the awk step so `$S` reaches `osrelease` if pre-release builds must differ under `uname -r`); `sys.mk` (an `OSPATCH` variable, with `OSREV`, `OSRev` and `OSrev` built from three fields); `param.h`; `Makefile:2236` (--match); the literal banner in 7 gate and fixture files, 3 shipped text files and 3 documents (listed in the tree table); the picofetch fixture `usr.bin/picofetch/tests/hwinfo_fixture.c:85-86`, which holds the type and release as separate strings, so `render.py:242` and the fixture move together; package metadata maps the label with each format's own syntax. The banner edits occur in the rename in any case, because the name changes.
- Cost. +4 bytes flash `.rodata` and 0 bytes RAM for the unlabeled release string; a label in the version string adds 6 bytes of flash (`-alpha`), and a label also in `osrelease` adds 6 more.

### S2: CalVer, YYYY.MM

- Rules. The version is the release month. Compatibility is carried by a separate integer, the ABI epoch, held in a file the image ships (`/etc/release` or a sibling) and incremented when the contract list of S1 changes incompatibly; calver.org's own guidance pairs the date with a changelog and a deprecation policy.
- First value. `GizmoBSD 2026.11` (or the release month), ABI epoch 1.
- param.h. `GizmoBSD 202611`; the digits now equal the printed version, removing today's pair of "2.7" and 202608. `DiscoBSD_2_7` has no replacement. `KERN_OSREV` = 202611.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the number never claims compatibility. ZeroVer: no 0.x band. Cost of the answer: the printed version says nothing about a break, so a user needs the epoch.
- Tree changes. As S1 (`OSPATCH` unneeded); the cycle comment changes to name a month, and `OSR` becomes a second date next to the date already in the `version` string.
- Cost. +8 bytes flash `.rodata`, 0 bytes RAM. The release month is a property of the day a release is cut; the upstream cadence (Feb and Aug) fits, a fork cadence is unknown (I).

### S3: Revision letter, hardware style, "Rev A"

- Rules. One uppercase letter names a change-controlled configuration. The letter advances when the contract list of S1 changes incompatibly; fixes inside a letter are identified by the build identity (`#N`, commit count). 26 letters bound the series; a continuation (AA) is a rule to add.
- First value. `Rev A` in the `version` string, written `RevA` in `osrelease`.
- param.h. `GizmoBSD_REV 1` (A is 1) returned by `KERN_OSREV`. This loses the year-and-month value of D4 in that sysctl; the date moves to the build stamp. `sys/sys/sysctl.h:129` names `KERN_OSREV` "system revision", which the letter fits literally.
- Host package. Independent SemVer, as in S1.
- Answers to the critiques. Hickey and Hynek: the letter claims one bit and says so. Hyrum: the same contract list. ZeroVer: no 0.x band. Cost: no ordering of magnitude, no pre-release vocabulary, and tools that sort versions treat letters as in the table above.
- Tree changes. As S1, plus a second value in `newvers.sh`: a display value that `version[]` prints (`Rev A`) beside the unspaced `OSR` (`RevA`) that `osrelease[]` and `osversion[]` consume. The scratch variant changes four lines of the script (Appendix A.2); a delimiter change alone would put the space in both strings.
- Cost. +3 bytes flash `.rodata` for the specified split, +4 with `Rev A` in both strings, 0 bytes RAM. The earlier +2 measured `RevA` in both strings, which is neither form.

### The owner's two candidates by name

- "GizmoBSD 0.a". Rejected as an OS version. SemVer item 2 requires numeric fields; PEP 440, pacman, and sort -V place it below "0.1" while the dpkg algorithm places it above (measured above); the alphabet bounds the series. The intent behind it, a pre-release signal, is expressed by S1 with `0.1.0-alpha`.
- "Rev A". Retained as S3 and not recommended as the series. It has a hardware precedent (the tracked datasheet table, and the RP2040 stepping names), and the RP2040 case shows the weakness: the silicon keeps a letter label for people and a small integer for software, and the integer cannot separate B0 from B1. A letter alone leaves software without an ordered value; S1 keeps a letter-free integer in `KERN_OSREV`.

## One scheduled break through each scheme

The 64-bit `time_t` at the user interface is an incompatible user-ABI change (the owner chose the 64-bit interface, recorded at `docs/research/time-representation-past-2038.md:147`).

| Scheme | Release before | Release after | What a user learns |
| --- | --- | --- | --- |
| S1 | 0.1.0 | 0.2.0 | y moved: the contract list changed; recompile programs |
| S2 | 2026.12 (epoch 1), illustrative | 2027.01 (epoch 2), illustrative | the month moved always; the epoch file carries the break |
| S3 | Rev A | Rev B | the letter moved: the contract list changed |

## Recommendation: S1

S1 states the one claim the tree can verify (the contract list changed), keeps SemVer's honest initial-development band while the ABI is still moving, orders unlabeled release strings identically in every package-version algorithm measured (digits and dots only) and labeled strings alpha < beta < release under SemVer and PEP 440, and costs 4 bytes of flash and 0 of RAM. It carries a written exit criterion so the ZeroVer critique has a measurable answer. S2 is the fallback if the release process becomes calendar-driven. S3 serves hardware-style labels (a PicoCalc board revision) and not the OS series.

Falsifiers, each a count over the first six tagged OS releases:

1. y increments in five or more of the six releases. Then y counts releases, the number carries no more than S2's date, and S2 replaces S1 with the contract list becoming its ABI epoch.
2. Two independent classifications of each release diff against the contract list (the maintainer and one reviewer) disagree on more than one release. Then the list is under-specified and is rewritten before the next tag; disagreement on two further releases moves the series to S3's single-bit rule.
3. A release touching the contract list ships without a y increment. Then the rule is not enforced and a gate comparing the list's files against the bump becomes a prerequisite of the next tag.

Not run: kernel build and linked-kernel text size (remote build host), Renode stages that match the banner, the packaging of a gizmobsd-host 2.0.0 artifact, a dpkg binary comparison, retrieval of the OSDev Wiki and OpenBSD primary pages, and the RetroBSD release numbering.

## Owner decisions

1. Adopt S1, S2 or S3 as the OS version scheme (D51), and whether a pre-release label belongs inside `osrelease` (+6 bytes of flash over the version-string-only form) or only in the `version` string as today.
2. Approve the contract list as the rule for incrementing y, and its 1.0.0 criterion.
3. Approve a separate OS tag namespace (`gizmobsd-v*`) and the `--match` change at `Makefile:2236`, so the firmware stamp stops naming the host tag.
4. Decide whether the literal banner in the 7 gate and fixture files and `usr.bin/picofetch/tests/hwinfo_fixture.c` read the release from `newvers.sh` (a calibrated follow-on gate change), so a later bump edits one file.
5. Decide the `KERN_OSREV` value: the year-and-month integer (S1, S2; plan D4) or the revision index (S3).

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

## Appendix B: replay of the ordering measurements

```python
# python3 -I; the packaging library (PEP 440) is optional and reported when absent.
import re
def order(c):
    if c == "~": return -1
    if c.isdigit(): return 0
    if c.isalpha(): return ord(c)
    return ord(c) + 256
def verrevcmp(a, b):  # port of dpkg lib/dpkg/version.c verrevcmp
    i = j = 0
    while i < len(a) or j < len(b):
        first = 0
        while (i < len(a) and not a[i].isdigit()) or (j < len(b) and not b[j].isdigit()):
            ac = order(a[i]) if i < len(a) else 0
            bc = order(b[j]) if j < len(b) else 0
            if ac != bc: return ac - bc
            i += 1; j += 1
        while i < len(a) and a[i] == "0": i += 1
        while j < len(b) and b[j] == "0": j += 1
        while i < len(a) and a[i].isdigit() and j < len(b) and b[j].isdigit():
            if not first: first = ord(a[i]) - ord(b[j])
            i += 1; j += 1
        if i < len(a) and a[i].isdigit(): return 1
        if j < len(b) and b[j].isdigit(): return -1
        if first: return first
    return 0
def dcmp(a, b):
    r = verrevcmp(a, b); return (r > 0) - (r < 0)
def semver_key(s):  # SemVer 2.0.0 item 11 (build metadata ignored)
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$", s)
    if not m: raise ValueError("not SemVer 2.0.0: " + s)
    core = tuple(int(x) for x in m.groups()[:3]); pre = m.group(4)
    ids = None if pre is None else [(0, int(i), "") if i.isdigit() else (1, 0, i) for i in pre.split(".")]
    return core, ids
def scmp(a, b):
    (ca, ia), (cb, ib) = semver_key(a), semver_key(b)
    if ca != cb: return -1 if ca < cb else 1
    if ia is None and ib is None: return 0
    if ia is None: return 1
    if ib is None: return -1
    if ia == ib: return 0
    for x, y in zip(ia, ib):
        if x != y: return -1 if x < y else 1
    return -1 if len(ia) < len(ib) else 1
def check(label, got, want): print(("ok  " if got == want else "FAIL") + " " + label)
# calibration: Debian Policy 5.6.12 examples (known-good) and a reversed pair (known-bad)
for a, b in [("1.0~beta1~svn1245", "1.0~beta1"), ("1.0~beta1", "1.0"), ("1.0", "1.0a"),
             ("~~", "~~a"), ("~~a", "~"), ("~", ""), ("", "a")]:
    check("dpkg " + a + " < " + b, dcmp(a, b), -1)
check("dpkg control 1.0a < 1.0 is false", dcmp("1.0a", "1.0") == -1, False)
# calibration: SemVer item 11 example chain (known-good) and 1.0.0 < 1.0.0-alpha (known-bad)
chain = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
         "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"]
for a, b in zip(chain, chain[1:]): check("semver " + a + " < " + b, scmp(a, b), -1)
check("semver control 1.0.0 < 1.0.0-alpha is false", scmp("1.0.0", "1.0.0-alpha") == -1, False)
# results quoted in the text
print("dpkg 0.a vs 0.1:", dcmp("0.a", "0.1"), "(1 means higher)")
print("dpkg 0.a vs 0.b:", dcmp("0.a", "0.b"), " 0.9 vs 0.10:", dcmp("0.9", "0.10"))
for a, b in [("0.2.0-alpha", "0.2.0-beta"), ("0.2.0-beta", "0.2.0-dev"), ("0.2.0-alpha", "0.2.0"),
             ("0.1.0-current", "0.1.0")]:
    print("semver", a, "vs", b, scmp(a, b))
try:
    from packaging.version import Version, InvalidVersion
    def p(s):
        try: return Version(s)
        except InvalidVersion: return None
    for s in ["0.a", "0.1", "0.2.0-alpha", "0.2.0-dev", "0.1.0-current"]:
        print("pep440", s, "->", p(s))
    print("pep440 order of 0.a 0.b 0.1 0.2:", [str(v) for v in sorted(map(Version, ["0.2", "0.1", "0.b", "0.a"]))])
    print("pep440 0.2.0-beta > 0.2.0-dev:", p("0.2.0-beta") > p("0.2.0-dev"))
except ImportError:
    print("packaging not installed: PEP 440 rows not run")
```

The pacman and sort -V rows come from `vercmp A B` (pacman) and `printf '0.1\n0.a\n0.b\n0.10\n0.9\n' | sort -V`; both were run once by hand and are not scripted.
