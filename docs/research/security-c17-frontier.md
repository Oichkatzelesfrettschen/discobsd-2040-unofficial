# Shipped security and C17 frontier

This audit binds its source conclusions to
`dbd8bf0cc84f3705b79e3a13c79436b0ba51cb54`. It covers programs and
libraries reachable from the RP2040 manifest, plus source-only utilities when
a public CVE names their mechanism. A source match establishes exposure; a
name or version match only nominates a review. Host sanitizers establish
memory behavior for the exercised inputs. Cortex-M0+ compilation, final a.out
inspection and filesystem accounting establish the target footprint. Board
behavior remains outside this audit.

Structural Graft job `527d63e861f3851b3aea15c224869cb0` supplied the first
symbol and file map. The graph matches the source commit but carries zero
semantic-ready records, and the separate deep cache covers only
`sys/arch/rp2040/dev` at an older commit. Every disposition below therefore
comes from live source, the manifest, a behavior probe or a public CVE record.

## Search and attack surface

The shipped manifest exposes three high-value classes:

- `login`, `passwd` and setuid `su` cross account and privilege boundaries.
- `tar`, `cpio` and `compress` parse files that can originate outside the
  device. `tar -z` and `tar -Z` feed archives through `compress` over a pipe.
- `fsck`, executable loading, syscall copy paths, USB control requests, Dhara
  metadata and heatshrink streams consume persistent or host-controlled data.

The bounded source search was:

```sh
rg -n -i 'comprexx|unlzw|apply_substitution|pw_error' usr.bin bin lib
rg -n 'dhara_set_error|\*err[[:space:]]*=' sys/arch/rp2040
rg -n 'pack /(bin/tar|usr/bin/(cpio|compress|login|passwd|su|chpass))' \
  distrib/rp2040/mi.rp2040
```

The public-record check read the CVE List V5 records for each identifier on
2026-10-03. The stable record form is
`https://www.cve.org/CVERecord?id=CVE-YYYY-NNNN`.

## CVE dispositions

| Identifier | Disposition | Repository evidence | Falsifier or next gate |
| --- | --- | --- | --- |
| CVE-2006-1168 | applicable; fixed in this migration unit | The shipped LZW decoder followed attacker-controlled prefix links and wrote beyond the overlaid `htab` stack. The target-width fixture below reproduces a global buffer overflow in the base source under AddressSanitizer. | The fixed decoder accepts the fixture, leaves partial named output, or the calibrated unbounded mutation stops overflowing. |
| CVE-2001-1413 | named mechanism absent | The record names `comprexx`; the tree contains neither that symbol nor its filename-processing path. The shipped `compress` frontend has a separate bounded-name review. | A donor or binary inspection resolves a reachable `comprexx` equivalent in the shipped executable. |
| CVE-2010-0001 | implementation and platform condition absent | The record names gzip `unlzw.c` before 1.4 on 64-bit platforms. The manifest ships the independent `usr.bin/compress/compress.c` decoder on 32-bit ARM. | A source/data-flow comparison proves the same underflow in this decoder at ILP32 width. |
| CVE-2015-8915 | implementation absent | The record names libarchive `bsdcpio`; the image ships the tree's compact POSIX odc implementation under `usr.bin/cpio`. | A libarchive object enters the image or a crafted odc archive triggers an equivalent invalid read in the local parser. |
| CVE-2025-60753 | implementation absent | The record names libarchive bsdtar `apply_substitution` and `-s`. The local `bin/tar` has neither mechanism. | The local option parser gains substitution rules or a call edge resolves to equivalent unbounded allocation. |
| CVE-2026-10659 | affected Zephyr adapter absent | The record names Zephyr `drivers/disk/ftl_dhara.c` writing through a null error pointer. Every RP2040 flash callback routes errors through null-safe `dhara_set_error`. | A callback writes `*err` without first proving the pointer non-null, including a generated or alternate build path. |
| CVE-1999-1471 | shipped mechanism absent; source review retained | The shipped `passwd` changes the password hash and has no shell or GECOS input. `chpass`, which owns those fields, stays outside the RP2040 manifest and already bounds its aggregate GECOS buffer. | Shipping `chpass`, or finding an unbounded shell/GECOS copy in a setuid image path, reopens the row. |
| CVE-2000-0993 | helper absent | The affected BSD libutil `pw_error` format-string helper has no source or call site in the tree. | A linked object exports `pw_error`, or another password-database error path treats user data as a format string. |

NVD keyword queries returned zero records for `TinyUSB` and `heatshrink` on
2026-10-03. That result is a name-search receipt, not a clean bill of health:
aliases, downstream adapters and newly published records can evade it.
Heatshrink is pinned to `7d419e1fa4830d0b919b9b6a91fe2fb786cf3280`
(0.4.1 plus one commit). The USB implementation needs source-level control
request review independently of the product-name result.

## LZW memory-safety repair

The base source overflows `htab` with this 12-bit stream, shown in hexadecimal:

```text
1f 9d 8c 61 02 0a 1c 5d cc a0 c1 83 08 13 22 54 00
```

The original loop trusted every decoded prefix and advanced the output stack
without a bound. The repair enforces four independent invariants:

1. A code lies at or below the one permitted KwKwK frontier.
2. A dictionary reference lies below `free_ent`.
3. Every prefix strictly decreases, so a chain terminates at a literal.
4. Every stack write remains below the physical end of `htab`.

The decoder now rejects invalid magic, truncated headers and code widths
outside 9 through 12 before shifting `1` by the header-controlled value.
Corrupt named input keeps the archive, removes partial output and exits
nonzero. The regression
gate removes all four dictionary/stack checks from a generated mutation and
requires AddressSanitizer to reproduce the `htab` overflow, which calibrates
the test against a known-bad implementation rather than only observing the
fixed path.

