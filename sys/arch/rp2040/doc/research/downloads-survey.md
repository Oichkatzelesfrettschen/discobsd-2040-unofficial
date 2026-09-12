# Survey of the downloads folder for the RP2040 port

Scope: inventory only. Nothing here is ported, extracted into a tracked
tree, or touched on the board. The downloads folder holds about 302 GB in
roughly 350 top-level entries, split between the folder root, a
`1_Completed_Downloads` subtree (about 46 entries, mostly Internet
Archive torrent grabs with `_meta.xml`/`_meta.sqlite` companions), and
`1_Incoming_Incomplete` (one partial download, irrelevant here).

Constraints this survey ranks against, from `STORAGE.md` and
`BOOT-MAP.md`: a 96 KB user process window, about 70 KB free on the root
after the current 64-file image, floating-point `printf` costing about
10 KB per program unless the target opts out, and Smaller C as the only
native compiler (no K&R definitions, no `long long`). The tree already
ships `box`/`sysbox`/`textbox` (three multicall binaries), `awk`, `sed`,
`ed`, the `re` screen editor, and the `smlrc`/`as`/`ld`/`cc` chain, plus
`make`, `diff`, `tar`, `emg` (MicroEMACS), `med` (curses editor), `scm`,
`tclsh`, `basic`, `forth`, `bc`, `dc` sitting built but unshipped in the
tree already (see STORAGE.md, "Left out and available in the tree").
Anything that duplicates one of those is flagged rather than ranked.

Method: `ls -la`, `file`, `du -sh`, and `tar tf` / `unzip -l` / `isoinfo
-l` on each candidate; a handful of small archives were extracted under
`/tmp` to read a LICENSE file or sample a source tree, never into the
downloads folder. Large disk images (`.qcow2`, `.vmdk`, `.flp`, `.dsk`,
`.img`) were listed and identified but not mounted, so their internal
source trees are unverified where noted.

## Top candidates, ranked

| Rank | Item | License verdict | Size | Value / effort |
|---|---|---|---|---|
| 1 | V6/V7 Unix disk images (`titor-special_202112`, `titor-special_torrent`) | PORTABLE (Caldera 2002 ancient-Unix grant) | 4 disk images, a few MB each, inside 7.6 GB / 5.4 GB collections | High value, medium effort |
| 2 | `darwin_0.1` (extracted Apple source drops) | REFERENCE-ONLY (APSL 1.0/1.1) | 53 MB, ~80 small tarballs | High value as spec source, zero copy |
| 3 | Schily tree (`schily-2024-03-21`, esp. `ved`) | Mixed: CDDL/BSD per tool | 6.5 GB (mostly a mirror dump) | Low-medium value, needs sifting |
| 4 | `puzzles-20251220.ecb576f.tar.gz` (Simon Tatham's Portable Puzzle Collection) | PORTABLE (MIT, `LICENCE` file confirmed) | 2.4 MB source | Low-medium value, medium effort |
| 5 | `nano-2.0.9.tar.gz` | REFERENCE-ONLY (GPLv3) | 1.4 MB | Duplicate of `med`/`emg`; low priority |
| 6 | `libevent-2.1.12-stable.tar.gz` | PORTABLE (BSD-3) | 1.1 MB | Wrong layer for this port; skip in practice |
| 7 | `tmux-2.9a.tar.gz` | PORTABLE (ISC/BSD-3) | 500 KB | Wrong shape for a single-console board; skip in practice |
| 8 | `SeL4-src` (zips, `seL4-1.0.3` .. `10.1.1`) | REFERENCE-ONLY (GPLv2 kernel / BSD-2 userland split, `LICENSE_BSD2.txt` confirmed) | 25 MB of zips | Wrong architecture level (microkernel vs. monolithic); reference only |
| 9 | `gnuhurd` (gnumach, hurd, mig, glibc-hurd-pthread) | REFERENCE-ONLY (GPL) | tarballs, tens of MB | Wrong architecture level; `mig`'s IDL idea is the only transferable concept |
| 10 | `mtxinu-modernization-pack.zip` | N/A -- planning docs, not code | 67 KB | Not source; documents a prior modernization plan for a Mach/BSD hybrid, worth reading, nothing to port |

Everything below rank 10 is SKIP: proprietary binary distributions with
no accompanying source, or code at the wrong architecture level for a
96 KB process window.

## PORTABLE (refactor and merge)

**V6/V7 Unix RK05/RL disk images** (`titor-special_202112/unix{0,1,2,3}_v6_rk.dsk`,
`unix_v7_rl.dsk`, plus `AncientUnix.pdf` and the Lions Commentary scan).
Caldera's 2002 license grant covers 32V, V6, and PDP-11 32/V-era Research
Unix and is the ancestor lineage of 2.11BSD, so this is the single
best-licensed source tree in the whole folder for a 2.11BSD-derived
target -- PORTABLE without qualification. Not yet extracted: reading the
disk images needs a PDP-11 filesystem reader (`simh`'s `dumper`, or the
`v6tools`/`ancient-unix` utilities on GitHub); this was not run in this
survey. Two copies exist (`titor-special_202112` and `titor-special_torrent`,
7.6 GB and 5.4 GB); a `diff` of their directory listings shows they are
near-duplicate John Titor ARG collections with the same four Unix disk
images and the same two Unix documents inside a much larger pile of
unrelated OS disk images (Solaris 8 Foundation, misc `.iso`) -- one copy
can be deleted without losing the Unix content once it is extracted.
Estimated a.out size on this target: V6/V7 utilities are K&R C written
for a 16-bit PDP-11 word and will need a real rewrite pass for Smaller
C's no-K&R-definitions rule, but the algorithms (the V6 `cat`, `wc`,
`ed`, small games like `bcd`, `ching`, `quiz`) are tiny by construction
and should land under 2 KB of tree code once relinked against the
current libc. Effort: medium -- extraction tooling first, then a
per-utility rewrite, not a straight compile.

