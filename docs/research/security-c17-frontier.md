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
symbol and file map. At current source `34902d8a3aa0a742729989be32fea67a2732ee35`,
its graph remains pinned to `dbd8bf0cc84f3705b79e3a13c79436b0ba51cb54` with
26,116 pending and zero ready records. Graft queries read live source excerpts;
the separate deep cache has 137 ready summaries for `sys/arch/rp2040/dev` at
`b7ba8421daa773d0498077b8d170031405688906`. Graft located `copyin`, `mkpath`
and the direct `creat` and `mkdir` paths quickly, but every disposition below
comes from the named source revision, the manifest, a behavior probe or a
public CVE record.

The login batch uses a temporary structural graph generated from the active
worktree at base `5337ce8e883638b0fae5b2ebc95050d83c063c75`. Graft parsed 2,595
files into 26,235 nodes and 16,551 edges, then replayed 2,594 extraction
records on refresh. A narrowed credential-transition query covered
`usr.bin/login/login.c`. The retained MCP cache remains registered to the
canonical checkout and was not rebound to this worktree. Graft located
unchecked `setgid`, `initgroups`, `setuid`, and Kerberos `setreuid` calls; live
source and libc/kernel implementations confirmed the return-value contracts.

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

## Packed password utility permissions

`usr.bin/passwd/Makefile` installs `passwd` with mode `04755`, and
`passwd.c` needs effective root to replace `/etc/shadow` and rebuild the
password database after authenticating the caller's old password. The RP2040
image does not install the Makefile's output through that recipe: `pack`
entries use the mode supplied by `distrib/rp2040/mi.rp2040` and
`tools/fsutil/fsutil.c` passes that mode into `fs_file_create()`.

The previous manifest set `filemode 0775`, packed `passwd` without an
override, then packed `su` followed by `mode 04751`. The fsutil parser applies
`mode` to the pending object and closes that object when the next object
directive arrives. Consequently `su` received `04751`, while `passwd`
received `0775`. A non-root caller therefore could not complete the shipped
password-change flow even though the standalone build recipe declares the
setuid installation mode. The manifest now assigns `04755` to `passwd` and
retains `04751` for `su`.

`mkmanifest.py` now carries the effective file mode through profile
composition, requires the two privileged packed programs to retain their
declared modes, and has negative controls that remove each mode directive.
`check-fs-profiles` passes on all five profiles. `check-account-image` reads
the actual packed inode modes through fsutil and verifies both setuid bits in
the built image, in addition to checking the locked root verifier and private
shadow file. The gate does not execute a setuid account transition on the
board.

## Shipped passwd C17 and warning-profile migration

`usr.bin/passwd/passwd.c` was a maintained shipped translation unit, but its
Makefile selected `WARNLEVEL=legacy` at base commit `39468a08`. The RP2040
target compiler is `arm-none-eabi-gcc 16.2.0`, Cortex-M0+ Thumb, `-Os`, and
GNU17. Enabling the
existing full warning profile exposed two concrete defects: comparing the
unsigned process `uid_t` against the signed `struct passwd.pw_uid`, and
passing integer `NULL` as the variadic `execl` sentinel. The authorization
check now rejects negative stored UIDs before converting the target UID, and
`execl` receives a null pointer sentinel.

All four function definitions now use complete parameter lists; helpers and
the process UID have internal linkage, obsolete `register` declarations and
redundant function declarations are removed, and `include/unistd.h` owns the
`crypt`/`getpass` argument contracts without conflicting local declarations.
`usr.bin/passwd/Makefile` now uses
`WARNLEVEL=full` and persistently enables `-Wold-style-definition`. The exact
Cortex-M0+ build passes `-Wall -Wextra -Werror` with that source-form gate. A
direct target compile also passes
`-std=c17 -Wall -Wextra -Wold-style-definition -Werror`.