## Why the translation unit was not C17-clean

The file predated prototypes and depended on compiler extensions and platform
assumptions that GNU17 still tolerated:

- K&R definitions and empty parameter lists left argument types outside the
  function declarator. DEBUG-only definitions preserved the same defect even
  when the production form happened to compile.
- Implicit declarations, the dormant local `rindex`, external linkage for
  internal tables and `register` ordering folklore described VAX compilers
  rather than the RP2040 implementation.
- SIGSEGV acted as malformed-input detection after an unchecked memory access.
  A fault handler cannot turn undefined behavior into input validation.
- Unbounded filename copy/append operations, suffix pointer underflow,
  unchecked allocation and `stat`, an uninitialized overwrite response and a
  signed `fwrite` comparison violated bounded ownership or type contracts.
- The bit-output buffer used signed `char`, DEBUG format strings disagreed
  with the 16-bit configuration's `long` code type, and the unrolled hash
  clear obscured its array bound from static analyzers.
- One timestamp member remained uninitialized because the code assigned
  `timep[0].tv_usec` twice.

The migration gives every definition a full prototype, uses descriptive
locals, limits internal symbols to `static`, uses an unsigned byte buffer,
replaces the unrolled clear with one bounded loop, validates all decoder and
filename boundaries, and promotes `usr.bin/compress` from `WARNLEVEL=legacy`
to `WARNLEVEL=full`. Production and DEBUG forms also accept strict C17 with
`-Wall -Wextra -Werror -Wpedantic` on the host.

## Size, RAM and runtime evidence

The before and after objects use the exact Cortex-M0+ flags selected by the
RP2040 build. The final a.out pair uses one unchanged crt0, linker script and
libc. `hsaout -s` applies the root allocation formula to each file.

| Surface | Base | Repaired | Delta |
| --- | ---: | ---: | ---: |
| Source lines | 1,372 | 1,393 | +21 |
| Source bytes | 36,841 | 36,517 | -324 |
| Object text | 4,657 | 4,814 | +157 |
| Object data | 56 | 32 | -24 |
| Object BSS | 30,220 | 30,216 | -4 |
| Object total | 34,933 | 35,062 | +129 |
| Final a.out bytes | 13,452 | 13,572 | +120 |
| Packed bytes | 11,168 | 11,243 | +75 |
| Packed root blocks | 12 | 12 | 0 |

The source file sheds 324 bytes while adding 21 lines because Git already
retains the removed 1984--1985 RCS chronology and the repair adds executable
checks. Removing the log also restores the repository rule that source
comments describe current mechanisms. The writable target image sheds 28
bytes across data and BSS; the one-process resident total grows by 129 bytes.
The decoder stack still overlays `htab`, so the repair adds no second arena or
heap allocation.

Thirty warmed host runs over the 1,012,736-byte generated root image measured
16.8 ms mean before and 16.9 ms after with 0.1 ms standard deviation for both.
That host result detects no meaningful throughput change. It does not measure
Cortex-M0+ cycles or a board peak-water mark.

## Ranked residual frontier

| Priority | Unit | Security or resource question | Required evidence before editing |
| --- | --- | --- | --- |
| P0 | `bin/tar` and `usr.bin/cpio` extraction confinement | Determine how absolute paths, `..`, pre-existing symlinks, hard-link targets and device entries can escape the selected destination. | Calibrated archives for every escape class, source call map, host filesystem oracle and final target footprint. |
| P0 | account-policy reconciliation | The image now ships setuid mode 04751 `su`, places `operator` in wheel and makes the console insecure for direct root login refusal, while `security-profile.md` still says the manifest omits `su`. | Built-image modes and account files, login/su host tests, Renode transcript, then a focused correction of the stale profile. |
| P1 | `compress` descriptor lifecycle | `stat` followed by `freopen`, then pathname `chmod`, `chown`, `utimes` and `unlink`, admits rename and symlink races that can apply metadata to or remove a replacement path. Reported metadata failures now preserve the input and remove the destination, but pathname identity remains unbound. | Competing-rename/symlink harness, descriptor-based create/update design, failure injection and packed-size comparison. |
| P1 | `compress` option grammar | `atoi` accepts ambiguous text and has no explicit overflow contract before the value is clamped. | Exact accepted grammar, boundary/overflow tests and a helper-dependency inspection. |
| P1 | executable loading and syscall copying | File headers, segment arithmetic and user pointers cross the kernel boundary inside a 144 KB flat process window. | Integer-boundary corpus, negative copy tests, exact loader/copy call graph and MPU fault evidence for protection claims. |
| P1 | USB control requests | Host-controlled setup packets select descriptor and endpoint operations in privileged code. | Deep-cache navigation refreshed to the selected source, direct source audit, packet corpus, host model and board fault/progress captures. |
| P2 | source-only setuid utilities | `chpass` and related password-database editors are outside the image but retain older dialect and privileged temporary-file logic. | Manifest reachability proof, strict-C17 build, concurrent update/failure harness and explicit decision to ship or quarantine. |

Each row remains open until its named negative fixture fails before the repair
and passes after it. Aggregate warning counts, aggregate token throughput and
semantic summaries cannot close a security or resource claim.

## Reproduction

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-compress-host
shellcheck -S error usr.bin/compress/tests/compresscheck.sh
cppcheck --std=c17 --enable=warning,performance,portability \
  --error-exitcode=2 usr.bin/compress/compress.c
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 distribution
tools/bin/rp2040/fsutil --check --partition=1 distrib/rp2040/sdcard.img
```

The build and distribution gates establish host and target construction, not
physical-board execution. A different compiler, flags, libc, packing codec or
base commit invalidates the numerical comparison and requires a fresh pair.
