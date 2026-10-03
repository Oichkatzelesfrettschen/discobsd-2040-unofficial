# Shipped security and C17 frontier

The initial shipped-surface search starts at
`dbd8bf0cc84f3705b79e3a13c79436b0ba51cb54`; each completed migration section
names its later source base explicitly. The audit covers programs and libraries
reachable from the RP2040 manifest, plus source-only utilities when a public
CVE names their mechanism. A source match establishes exposure; a name or
version match only nominates a review. Host sanitizers establish memory
behavior for the exercised inputs. Cortex-M0+ compilation, final a.out
inspection and filesystem accounting establish the target footprint. Board
behavior remains outside this audit.

Structural Graft job `527d63e861f3851b3aea15c224869cb0` supplied the first
symbol and file map. Its graph remains pinned to `dbd8bf0cc84f3705b79e3a13c79436b0ba51cb54`
and carries zero semantic-ready records; the separate deep cache covers only
`sys/arch/rp2040/dev` at an older commit. Graft located `copyin`, `mkpath` and
the direct `creat` and `mkdir` paths quickly, but every disposition below comes
from the named source revision, the manifest, a behavior probe or a public CVE
record.

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
| CVE-2005-1111 | GNU implementation absent | The signed FreeBSD advisory identifies contributed GNU cpio closing an output before a pathname-based permission change. The local implementation performs neither post-close chmod nor hard-link extraction. | A local extraction path changes metadata through a pathname after closing the created descriptor. |
| CVE-2005-1229 | GNU implementation absent; equivalent local weakness fixed | The advisory names GNU cpio, whose implementation and symbols are absent from the local 306-line program. The local base nevertheless accepted absolute and `..` names and wrote outside the extraction directory. | Either escape fixture creates its named victim outside the extraction root. |
| CVE-2005-4268 | GNU implementation absent; local truncation fixed | The advisory's 64-bit GNU formatting buffer is absent. The local writer used fixed fields without overflowing them, but silently truncated values wider than odc can represent. | A file size wider than the eleven-octal-digit field produces an archive header. |
| CVE-2015-1197 and CVE-2023-7216 | named GNU or RHEL implementations absent; equivalent local weakness fixed | The local base followed both a pre-existing symlinked parent and a final symlink through `access`, `mkdir` and `creat`. | Either symlink fixture changes the victim outside the selected pathname object. |
| CVE-2026-66484 | GNU tar path absent | The record and fixing commit name GNU cpio's tar hard-link target. The local program accepts only odc regular files and directories and calls no hard-link API. | Tar or hard-link handling becomes reachable in the local executable. |
| CVE-2026-66485 | GNU allocation path absent | The fixing commit removes archive-sized `alloca` use from GNU `make_path`. The local parser uses one `MAXPATHLEN` name buffer and one 512-byte transfer buffer and performs no allocation. | An archive-controlled length reaches stack or heap allocation. |
| CVE-2026-66486 | GNU implementation absent; equivalent local weakness fixed | The local cpio table path also printed an archive name literally. The repaired cpio parser rejects C0, DEL and C1 control bytes before listing or extraction. | A name containing newline or ESC reaches standard output. |
| CVE-2025-60753 | implementation absent | The record names libarchive bsdtar `apply_substitution` and `-s`. The local `bin/tar` has neither mechanism. | The local option parser gains substitution rules or a call edge resolves to equivalent unbounded allocation. |
| CVE-2001-1267 and CVE-2002-0399 | GNU implementation absent; equivalent local weakness fixed | The local base tar accepted `..`, `/..` and `./..` components. A calibrated `../escaped` member wrote outside its extraction root. | Any absolute, empty, dot, dot-dot or repeated-empty member component reaches a filesystem operation. |
| CVE-2002-1216, CVE-2006-6097 and CVE-2007-4131 | named implementations absent; equivalent local weakness fixed under a stable directory namespace | The local base used pathname `open`, `mkdir`, `symlink` and `link` operations without rejecting pre-existing or archive-created symlink traversal. Calibrated parent, final and archive-created symlink archives changed or reached objects outside the selected path. | A symlinked parent or final component changes its outside victim under the stable-namespace test fixture. |
| CVE-2016-6321 | GNU implementation absent; equivalent pathname-sanitization weakness fixed | The local base performed no component validation before extraction. The repaired parser validates the assembled ustar path, including full-width prefix, name and link fields, before selection, listing or filesystem use. | A malformed or control-bearing path reaches listing or extraction, or a full-width field reads into the following header member. |
| CVE-2025-45582 | implementation lineage absent; equivalent multi-entry symlink weakness fixed | The base extractor followed a symlink created by an earlier member when it processed a later descendant. The fixed extractor validates link targets and refuses every symlink encountered while walking a later member's parents. | A link member followed by a descendant creates the descendant through that link. |
| CVE-2026-18508 | named implementation absent; equivalent hard-link boundary weakness fixed | The base extractor passed an unchecked archive link target to `link()`. A calibrated `../outside-existing` target created a link inside the root to the outside inode. The fixed path accepts only a relative safe target whose verified regular-file identity was created earlier in the same extraction. | A hard-link target escapes, follows a symlink, names a pre-existing inode or carries special mode bits. |
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

