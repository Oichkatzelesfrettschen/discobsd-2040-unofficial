# What the rp2040 image contains, and what redistributing it requires

The question is whether the firmware can be redistributed as UF2 files
and sold pre-installed on a board, and what each act obliges. The
answer is derived from the licenses of what is in the image, read from
the tree, and from the primary sources named below; the port's `NOTICE`
and `/etc/COPYRIGHT` (PR #80) are the artifacts that carry the result.

## Method

The shipped set is the root manifest (`distrib/rp2040/mi.rp2040` and
`md.rp2040`): 40 `pack` executables (seven of them multicall boxes
holding 55 tools), 14 `file` entries, 78 hard links, plus the kernel
and the second-stage boot code in `unix.uf2`. Each source directory
that feeds those was grepped for copyright and license text (counts of
notice-bearing files over source files), and every vendored LICENSE
file was read. Programs in the tree that the rp2040 manifest does not
ship (kilo, tclsh, make, yacc, rogue, monop, bc, dc, lex, retroforth,
pdc, sre, virus, picoc) were left out of the obligation set.

Primary sources:

- The Caldera license letter of January 23, 2002, read from the PDF at
  <https://www.tuhs.org/Archive/Caldera-license.pdf>.
- The DiscoBSD `LICENSE` (BSD 3-Clause, DiscoBSD 2020-2026 and RetroBSD
  2014).
- The University of California's letter of July 22, 1999
  (README.Impt.License.Change), as quoted by
  <https://www.freebsd.org/copyright/license/>: "Effective immediately,
  licensees and distributors are no longer required to include the
  acknowledgement within advertising materials."
- Raspberry Pi trademark rules,
  <https://www.raspberrypi.com/trademark-rules/>.
- SPDX's Caldera entry, <https://spdx.org/licenses/Caldera.html>.
- The GCC Runtime Library Exception 3.1 (the gnu.org page rate-limited
  the fetch; the terms are quoted from the license as shipped with GCC).
- `benchmarks/coremark/LICENSE.md` in the tree: the CoreMark Acceptable
  Use Agreement and the Apache License 2.0.

## Inventory

| Component in the image | Holder | License | Evidence |
| --- | --- | --- | --- |
| kernel (`sys/kern`, `sys/arch/rp2040/rp2040`) | Regents of UC | 1986 Berkeley notice ("The Berkeley software License Agreement specifies the terms") | 48 of 52 and 14 of 15 files carry it |
| port drivers (`sys/arch/rp2040/dev`) | DiscoBSD | ISC permission notice | usb.c, flash.c headers, "Copyright (c) 2026 DiscoBSD" |
| boot2 | Raspberry Pi (Trading) Ltd | BSD-3-Clause (SPDX header) | `boot2_w25q080.S` |
| Dhara | Daniel Beer | ISC | vendored LICENSE |
| heatshrink | Scott Vokes | ISC | vendored LICENSE |
| libc (`gen`, `stdio`, `stdlib`, `string`) | Regents of UC | 1988-1993 Berkeley notices, some with the advertising sentence | 77 of 111 gen files; the rest are V7-descended |
| compiler_rt soft float (`libc/runtime`) | LLVM / UIUC | NCSA or MIT | LICENSE.txt |
| libgcc (linked, not in tree) | FSF | GPL-3 + Runtime Library Exception 3.1 | GCC toolchain |
| sh, ed, sed, awk, find, cpio, dd, tee, du, tr, uniq, sort, grep, echo, kill, sleep, rm, ln, basename, touch, tty, mknod, ps, tar | Regents of UC and, for the V7-descended ones, Caldera | Berkeley notices where present; Caldera for AT&T lineage | `bin/sh` 4 of 29 files, `usr.bin/sed` 0 of 2, `usr.bin/awk` 0 of 10: V7-descended 2.11BSD code |
| login, passwd, su, init, fsck, getty, update, more, head, wc, cmp, date, df, stty, mount, umount, pwd, test, cp, mv, mkdir, chmod, id, env, printf, xargs, uname | Regents of UC | 1980-1988 Berkeley notices | headers |
| look, deroff (in textbox) | Caldera | Caldera license | vendored LICENSE files |
| cut, paste, seq, dirname, nl, cksum, expand, unexpand, uuencode, uudecode, fold, rev, comm (textbox) | sbase contributors | MIT | vendored LICENSE |
| md, menu, resize, keen, fifteen, bubble | DiscoBSD | BSD/ISC (original work, headers say so) | headers |
| stevie | public domain | Unlicense | vendored LICENSE |
| smlrc | Alexey Frunze | BSD-2-Clause | license.txt |
| as, ld | Serge Vakulenko | MIT/X-style notice with a no-advertising clause | headers |
| pdp11 | Schmidt, Cheney (avr11), DiscoBSD additions | WTFPL 2 | COPYING |
| `/usr/v6/root.rk` | Caldera | Caldera license | the pack is V6 binaries |
| coremark | EEMBC | Apache-2.0 plus the CoreMark Acceptable Use Agreement | LICENSE.md |
| `/etc` files, profiles, motd | DiscoBSD | BSD-3 | tree |

No component is GPL. The three GPL mentions in compiled sources are
comments explaining that GPL code was not used (flash.c on FUZIX,
bubble.c on a reference game) or a libgcc pull that was avoided
(doprnt.c).

## Obligations

Two acts are in question. Distributing UF2 files is "redistribution in
binary form". Selling a Pico with the firmware installed is the same act
plus a product.

| Obligation | Source | UF2 files | Pre-installed board |
| --- | --- | --- | --- |
| Reproduce every copyright notice, condition list and disclaimer "in the documentation and/or other materials provided with the distribution" | every BSD, ISC, MIT, Caldera, NCSA text | `NOTICE` alongside the files or in the release page | `NOTICE` in the product documentation; `/etc/COPYRIGHT` on the root as the on-device material |
| Acknowledgement in advertising: "This product includes software developed or owned by Caldera International, Inc." | Caldera license, clause 3 | a release page that describes features is advertising: include it | the listing text must carry it |
| Acknowledgement of UC Berkeley in advertising | 1988 Berkeley notice | withdrawn 22 July 1999; reproduce the notice itself, add the sentence as courtesy | same |
| No endorsement by the holders' names | every license | do not call it "Raspberry Pi's UNIX", "Caldera's", "Berkeley's" | product name and copy avoid it |
| CoreMark trademark only with unmodified benchmark, revocable | EEMBC AUA 1.1, 1.2, 1.4 | acceptable for a development image | drop `pack /usr/bin/coremark` from a product image, or comply and display the trademark notice |
| Apache-2.0 NOTICE and attribution for CoreMark | Apache 2.0 section 4 | covered by `NOTICE` section 13 | same |
| GCC runtime exception: binaries from an eligible compilation process | GCC RLE 3.1 section 1 | satisfied (GCC, no proprietary plugin) | same |
| "Raspberry Pi" and "Pico" not in the product name; referential wording; logo only on a genuine-product listing; commercial use of the marks otherwise needs a license | Raspberry Pi trademark rules | not applicable | the listing says "runs on a Raspberry Pi Pico"; the product is a genuine Pico resold with software, which the rules describe as not conferring branding rights |
| UNIX is a trademark of The Open Group | Caldera letter footnote | describe as "2.11BSD-derived" | same |
| Source availability | none of these licenses requires it (no copyleft) | state the commit or tag the image was built from, since the tree is public anyway | same, in the listing |

## What the research settled and what it did not

- 2.11BSD's AT&T-descended code is redistributable under the Caldera
  license: the letter covers "16 bit UNIX Versions 1-7" and 32V and the
  right "to use, modify and distribute ... including creating derived
  binary products"; 2.11BSD's V7 lineage is what TUHS distributes under
  it. The 2BSD tree at TUHS states 2.11BSD "can be freely used under the
  Caldera license" (wfjm.github.io/home/211bsd, which mirrors the TUHS
  position).