The measured linked executable changes from 14,346 to 14,274 text bytes; data
remains 624 bytes and BSS remains 1,620 bytes. The raw a.out shrinks from
15,004 to 14,932 bytes, and the repository packer reports 12,312 to 12,249
packed bytes, reducing the packed root allocation from 14 blocks to 13. The
source changes from 292 lines / 7,161 bytes to 284 lines / 7,217 bytes; the
small source-byte increase accompanies clearer prototypes while the linked
and packed forms shrink. The full distribution build installs byte-identical
output, and `fsutil` reports the packed `/usr/bin/passwd` inode as mode
`0104755`, owner 0. The account-image checker and all five manifest profiles
pass.

This is a per-file migration, not proof that the file's shared declarations
are strict-prototype clean. Adding `-Wstrict-prototypes` reaches pre-existing
empty-parameter declarations in `include/pwd.h` and `include/unistd.h` before
it can certify this translation unit; those headers remain separate C17
frontier entries.

## Shipped login privilege handoff and C17 migration

`usr.bin/login/login.c` installed the selected user's group and user identity
without checking the return values from `setgid`, `initgroups` or `setuid`.
`initgroups` returns `-1` when its privileged `setgroups` call fails, and the
kernel credential syscalls return errors when their checks reject a request.
The old path could therefore continue toward shell execution without proving
that the requested credential transition completed. The repair checks each
transition and exits on failure. It applies the same rule to `setreuid` and
root-UID restoration in the conditional Kerberos path; that optional branch is
not selected by the RP2040 target build.

The command-line username path now enforces the same `UT_NAMESIZE` bound as
interactive input. The environment vector starts with its required null
terminator before `setenv` traverses it. Password comparison handles a null
`crypt` result, and the shell argument vector ends with a pointer-valued null
sentinel. The translation unit now uses complete C17-style function
definitions, internal linkage for private helpers and state, `strchr` and
`strrchr`, explicit braces and the full target warning profile. Dead helpers
and data were removed. The unchanged `tty_modes.c` remains a separately
compiled helper.

The source contract checks handoff ordering and rejects fixtures that remove
the final UID check, a Kerberos credential check, a mode restore or a required
handoff stage. `shellcheck -S error`, the contract script, and
`bmake MACHINE=rp2040 -C tests/getty_contracts clean check` pass. The login
target builds with `-Wall -Wextra -Werror -Wold-style-definition` under the
repository GNU17 target mode. A standalone strict-prototype check remains
blocked by the shared `include/unistd.h` declarations for `access` and
`alarm`; those declarations are separate frontier work.