## cpio extraction confinement and C17 migration

The cpio unit starts from `9d5b049afd7b9d5fd11841c68334ecc3ca65a9c0`.
The base extractor trusted archive pathnames, created parents with
`access`/`mkdir`, and opened final names with truncating `creat`. A calibrated
run of the new suite against that exact source reached `absolute archive
reported success`; the archive wrote its payload outside the extraction root.
The fixed suite exercises absolute and `..` escapes, empty and dot components,
symlinked parents, final symlinks, existing-file overwrite, unsupported types,
invalid octal, missing and embedded terminators, a missing trailer, C0 and ESC
listing names, listing from an execute-only current directory,
owner-untraversable directory entries under `umask 0777`, and final or parent
symlinks during archive creation. The suite compiles its host binary with the
target's 256-byte pathname limit, round-trips its 255-byte maximum name with a
newline and at EOF, and rejects a 256-byte or NUL-bearing input record. The
suite verifies that extracted files retain no setuid or setgid bits. It also
rejects absolute and missing archive-creation inputs instead of silently
publishing an unsafe or incomplete archive.

Review-head `468b2c2327ce2a5a098e139744d7c22030fb728a` supplied four
additional calibrated controls. Its table path failed with `EACCES` in an
execute-only current directory, a symlinked archive-creation parent produced a
183-byte archive containing the outside file, `umask 0777` prevented creation
of a streamed child, and its target-width writer rejected the 255-byte maximum
name as exceeding `MAXPATHLEN` when the delimiter arrived separately.

Extraction now accepts relative regular files and directories only. It checks
every odc digit, requires exactly one trailing pathname NUL and a zero-length
`TRAILER!!!`, rejects control bytes and unsafe components, and creates regular
files with `O_EXCL`. Extraction strips archive-controlled setuid, setgid and
sticky bits because the program has no ownership-restoration contract. For each
parent it compares `lstat` identity with the descriptor returned by `open` and
`fstat` before `fchdir`. Newly created directories start with owner access
under a temporarily cleared creation mask; `fchmod` on the verified descriptor
then applies the caller's mask while retaining owner access. The streaming
format supplies no bounded second pass for restoring a mode before later child
entries arrive; pre-existing directories retain their modes. Archive creation
walks and verifies every parent through the same descriptor path, rejects
symlinks and unsupported types, and compares the opened final regular file or
directory with its `lstat` device, inode and type. Table mode retains no root
descriptor. The writer's bounded byte-stream reader reserves the final buffer
byte for NUL, accepts newline or EOF as the record delimiter, and rejects an
embedded NUL or another pathname byte after the buffer fills. The writer also
refuses a file length wider than the odc size field.

The base source was not a C17 translation unit under the repository's strict
contract. Nine K&R definitions kept parameter types outside their declarators,
`copyout` and `copyin` used empty parameter lists, and writable globals and
`register` locals carried pre-prototype compiler assumptions. The parser also
used signed `int` byte counts for `read` and `write`, accepted partial octal
fields, discarded the final archive-name byte without proving it was NUL, and
treated header EOF as a successful archive without `TRAILER!!!`. The rewrite
uses full prototypes, internal linkage, `size_t` and `ssize_t` at byte-count
boundaries, `_Static_assert` for the 76-byte header and `_Noreturn` for fatal
paths. GCC and Clang accept the whole file as strict C17 with conversion
warnings, and the host behavior suite runs under address and undefined-behavior
sanitizers.

The object and final a.out pairs use one compiler flag set, crt0, linker script
and libc. Target `-fstack-usage` data gives the internal static frames; it does
not include libc or kernel stack use.