**Simon Tatham's Portable Puzzle Collection** (MIT, `puzzles-20251220.ecb576f/LICENCE`
confirmed). Almost every puzzle assumes a bitmap front end and dynamic
allocation sized to the puzzle parameters; none of that fits a fixed
96 KB window cleanly. The one puzzle whose core logic is small and text-
representable is "Fifteen" (sliding tiles) or "Mines" in ASCII-grid form;
either would need its front end rewritten from scratch against `termios`
raw mode, which the tree does not currently expose to userland programs
in a documented way. Value: a genuinely fun thing to type `cc -o mines
mines.c` for on the board. Effort: medium-high -- the puzzle engine
files (`tree234.c`, `puzzles.c` core, one game's `.c`) are reusable
almost verbatim; the front end is a full rewrite.

## REFERENCE-ONLY (clean-room only, never copied)

**darwin_0.1** (Apple Public Source License 1.0/1.1, `APPLE_LICENSE.txt`
present at the tree root -- APSL is not on the PORTABLE list because its
patent and "Externally Deployed"/attribution terms are not BSD/MIT-
compatible). This is not one archive but roughly 80 already-unpacked
`.tar.gz` drops of individual 4.4BSD-Lite2-derived NeXT/Apple command
groups: `shell_cmds-1.tar.gz` (`apply`, `basename`, and siblings),
`text_cmds-1.tar.gz`, `file_cmds-1.tar.gz`, `basic_cmds-1.tar.gz`,
`bc-1.tar.gz`, `awk-1.tar.gz`. Sampling `shell_cmds` showed real BSD-
lineage C sources with NeXTSTEP `PB.project`/`Makefile.postamble`
scaffolding around them, one utility per directory (`apply/apply.c`,
`basename/basename.c`). Value: `awk`, most of `text_cmds`, and `bc` are
already shipped or already built in the tree per STORAGE.md, so darwin_0.1's
value is in the handful of small BSD utilities the RP2040 root does not
carry yet -- `apply` (run a command once per argument list, a shell
convenience with no floating point and a tiny state machine) is the
standout, plus whatever `basic_cmds` and `file_cmds` turn out to hold on
closer inspection (not fully enumerated in this pass). Clean-room plan
for `apply`: reproduce it from its own `apply.1` man page (a spec, not
the implementation) and POSIX's `xargs` semantics as a cross-check,
write it fresh against the tree's libc, verify argument-splitting
behavior with differential testing against the reference `apply.c`
running on the host (build both, diff stdout for a matrix of argument
counts), and size it with `scc`/`cloc` against the STORAGE.md budget
before it's offered for a box.

**seL4** (GPLv2 kernel components plus BSD-2 userland, per
`LICENSE_BSD2.txt` in the 10.0.0 zip). Wrong layer entirely -- DiscoBSD
is a monolithic 2.11BSD kernel, not a microkernel, and seL4's value here
is conceptual only: its capability-table design is worth reading as an
anti-pattern check when someone eventually proposes capability-style
IPC restrictions for the RP2040 tree, never as code to port. No task
proposed.

**GNU Hurd / GNU Mach / mig** (`gnuhurd`, GPL). Same verdict as seL4 for
the kernel pieces. The one transferable idea is `mig`'s notion of
generating marshalling stubs from an interface description -- already
moot for a tree with no IPC layer at this scale. No task proposed.

**Schily tree** (`schily-2024-03-21`, a 6.5 GB mirror dump of
`mirrors.dotsrc.org` and `sourceforge.net` under Jörg Schilling's
umbrella -- `star`, `cdrtools`, and a small vi-like editor called `ved`
were visible in the top-level listing). Schily's own code is
dual-licensed CDDL/GPL/BSD depending on the specific tool and file, not
uniformly one license, so each tool needs its own check before ranking.
`ved` is the only item here that could plug a gap (a `vi`-shaped editor
smaller than `med`), but the tree already carries `ed`, the `re` screen
editor, `med` (curses), and `emg` (MicroEMACS, public domain) unshipped
-- three editors already cover that ground, so `ved` is a duplicate, not
a gap. This tree was not sifted file-by-file in this pass; if someone
wants to look further, treat every subtree individually, not the 6.5 GB
blob as one license unit.

**nano 2.0.9** (GPLv3). Duplicate of `med`/`emg` for the same reason as
`ved` above. No task proposed.

## SKIP

The following are proprietary, binary-only, or off-topic for a 2.11BSD-
derived embedded target, confirmed by listing rather than by license
research because no source is present to license-check:

- `minix_R3.4.0rc6-d5e4fc0.iso` -- confirmed by `isoinfo`: `/usr/src` on
  this ISO is an empty directory. This is a binary install image, not
  the MINIX source tree, despite MINIX's own BSD-3 license. If MINIX
  source is wanted, it has to come from a source tarball, not this ISO.
- `dell-unix.tar/DELL-UNIX` (SVR4, proprietary) -- a 524 MB disk image
  and a 133 MB `DellSVR4v22.tgz` binary distribution, no source tree.
- `sun386i.tar.bz2` -- SunOS 4.01/4.02 binary floppy images (`.gz` per
  floppy), no source.
- `Mt_Xinu_Mach_386_920331020` -- seven `.7z`/floppy-image bundles
  (Base System, X Window System, Networking, Mach 3.0+DUI, On-Line
  Documents) plus scanned manuals; every floppy is a `.FD1440.dsk`/`.flp`
  binary image. Not mounted in this pass; treat as binary-only until
  someone opens a floppy image and finds otherwise.
- `bsdos-1.1` -- a BSDi `.qcow2.xz` disk image and a `.tbz2` tape backup,
  proprietary (BSDi's license predates the 4.4BSD-Lite settlement), not
  mounted to check for a source tree.
- `opensolaris_starter_kit` (`starter_kit1.iso`, `starter_kit2.iso`) --
  `isoinfo -d` reports `System id: Solaris`; these are OpenSolaris
  installer media (CDDL upstream, but not present as source here), not
  a source dump.
- `aix-4.3.3-with-bull-freeware-archive` -- 7.5 GB of AIX installer
  ISOs (proprietary) plus a 301 MB `433_freeware_2005.tgz` of prebuilt
  GNU binaries for AIX; no portable source, wrong architecture (POWER).
- `sco-xenix-86-286-3860-collection` -- `.7z`/`.rar`/`.zip` floppy-image
  installers for SCO Xenix 86/286, proprietary, binary-only.
- `rvds-2.2-ia.tar` (ARM RealView Development Suite) -- ARM Ltd's
  proprietary Windows toolchain; irrelevant since this port's native
  chain is Smaller C's own Thumb-1 back end.
- `arm-v-3` -- this is the ARM Architecture Reference Manual v3 as a
  scanned PDF (`arm v3.pdf`, OCR text and page-index siblings), not
  source code.
- `applixware-office-v-4.4-4-4-1` -- proprietary Windows/Solaris office
  suite, no relevance.
- `1code-0.0.23.tar.gz` -- an Electron/Bun/TypeScript AI coding
  assistant client (drizzle migrations, a `build/` with `.icns`/`.plist`
  assets). Named in the task's expectation list but on inspection has
  nothing to do with Unix source or embedded targets; flagged and
  skipped rather than silently dropped.
- `mtxinu-modernization-pack.zip` -- ten Markdown/TSV planning documents
  (`MTXINU_MODERNIZATION_AUDIT.md`, `ROADMAP.tsv`, and similar), no code.
  Worth a read for whoever proposed a prior Mach/BSD hybrid plan, but it
  is not a source tree and nothing here is portable by definition.
- The remaining Solaris/Sun items (`sol-*`, `Solaris_2.5.1_x86_Sparc`,
  `solstice_disksuite_4.0_sparc_x86`, `SunWorkShop_5`, `sun-staroffice-8`,
  `Sun_Developer_Software_0802`, `sun-developer-software-8-03-solaris-9`,
  `sun-education-software-edusoft`, `SunSoft_CDware_Sep-Dec_1995`,
  `devpro_v6n1` -- Sun DevPro CDs, not Unix "v6", a name-search false
  positive), `Darwin1.0`/`Darwin0.3CDCaseFront`/`darwin-builder-04-23-2024`
  (`.toast`/`.iso` binary CD images, not source drops the way `darwin_0.1`
  is), `winnt31-347-orig`, `Windows 2000 Professional SP4 Lite.iso`,
  `Microsoft Windows XP SP3 Pro Lite (English) x86.iso`, `NT4SRVWITH6A.ISO`,
  `Codex.dmg`, `Gowin_*` (FPGA toolchain), `Octane_*`/`octane_*` (renderer),
  `nvenc_*`/`NVIDIA-OptiX-*`/`Video_Codec_*` (GPU SDKs), and every general
  Linux/BSD/Windows install ISO (`debian-*`, `ubuntu-*`, `cachyos-*`,
  `FreeBSD-*`, `GhostBSD-*`, `MidnightBSD-*`, `dfly-*`, `lmde-*`,
  `Win11_25H2_*`) are all out of scope by the task's own skip list:
  proprietary binaries, GPU/media tooling, or full distro install media,
  none of it a small-component source tree.
- Everything else at the folder root that is a document, image, video,
  `.docx`/`.pdf`/`.jsonl`/`.tsv`/`.csv`/`.png`/`.jpeg`/`.mov` personal
  file, or an unrelated project archive (`r3v_*`, `mesa-rekit-*`,
  `cayley_infinity*`, `sedenion_*`) belongs to other work entirely and is
  out of scope for this survey.

## What the tree already carries (do not port twice)

- Editors: `ed`, the `re` screen editor shipped; `med` (curses) and
  `emg` (MicroEMACS, public domain) built but unshipped. `ved` (Schily)
  and `nano` (GPLv3) both duplicate this ground and add nothing.
- Awk, sed, text tools: STORAGE.md's `textbox` already covers `cut`,
  `paste`, `seq`, `dirname`, `nl`, `cksum`, `expand`, `unexpand`,
  `uuencode`, `uudecode`, `fold`, `rev`, `comm`; `awk` (62 KB) ships
  separately. Darwin's `awk-1`/`text_cmds-1` drops are reference
  material for gaps in that list only, not a wholesale import.
- Small languages: `bc`, `dc`, `scm` (Scheme), `tclsh`, `basic`, `forth`
  are already built and just not on the shipped image (STORAGE.md,
  "Left out and available in the tree"). No small-language candidate
  found in the downloads folder beats what is already sitting there.
- Native toolchain: `smlrc`/`as`/`ld`/`cc` already ship; nothing in this
  survey (including RVDS) is a candidate replacement or supplement.

## Proposed port tasks (one agent each)

1. **Extract and triage the V6/V7 Unix disk images.** Build or fetch a
   PDP-11 filesystem reader (`simh` tooling or the `ancient-unix`
   GitHub scripts) against `unix0-3_v6_rk.dsk` and `unix_v7_rl.dsk` from
   `titor-special_202112`, list `/usr/source/s` (V6) and its V7
   equivalent, and produce a short catalog of utilities small enough to
   be worth rewriting for Smaller C. Confirm the Caldera license text
   against the actual grant (it is widely mirrored; verify the exact
   wording covers the specific release before treating it as blanket
   PORTABLE). Do not port anything in this task -- catalog and rank
   only, handed to task 2.
2. **Rewrite one or two V6/V7 utilities identified by task 1** against
   the current libc and Smaller C's no-K&R-definitions, no-`long long`
   rules, sized against the STORAGE.md a.out table, and land them in a
   fourth multicall box (STORAGE.md already anticipates a "text tools"
   box; small games or utilities could be a fifth).
3. **Audit `darwin_0.1`'s remaining unsampled tarballs** (`basic_cmds-1`,
   `file_cmds-1`, `misc_cmds-1`, `system_cmds-1`, `mail_cmds-1`,
   `patch_cmds-1`, `bootstrap_cmds-1`, `adv_cmds-1`) for any utility the
   tree lacks that is not already covered by `textbox`/`box`/`sysbox`,
   the same way `shell_cmds/apply` was identified here, and write the
   clean-room reproduction plan (spec source, size budget, differential
   test against the reference binary built on the host) for each finder.
4. **Sift the Schily tree tool-by-tool** (`schily-2024-03-21`, 6.5 GB) to
   separate CDDL from BSD-licensed pieces per file, since it was not
   opened beyond the top-level listing in this survey; report anything
   genuinely novel and small, else recommend deleting the local copy
   once the licensing question is answered (it is a stale SourceForge/
   dotsrc mirror, not original work).
5. **Prototype one Tatham puzzle in ASCII** (Mines or Fifteen) against
   `puzzles-20251220.ecb576f`'s core engine files, replacing the
   bitmap front end with a `termios`-raw-mode text UI, and measure the
   resulting a.out against the 96 KB window before proposing it for the
   root.

Housekeeping, not a port task: delete one of `titor-special_202112` /
`titor-special_torrent` once the V6/V7 disk images are extracted from
it, since `diff` confirms they are near-duplicates and together hold
13 GB for four small disk images' worth of unique content.