Matched Cortex-M0+ artifacts use repository startup, libc and libutil with
`-Os`. The login source changes from 620 lines / 14,559 bytes to 611 lines /
14,638 bytes. The final ELF changes from 21,992/745/3,428 text/data/BSS bytes
to 21,912/693/3,432 bytes. Raw a.out size changes from 22,769 to 22,637 bytes;
packed size changes from 19,380 to 19,255 bytes, with the root allocation
remaining 20 blocks. The measured `main` stack frame remains 408 bytes and the
largest helper frame remains 1,040 bytes. Source bytes grow slightly while
the linked text, initialized data, raw image and packed image shrink; BSS
grows by four bytes. These are host cross-build measurements, not board RAM
or runtime observations.

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
| CVE-2025-45582 | implementation lineage absent; equivalent cross-invocation symlink weakness fixed | The [NVD record](https://nvd.nist.gov/vuln/detail/CVE-2025-45582) describes a symlink extracted from one archive and a descendant extracted by a second invocation. The regression extracts a safe link to an in-root symlink in the first invocation, then attempts an outside-targeting descendant in the second; the fixed extractor refuses the symlink parent and leaves the outside sentinel unchanged. | A later `tar xf` follows a link retained by an earlier invocation and changes the outside sentinel. |
| CVE-2026-18508 | named implementation absent; equivalent hard-link boundary weakness fixed | The base extractor passed an unchecked archive link target to `link()`. A calibrated `../outside-existing` target created a link inside the root to the outside inode. The fixed path accepts only a relative safe target whose verified regular-file identity was created earlier in the same extraction. | A hard-link target escapes, follows a symlink, names a pre-existing inode or carries special mode bits. |
| CVE-2026-10659 | affected Zephyr adapter absent | The record names Zephyr `drivers/disk/ftl_dhara.c` writing through a null error pointer. Every RP2040 flash callback routes errors through null-safe `dhara_set_error`. | A callback writes `*err` without first proving the pointer non-null, including a generated or alternate build path. |
| CVE-2026-23833 | ESPHome API decoder absent from the shipped image | The NVD record names `components/api/proto.cpp` and its protobuf length-overflow check. The RP2040 manifest ships no ESPHome API component or protobuf decoder; the device console is USB CDC-ACM, not the ESPHome plaintext API. | A selected manifest profile or linked image gains the affected decoder, or an equivalent unauthenticated length check is found in the shipped protocol path. |
| CVE-2026-54571 | ESPAsyncWebServer multipart parser absent | The NVD record names `src/WebRequest.cpp`, an 8-bit multipart boundary position and a 256-byte boundary loop. The RP2040 manifest contains no ESPAsyncWebServer source, `WebRequest.cpp` or multipart parser. Generic TinyUSB FreeRTOS support is not selected by the RP2040 build; `tusb_option.h` defaults `CFG_TUSB_OS` to `OPT_OS_NONE`. | The RP2040 build selects the affected parser or an equivalent request-driven 8-bit boundary loop; audit the resulting watchdog behavior on the target. |
| CVE-2025-53094 | ESPAsyncWebServer header writer absent from the shipped image | The NVD record names `src/AsyncWebHeader.cpp` and CR/LF injection into HTTP response headers. The RP2040 manifest contains no ESPAsyncWebServer source or asynchronous HTTP/WebSocket server; the separately packaged host console uses Python's `http.server`. | The shipped image gains that library or an equivalent attacker-controlled response-header construction path. |
| CVE-1999-1471 | shipped mechanism absent; source review retained | The shipped `passwd` changes the password hash and has no shell or GECOS input. `chpass`, which owns those fields, stays outside the RP2040 manifest and already bounds its aggregate GECOS buffer. | Shipping `chpass`, or finding an unbounded shell/GECOS copy in a setuid image path, reopens the row. |
| CVE-2000-0993 | helper absent | The affected BSD libutil `pw_error` format-string helper has no source or call site in the tree. | A linked object exports `pw_error`, or another password-database error path treats user data as a format string. |

The three additional NVD records were fetched on 2026-10-04 from
`https://services.nvd.nist.gov/rest/json/cves/2.0?cveId=CVE-YYYY-NNNN`.
The upstream advisories are [GHSA-87j8-6f7g-h8wh](https://github.com/ESP32Async/ESPAsyncWebServer/security/advisories/GHSA-87j8-6f7g-h8wh),
[GHSA-4h3h-63v6-88qx](https://github.com/esphome/esphome/security/advisories/GHSA-4h3h-63v6-88qx)
and [GHSA-4phx-fcj6-46r4](https://github.com/ESP32Async/ESPAsyncWebServer/security/advisories/GHSA-4phx-fcj6-46r4).
Repository reachability was checked against `distrib/rp2040/mi.rp2040`,
`distrib/rp2040/profiles`, the RP2040 USB configuration and tracked source with
`rg` and `git grep`. Graft's structural lexical query returned host-console and
legacy WIZnet matches, not an RP2040 HTTP implementation. The exact vulnerable
files and API identifiers are absent from tracked source. A platform list in
an upstream advisory alone does not establish that the named component ships.

## Measured C17 work register

This register covers the security-sensitive translation units already
migrated and measured below; it is a bounded starting set, not a census of
every C translation unit linked into every RP2040 profile. The
passwd/compress/cpio/tar rows preserve the source snapshot rebuilt at
`34902d8a3aa0a742729989be32fea67a2732ee35` with the RP2040 Cortex-M0+ flags,
repository startup object and repository libc. Later rows name their own
source batch and target measurement. Object and final ELF sections come from
`arm-none-eabi-size`; raw and packed a.out sizes and root blocks come from
`tools/bin/rp2040/hsaout -s`. Earlier base-to-repair deltas remain in the
detailed sections below.

| Translation unit | C17 gate and current status | Source lines / bytes | Object T/D/B; ELF T/D/B | a.out raw / packed; root blocks | Remaining work |
| --- | --- | ---: | --- | --- | --- |
| `usr.bin/login/login.c` | Target `-Wall -Wextra -Werror -Wold-style-definition` passes under GNU17; source-contract negative controls cover the privilege handoff. | 611 / 14,638 | 3,128/12/164; 21,912/693/3,432 | 22,637 / 19,255; 20 blocks | Shared `access` and `alarm` prototypes block direct strict-prototype certification; the optional Kerberos branch lacks a target build and injected-failure test. |
| `usr.bin/passwd/passwd.c` | Target `-std=c17 -Wall -Wextra -Wold-style-definition -Werror` passes; production target remains GNU17. | 282 / 7,147 | 1,955/0/4; 14,274/624/1,620 | 14,932 / 12,249; 13 blocks | `include/unistd.h` retains two unspecified argument lists for malformed source-only callers; close those callers before whole-unit `-Wstrict-prototypes` certification. |
| `usr.bin/compress/compress.c` | Production and DEBUG forms pass host strict C17 with pedantic warnings treated as errors. | 1,424 / 37,245 | 4,911/32/30,216; 13,326/236/30,344 | 13,596 / 11,241; 12 blocks | Close the pathname identity race in the `stat`/`freopen`/metadata lifecycle with descriptor-relative APIs; retain input on metadata failure. |
| `usr.bin/cpio/cpio.c` | GCC and Clang pass host strict C17 with conversion diagnostics; host behavior tests pass ASan and UBSan. | 566 / 18,086 | 3,540/0/776; 11,501/204/900 | 11,740 / 9,323; 11 blocks | Close the stable-namespace rename race through directory-relative APIs or a bounded staging design. The measured internal stack paths are 256-byte input and 224-byte output. |
| `bin/tar/tar.c` | GCC and Clang pass host strict C17 with pedantic, prototype and conversion diagnostics; target production warning contract passes. | 2,770 / 84,055 | 14,024/58/2,181; 28,308/692/3,936 | 29,032 / 23,681; 25 blocks | Shared headers and target stdio/ioctl macros still block the direct target strict-prototype/conversion probe; the stable-namespace extraction race remains. |
| `include/pwd.h` / `lib/libc/gen/getpwent.c` | Header API passes standalone strict C17 prototype checking. The implementation has full parameter lists and passes target `-Wold-style-definition -Werror`; whole-unit strict-prototype checking still reports two declarations in `include/unistd.h`. The full RP2040 build and aggregate check pass. | Header 37 / 1,455; C 205 / 4,030 (base 34 / 1,384 and 209 / 4,063); 54 C/header includers | `getpwent.o`: 620/4/344 bytes; identical section sizes to the pre-edit object. Maximum measured function frame: 24 bytes. | Linked through libc; no standalone executable measurement. | The remaining `unistd.h` argument lists depend on correcting source-only legacy callers. No public structure layout or function ABI changed. |
| `include/unistd.h` | At `2f05c54a285af10b782f09cd98c43d19302a5483`, 13 no-argument and 13 argument-taking APIs had typed declarations. This batch types `sync(void)`; C17 function-pointer and wrong-arity checks cover the 27 typed declarations. | 179 / 6,834 | Header emits no object; `passwd.o` 1,955/0/4 | `passwd` 14,932 raw / 12,249 packed; 13 blocks; `libc.a` 406,408 bytes | Two unspecified declarations remain: `access` and `alarm`. Their callers are in source-only `uucp` and `tip`; both programs are absent from the RP2040 `full` root manifest. |
| `sbin/fdisk/fdisk.c` | Complete target translation unit builds as C17 with `-Wall -Wextra -Werror -Wmissing-prototypes -Wold-style-definition`; the `fdisk` Makefile enables the full warning profile. The shared `sync` API now rejects arguments at compile time. | 341 / 6,391 | 2,397/0/516; 10,042/216/588 | a.out 10,292 raw bytes; absent from RP2040 root manifest | Repair the four baseline warning sites, correct the `sync(fd)` call, make helper/global symbols internal, check partition bounds without signed overflow, and handle descriptor zero and pathname formatting. A separate audit still needs to validate numeric parsing and MBR I/O failure handling. |

The source and binary snapshot can be repeated in a clean worktree after
creating the generated `include/machine` link to `rp2040`:

```sh
bmake MACHINE=rp2040 -C tools install
bmake MACHINE=rp2040 -C lib/startup-arm
bmake MACHINE=rp2040 -C lib/libc
bmake MACHINE=rp2040 -C usr.bin/passwd clean all
bmake MACHINE=rp2040 -C usr.bin/compress clean compress
bmake MACHINE=rp2040 -C usr.bin/cpio clean all
bmake MACHINE=rp2040 -C bin/tar clean all
arm-none-eabi-size usr.bin/passwd/passwd.o usr.bin/compress/compress.o \
  usr.bin/cpio/cpio.o bin/tar/tar.o
tools/bin/rp2040/hsaout -s usr.bin/passwd/passwd \
  usr.bin/compress/compress usr.bin/cpio/cpio bin/tar/tar
```

`compress.c` received the bounded `-b` decimal grammar and saturation fixes
after its original before/after size pair. The snapshot table records the
current source at `34902d8a`; the migration section's original pair remains
historical and must not be read as the current executable size.

The `include/pwd.h` and `lib/libc/gen/getpwent.c` batch was measured at
`286600f2cdb4b5d1d3439c81a3f4a5d762a85a53`. The standalone header probe used
`-std=c17 -Wall -Wextra -Wstrict-prototypes -Werror`. The implementation
probe used Cortex-M0+ with flags
`-std=c17 -Wall -Wextra -Wstrict-prototypes -Wold-style-definition -Werror`
and `-Wno-error=strict-prototypes` only to demote existing strict-prototype
warnings from `include/unistd.h` for diagnosis.
The complete
`PYTHON=$(command -v python) bmake MACHINE=rp2040 build` rebuilt the shipped
kernel, libc, utilities and distribution against the new declarations, and
`PYTHON=$(command -v python) bmake MACHINE=rp2040 check` passed all configured
non-hardware tiers. The libc object retained 620 text, 4 data and 344 BSS bytes; the pre-edit object
had the same section sizes. `-fstack-usage` measured a maximum 24-byte frame
in `scanpw`. No separate behavior test exists for this libc API, and the
change leaves lookup behavior and structure layout unchanged.

`include/pwd.h` is migrated. The `include/unistd.h` batch typed 26 of its 29
previously unspecified APIs, using the target source set, `libc` definitions,
syscall records and generated wrapper contracts. Its compile-only test uses
`-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes`, exact function-pointer
assignments and separate wrong-arity probes. The full target build and
aggregate `check` pass. The measured header is 179 lines / 6,829 bytes;
`libc.a` is 406,408 bytes, `passwd.o` is 1,955/0/4, and the packaged passwd
image is 14,932 raw / 12,249 packed bytes in 13 root blocks. Header changes
emit no code or data by themselves. The exact source commit is
`2f05c54a285af10b782f09cd98c43d19302a5483`.

The fdisk migration closed the `sync` declaration: the API takes no
arguments, and `sync(fd)` compiled only while the header left its parameter
list unspecified. The old `usage()`, `print_ptable()` and `wipe_mbr()`
definitions also used empty parameter lists instead of C17 `(void)`
prototypes; file-local helpers and state now have internal linkage. The
corrected complete source builds as C17 with the full warning profile and
passes the target link. The baseline warning sites were
partition-field format mismatches, a signed/unsigned bounds comparison and
the device-path format mismatch. The rebuilt Cortex-M0+ ELF uses 10,042 text,
216 data and 588 BSS bytes; the converted a.out is 10,292 bytes. The source
remains outside the shipped manifest, so these sizes do not change RP2040
root-image totals. Numeric parsing and MBR I/O failure handling remain
separate audit work for this source-only utility.

Two declarations remain open because repository-wide build consumers call
the functions with invalid argument forms. `usr.bin/uucp/uuxqt.c` calls
`access(NOLOGIN)` without a mode; `usr.bin/tip/cmds.c` passes the string
`value(ETIMEOUT)` to `alarm` instead of converting it with `number`. Both
programs are absent from the RP2040 `full` manifest. The next batch must
repair and migrate each complete translation unit under C17 conventions
before those two header prototypes close. `getpgrp(pid_t)` now matches the
kernel syscall record and shipped `compress` caller `getpgrp(0)`.

The next device-boundary units are
`sys/arch/rp2040/dev/usb.c` and the Dhara metadata path in
`sys/arch/rp2040/rp2040/map.c`; record source size, exact compile flags,
linked text/data/BSS, stack usage and a claim-specific negative test before
editing. `usr.bin/login/login.c` and `usr.bin/su/su.c` follow as account-boundary
units. This register does not yet enumerate all translation units behind
multicall binaries or profile-specific libraries; derive that census from the
resolved profile manifests and link inputs before claiming complete C17
coverage.

## Dhara page metadata bounds

At source revision `915cbaebaefee5ae187e7610a96d64fbdeec8b3a`,
`map.c:144-151` accepted alternate physical-page indexes from persistent
metadata and passed them to `dhara_journal_read_meta()`. For the shipped
1024-byte page and eight-page checkpoint group, a checkpoint-slot index
selected metadata offset 944; the 132-byte copy read 52 bytes past
`journal.page_buf` when the page shared the current head's buffered group.
The repaired reader validates the device page range, rejects checkpoint
slots, and checks the metadata slice against the NAND page size before either
the buffered copy or the NAND read.

`check-dhara-metadata` compiles the production `journal.c` as strict C17 with
address and undefined-behavior sanitizers. It exercises valid buffered and
NAND-backed pages, checkpoint and out-of-range pages, undersized geometry,
and a validator-bypass mutation that must reproduce the original stack
buffer overread. With the production Cortex-M0+ flags, the journal object
grows from 2,608 to 2,710 text bytes, with data and BSS unchanged; the
`dhara_journal_read_meta` frame grows from 32 to 40 bytes. A complete kernel
build after the repairs reports PICO at 102,198 text / 248 data / 39,816 BSS
bytes and PICO_UART at 90,328 / 192 / 14,768 bytes. These compile-time
measurements do not establish runtime stack high-water or physical-board
behavior. The host gate also does not establish unprivileged control of
corrupted QSPI metadata. The audit also checked raw NOR callback bounds.
Graft navigation to
`dhara_nand_read`, `dhara_nand_prog` and
`dhara_nand_is_free`, verified against `sys/arch/rp2040/dev/flash.c`, found
that the raw callbacks formed addresses before establishing the page was in
the filesystem extent; `dhara_nand_read` also used overflow-prone
`offset + length` arithmetic. The callbacks now share a subtraction-based
range predicate that checks the page index before address formation. The
sanitized host gate exercises the production predicate at the first and last
valid pages, the first invalid and maximum page indexes, and end/overflowing
byte ranges. This proves the predicate, while the target kernel build proves
the callbacks compile against it; neither establishes physical flash behavior.

NVD keyword queries returned zero records for `TinyUSB` and `heatshrink` on
2026-10-03. That result is a name-search receipt, not a clean bill of health:
aliases, downstream adapters and newly published records can evade it.
Heatshrink is pinned to `7d419e1fa4830d0b919b9b6a91fe2fb786cf3280`
(0.4.1 plus one commit). The USB implementation needs source-level control
request review independently of the product-name result.

## USB BOOTSEL GPIO selector

`usb_setup()` accepts the Pico reset-interface BOOTSEL request for interface
2 and passes its 16-bit `wValue` to `usb_reset_to_bootsel()`. When bit 8 asks
for an activity LED, bits 15:9 select the GPIO. The base implementation used
that seven-bit selector directly as `1UL << pin`; a host could select 32
through 127 and invoke an out-of-range shift in privileged interrupt context.
The request was reachable over the enumerated reset interface, so the bound
does not depend on local callers.

`usb_reset_gpio_mask()` now accepts only RP2040 GPIO indexes 0 through 29 and
returns failure before shifting any other requested index. `usb_setup()` stalls
an invalid reset request instead of calling the ROM. A selector is ignored and
produces a zero mask when the activity-LED flag is clear. The sanitizer test
exercises all 128 encodings and a calibrated mutation that accepts non-existent
GPIOs; the Cortex-M0+ `PICO` kernel build compiles the production USB path with
`-Wall -Wextra -Werror`. The `PICO_UART` UART-only kernel also builds with those
flags but excludes the USB driver.
With the installed arm-none-eabi GCC 16.2 and production `PICO` flags, the
rebuilt `usb.o` measures 3,728 text / 8,200 data / 344 BSS bytes, compared with
3,732 / 8,200 / 344 for the clean-main object. The object text falls by four
bytes while data and BSS remain equal; this is not a final UF2 or board-RAM
measurement.

At Pico SDK commit `079c6f39023649b154152db30f1d781e884879bc`, the public
reset-interface header defines the BOOTSEL request code at
<https://github.com/raspberrypi/pico-sdk/blob/079c6f39023649b154152db30f1d781e884879bc/src/common/pico_usb_reset_interface_headers/include/pico/usb_reset_interface.h>.
The SDK control handler documents bits 9 through 15 as a GPIO number from 0
through 127 and passes that selector to the boot ROM at
<https://github.com/raspberrypi/pico-sdk/blob/079c6f39023649b154152db30f1d781e884879bc/src/rp2_common/pico_usb_reset/usb_reset.c>.
The same revision's RP2040 platform header defines `NUM_BANK0_GPIOS` as 30
at
<https://github.com/raspberrypi/pico-sdk/blob/079c6f39023649b154152db30f1d781e884879bc/src/rp2040/hardware_regs/include/hardware/platform_defs.h>.
Its boot ROM header declares the reset callback with two `uint32_t` arguments
at <https://github.com/raspberrypi/pico-sdk/blob/079c6f39023649b154152db30f1d781e884879bc/src/rp2_common/pico_bootrom/include/pico/bootrom.h>.
The repository's dispatch predicate and selector decoding are in
`sys/arch/rp2040/dev/usb.c`; `tests/rp2040/usb_reset/check.sh` runs the exact
production helper under AddressSanitizer and UndefinedBehaviorSanitizer. No
physical USB packet or board reset was exercised by this host and kernel-build
evidence.

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
trailing slashes and harmless `.` components before enforcing the bounded
path buffer, including the maximum-width ustar path with an initial `./`,
treats an initial `.` as the current directory's children, and skips the
output archive's captured inode if that file lies below the input root.
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

The base source is `bin/tar/tar.c` at commit
`1bed0b4dc4aeb5c3fb0d0a2a3a559a809b5af592` (SHA-256
`db2736172f351d9417ead5987039bb573aeef75202f1b83634a073047a690968`). The
repaired source rows below were refreshed after the maximum-path normalization
change. The target object and final a.out use the same Cortex-M0+ compiler
flags, headers, crt0, linker script and libc. `arm-none-eabi-size -A
bin/tar/tar.o` supplies object section sizes; the executable size tool and
`tools/bin/hsaout -s bin/tar/tar` supply the final and packed-root values.
Target `-fstack-usage` with the same compile flags reports internal static
frames and excludes libc and kernel stack use. Reproduce the repaired build
with `bmake MACHINE=rp2040 -C tools install` followed by
`bmake MACHINE=rp2040 -C bin/tar all`.

| Surface | Base | Repaired | Delta |
| --- | ---: | ---: | ---: |
| Source lines | 1,913 | 2,770 | +857 |
| Source bytes | 48,416 | 84,055 | +35,639 |
| Object text | 6,684 | 10,292 | +3,608 |
| Object read-only data | 1,481 | 3,732 | +2,251 |
| Object writable data | 260 | 58 | -202 |
| Object BSS | 1,890 | 2,181 | +291 |
| Final text | 23,630 | 28,228 | +4,598 |
| Final data | 888 | 692 | -196 |
| Final BSS | 3,648 | 3,936 | +288 |
| Final a.out bytes | 24,552 | 28,952 | +4,400 |
| Packed bytes | 20,366 | 23,581 | +3,215 |
| Packed root blocks | 21 | 25 | +4 |
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
The repaired tar occupies four more packed root blocks than the base source.
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
| P1 | optional Kerberos login branch | The RP2040 target does not select `KERBEROS`, so the repaired `setreuid`/`setuid(0)` failure path is source-checked but absent from the target compile and runtime contract. | Build the branch against its supported Kerberos headers and libraries, then inject each credential syscall failure and prove the ticket path aborts safely. |
| P1 | `compress` descriptor lifecycle | `stat` followed by `freopen`, then pathname `chmod`, `chown`, `utimes` and `unlink`, admits rename and symlink races that can apply metadata to or remove a replacement path. Reported metadata failures now preserve the input and remove the destination, but pathname identity remains unbound. | Competing-rename/symlink harness, descriptor-based create/update design, failure injection and packed-size comparison. |
| P1 | executable loading and syscall copying | File headers, segment arithmetic, pathname inputs and argument vectors cross the kernel boundary inside a 144 KB flat process window. `exec_save_args()` now reads `argv`, `envp` and their strings through `copyin`; `ufs_namei.c:namei()` still copies syscall pathnames with direct `copystr()`. | Retain the calibrated `check-exec-spool` invalid-vector/string cases; add negative pathname pointer and user-window-boundary coverage for `namei`; retain the integer-boundary loader corpus and MPU fault evidence for protection claims. |
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
bmake MACHINE=rp2040 WARNLEVEL=full -C usr.bin/passwd clean all
/usr/bin/arm-none-eabi-gcc -std=c17 -Wall -Wextra \
  -Wold-style-definition -Werror -fno-common -mcpu=cortex-m0plus \
  -mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
  -DLINEAR_INODE_CACHE -DCOMPACT_INODE_FIELDS -DCOMPACT_SWAPMAP \
  -DSINGLE_UFS_ROOT -DNMOUNT=1 -nostdinc -Iinclude -Os \
  -c usr.bin/passwd/passwd.c -o /tmp/passwd-c17.o
PYTHON=/usr/local/bin/python3 bmake MACHINE=rp2040 check-fs-profiles
PYTHON=/usr/local/bin/python3 bmake MACHINE=rp2040 check-account-image
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