| Surface | Base | Repaired | Delta |
| --- | ---: | ---: | ---: |
| Source lines | 306 | 566 | +260 |
| Source bytes | 7,331 | 18,086 | +10,755 |
| Object text | 1,405 | 3,540 | +2,135 |
| Object data | 0 | 0 | 0 |
| Object BSS | 776 | 776 | 0 |
| Final text | 9,350 | 11,504 | +2,154 |
| Final data | 204 | 204 | 0 |
| Final BSS | 900 | 900 | 0 |
| Final a.out bytes | 9,588 | 11,740 | +2,152 |
| Packed bytes | 7,776 | 9,323 | +1,547 |
| Packed root blocks | 9 | 11 | +2 |
| Input-mode internal stack path | 160 | 256 | +96 |
| Output-mode internal stack path | 208 | 224 | +16 |

The repair adds no heap allocation and preserves both fixed buffers. Reusing
one `struct stat` at each identity check and keeping mutually exclusive input
and output frames outside `main` cut the first repaired draft's worst internal
path from 328 to 256 bytes. The remaining code, packed-block and stack growth
implements the pathname and format checks; aggregate shrinkage would require
removing those checks or changing the archive contract.

Archive creation now performs `lstat`, `open`, `fstat` and `fchdir` work for
each parent component; extraction already paid the same confinement cost.
Table mode removes its former root-directory open. These syscall counts explain
the direction of runtime change, but the audit has no Cortex-M0+ cycle capture
for cpio and makes no target-speed claim.

DiscoBSD lacks `openat`, `O_NOFOLLOW` and an `openat2`-style beneath-root
resolver. A concurrent process can rename a verified directory after the
identity check and before a descendant is created. The extractor also streams
effects: corruption discovered after a complete earlier entry does not roll
that earlier entry back. Those residuals require a directory-descriptor API or
a bounded staging design, not another pathname precheck.

## `compress` option grammar

The `-b` parser accepts only a nonempty sequence of ASCII decimal digits and
continues validating after saturating values above the build's `BITS` value,
so decimal input cannot overflow and retains the existing clamp behavior.
Values below `INIT_BITS` clamp upward. The host regression checks malformed,
signed, whitespace-prefixed and empty values, then inspects the encoded LZW
header for values above `INT_MAX` and a very long decimal value. The target
build and footprint gate determine whether this validation changes shipped
cost.

## tar extraction confinement and C17 migration

The tar unit starts from `d4c5e74f95524f7a8cf8a8125a61538291b59605`.
The base extractor accepted absolute or dot-dot paths, followed pre-existing
and archive-created symlinks, overwrote final symlinks and existing regular
files, accepted an outside hard-link source, restored archive-controlled
special mode bits, reused stale records after a short archive read, and trusted
unterminated or malformed header fields. A retained
calibration binary from that source writes the `../escaped` member, follows
the parent and final symlink fixtures, links to `../outside-existing`, and
restores mode 06755. The fixed test run against the same binary stops at
`dotdot archive reported success`, so the gate distinguishes the vulnerable
source from the repaired one.

Extraction now rejects empty, absolute, dot, dot-dot, repeated-empty,
control-bearing and over-limit paths before selection or filesystem use. The
control scanner distinguishes RFC 3629 sequences from raw C1 bytes, preserves
valid UTF-8 names and opaque non-control bytes, and rejects raw or UTF-8-encoded
C1 controls. A truncated sequence cannot hide a raw C1 continuation byte. It
accepts only regular files, directories, symbolic links and hard links, and
requires non-regular members to carry zero data. Parent traversal compares
`lstat` device and inode with `open` plus `fstat` before `fchdir`. Regular
outputs use `O_EXCL`; a later member can replace only an inode recorded by the
same extraction, including regular/symbolic-link type changes and replacement
between regular files and empty directories. Directory replacement uses
`rmdir`, preserves nonempty and pre-existing directories, and frees the removed
inode's metadata record. Append and update archives retain their last-entry-wins
contract within those boundaries. Ownership is not restored;
setuid, setgid and sticky bits are stripped. Created directories retain owner
traversal while children stream. Device/inode keyed directory records retain
the final mode and timestamp across noncontiguous members; a bounded
pathname-prefix cache write-combines restoration when the streamed prefix
changes. The mechanism therefore reopens a restrictive directory for a later
member and finalizes implicitly created parents with the extraction umask. An
existing search-only directory uses a verified `chdir` fallback when
`O_RDONLY` requires read permission; the entered device and inode must match
the pre-entry pathname observation. A hard link can name only a safe relative
regular-file identity created earlier in the same extraction, and an archive
cannot retain special bits through that alias. A relative symbolic-link target
can carry leading dot-dot components only while each one remains within the
member's parent depth; dot-dot after a named component is rejected because
intervening symbolic-link resolution defeats lexical reduction. Link fields
receive the same control and path validation before table output. Short writes
complete or fail, each archive-buffer refill carries its own valid-record
count, numeric fields reject invalid digits and overflow, and compressed reads
drain the filter pipe before the child status reaches a successful final
result. A fatal parse closes the read end before waiting, so an unbounded
decompressor tail cannot delay an already-decided rejection. Append and update
backspace by the final refill size rather than the initial blocking factor.
Directory metadata paths are bounded against both the native member buffer and
the fixed metadata stack before appending a directory slash. The native-capacity
host fixture confirms that a 256-byte ustar path fails with a bounded
diagnostic, while a sanitizer-instrumented mutation reproduces the original
out-of-bounds store.