- Wikipedia's Ancient UNIX article records that "concerns have been
  raised regarding the validity of the Caldera license" over who owned
  the copyrights at the time. Every distributor of 2.11BSD, V6 and V7
  binaries, TUHS and the SIMH kits included, relies on the letter; this
  project does the same, and the notice reproduces it. The residual risk
  is the same one the whole retrocomputing field carries, not one this
  project adds.
- The Caldera advertising clause is live. Unlike the Berkeley clause, it
  was never withdrawn, so a Tindie listing that mentions the shell, the
  editor or V6 needs the sentence.
- CoreMark is the one component whose terms a product image would
  rather avoid: a trademark license that is revocable and forbids the
  name on a modified benchmark. Removing it from a product image is a
  one-line manifest change and frees about 20 KB of root.
- Tindie's terms (https://www.tindie.com/about/terms/, effective June
  30, 2026, operator EETREE LLC of Washington; the site sits behind a
  Cloudflare challenge, fetched through headless Firefox on 2026-09-16)
  add three seller undertakings beyond the licenses: section 6, sellers
  "must own or be authorized to sell each product and must provide
  accurate photos, descriptions, specifications" and "are responsible
  for product safety, labeling, warranties they offer, ... consumer laws,
  intellectual-property rights"; section 13, a representation that
  listed content "does not infringe another party's rights"; section
  18, an indemnity of EETREE LLC for claims "arising from your products,
  listings, ... infringement". Section 7 lets Tindie remove "infringing"
  or "deceptive" products. The terms say nothing about third-party
  software licenses or exclusivity and name no governing law. The
  warranty and consumer-law exposure is the seller's alone.
- CoreMark's core sources are byte-identical to EEMBC's upstream main
  (MD5 of core_main.c, core_list_join.c, core_matrix.c, core_state.c,
  core_util.c and coremark.h compared on 2026-09-16); only the porting
  layer core_portme.c and core_portme.h differ, which CoreMark's run
  rules reserve for the port. The benchmark is unmodified in the
  Acceptable Use Agreement's sense, so the program may keep its name
  and the listing may say it runs CoreMark, with the trademark notice.
  The legal memorandum (legal-memo-redistribution.md) carries the full
  analysis.
- This is an engineering reading of the license texts, not legal
  advice; a product launch deserves a lawyer's hour over `NOTICE`.

## What was done

PR #80 in the port adds `NOTICE` (every text above), `/etc/COPYRIGHT`
on the root (one block, the short form with both acknowledgement
sentences and the repository URL), the README section "License and
redistribution" with the six steps for a redistributor, and changes
"the board arrives flashed" to say whose boards do.