Archive creation rejects absolute and parent-component inputs, normalizes
trailing slashes and harmless `.` components, treats an initial `.` as the
current directory's children, and skips
the output archive's captured inode if that file lies below the input root.
Unsafe source paths and symbolic-link targets set status 1 and skip the named
object while creation continues through the end-of-archive records, so a
rejected object does not leave a silently truncated archive. Creation reports
missing inputs and failed `-C` changes and verifies each directory identity
before entering it. A readable source root retains descriptor-rooted
traversal; a search-only root retains its pathname in the member buffer after
archive scanning finishes. Restoring that pathname also handles `-h` traversal
through a directory symlink before a later operand. Search-only
fallback covers both traversal components and explicit directory members. The
fixtures establish that behavior only under a non-privileged UID; a privileged
run emits an explicit skip because DAC override would bypass the fallback. A
symlinked source parent remains rejected by default; explicit `-h` follows it
while retaining the opened directory identity check.
Pre-existing directories used for extraction receive neither archive-controlled
mode nor archive-controlled timestamp metadata.
Update mode uses `mkstemp` and bounded linear parsing instead of `mktemp` plus
an external shell pipeline and fixed-window binary search. The replacement
removes a temporary sort, awk and move process set as well as the associated
pathname races; update lookup is linear in the number of archive entries
because the RP2040 image favors a small executable and bounded machinery over
another resident index.
The update index records the parsed timestamp as canonical octal and matches
the final separator against the complete pathname length, so space-padded header fields
and spaces in member names cannot cause an older operand to be appended.

C17 still permits old-style function definitions as obsolescent syntax. The
base file failed the repository's stricter contract because its K&R
definitions and empty parameter lists trigger `-Wold-style-definition` and
`-Wstrict-prototypes`; it also used deprecated `getwd`, unsafe `mktemp`, BSD
`bcopy`, `bzero` and `rindex`, unchecked signed byte counts and conversions,
writable internal tables, and `register` declarations. The migrated
translation unit uses complete prototypes, internal linkage, `_Static_assert`,
`_Noreturn`, `size_t`, `ssize_t`, `off_t` and checked conversions. GCC and
Clang accept the complete host unit as C17 with pedantic, prototype and
conversion diagnostics treated as errors, and both sanitizer suites pass.
The target build also passes its production warning contract. A direct target
probe with `-Wstrict-prototypes` still diagnoses declarations in the shared
`unistd.h` and `pwd.h`; target stdio and ioctl macros also trigger conversion
diagnostics. Those shared-header migrations remain outside the tar unit and
must not be represented as tar source failures.

The object and final a.out pairs use the same Cortex-M0+ compiler flags,
headers, crt0, linker script and libc. `hsaout -s` supplies the packed-root
measurement. Target `-fstack-usage` reports internal static frames and excludes
libc and kernel stack use.

| Surface | Base | Repaired | Delta |
| --- | ---: | ---: | ---: |
| Source lines | 1,913 | 2,721 | +808 |
| Source bytes | 48,416 | 82,605 | +34,189 |
| Object text | 6,684 | 10,124 | +3,440 |
| Object read-only data | 1,481 | 3,732 | +2,251 |
| Object writable data | 260 | 58 | -202 |
| Object BSS | 1,890 | 2,181 | +291 |
| Final text | 23,630 | 28,140 | +4,510 |
| Final data | 888 | 692 | -196 |
| Final BSS | 3,648 | 3,936 | +288 |
| Final a.out bytes | 24,552 | 28,864 | +4,312 |
| Packed bytes | 20,366 | 23,520 | +3,154 |
| Packed root blocks | 21 | 24 | +3 |
| `putfile` frame per recursive level | 824 | 832 | +8 |
| `dorep` frame | 552 | 560 | +8 |
| `doxtract` frame | 56 | 176 in `main` | +120 |

Writable data falls by 196 bytes because mode-display tables moved to read-only
storage and the update index disappeared. Linked BSS grows by 288 bytes: the
dominant addition is a 129-entry `unsigned short` directory-mode stack. The
stack holds only sanitized permission bits plus `USHRT_MAX`; narrowing the
retained representation from target `mode_t` saves 258 bytes while widening
back to `mode_t` at the `fchmod` boundary. The prefix stack avoids a pathname
allocation per directory and avoids restoring every ancestor after every
member. The creation frame grows by eight bytes to normalize one maximum-width
operand without mutating `argv`; fallback traversal depth uses shared state.
Security checks cost three packed root blocks in the final distribution.
Extraction keeps one shared list of 12-byte device/inode/next records for live
regular files and symbolic links, so target `malloc` consumes 16 bytes
including its header and alignment per record. The shared ownership list lets
a later archive member change an extraction-owned non-directory path between
regular-file and symbolic-link types without admitting a pre-existing path. A
directory record also carries its final timestamp, sanitized mode and
ownership bit; its 20-byte body consumes 24 allocator bytes. Repeated records
reuse identities. Replacement forgets an unlinked inode unless another
extracted hard link still names it. The live filesystem's inode capacity
therefore bounds the lists independently of archive record count. Both lists
are freed at extraction completion. Refusing all hard links or repeated
symbolic links would remove part of the variable RAM cost but would break
maintained archive semantics; accepting pre-existing sources would restore the
vulnerability.

Thirty warmed host runs over the 10,823,680-byte root-tree archive measured
listing at 4.4 ms mean for the base and 4.3 ms for the repair. Fifteen warmed
paired extraction runs measured 19.6 ms mean for the base and 35.4 ms for the
repair, a 1.81x host-filesystem cost. The review-head predecessor measured
34.1 ms in the same paired run, so inode-keyed directory revisits add 3.8% to
the repaired extraction path rather than the first correct draft's 69%.
Parent verification, exclusive creation,
metadata revalidation and hard-link provenance explain the direction. These
timings establish neither Cortex-M0+ latency nor board peak RAM.

The descriptor walk proves resistance to archive-controlled and pre-existing
symlinks only while the directory namespace remains stable. DiscoBSD provides
neither `openat`, `linkat`, `O_NOFOLLOW` nor descriptor-based timestamp
updates. A hostile concurrent writer can rename a verified directory before
the later pathname operation; path-based `link` and `utimes` retain the same
check/use window. Closing that residual requires a kernel and libc
directory-relative API, followed by adversarial rename tests. Streaming also
means a later malformed record does not roll back earlier extracted files.

## Ranked residual frontier

| Priority | Unit | Security or resource question | Required evidence before editing |
| --- | --- | --- | --- |
| P1 | `bin/tar` namespace-race residual | Replace stable-namespace pathname operations with directory-relative create, link and timestamp APIs without increasing the one-process window beyond its 144-kbyte limit. | Kernel and libc API design, adversarial concurrent-rename fixtures, ABI review, target stack and packed-root comparison. |
| P0 | account-policy reconciliation | The public tree shipped a fixed DES root verifier in world-readable `/etc/shadow`; direct root login is refused on the insecure console and wheel-only `su` skips password verification. The source now locks root and gives the image shadow mode 0600. | `bmake MACHINE=rp2040 check-account-image` extracts the packed image and verifies both fields; its calibrated negative controls reject a readable shadow file and an unlocked root verifier. Hardware account transitions remain unmeasured. |
| P1 | `compress` descriptor lifecycle | `stat` followed by `freopen`, then pathname `chmod`, `chown`, `utimes` and `unlink`, admits rename and symlink races that can apply metadata to or remove a replacement path. Reported metadata failures now preserve the input and remove the destination, but pathname identity remains unbound. | Competing-rename/symlink harness, descriptor-based create/update design, failure injection and packed-size comparison. |
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
bmake MACHINE=rp2040 check-cpio-host
sh bin/tar/tests/tartest.sh
shellcheck -S error usr.bin/compress/tests/compresscheck.sh
shellcheck -S error usr.bin/cpio/tests/cpiotest.sh
shellcheck -S error bin/tar/tests/tartest.sh
cppcheck --std=c17 --enable=warning,performance,portability \
  --error-exitcode=2 usr.bin/compress/compress.c
cppcheck --std=c17 --enable=warning,performance,portability \
  --error-exitcode=2 usr.bin/cpio/cpio.c
cppcheck --std=c17 --enable=warning,performance,portability \
  --error-exitcode=2 bin/tar/tar.c bin/tar/tests/tarfixture.c
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 distribution
tools/bin/rp2040/fsutil --check --partition=1 distrib/rp2040/sdcard.img
```

The build and distribution gates establish host and target construction, not
physical-board execution. A different compiler, flags, libc, packing codec or
base commit invalidates the numerical comparison and requires a fresh pair.
