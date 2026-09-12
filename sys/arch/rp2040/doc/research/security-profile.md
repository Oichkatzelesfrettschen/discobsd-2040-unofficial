# A constrained account profile for the RP2040 port

The RP2040 has no MMU and no MPU. Every process runs in the same physical
address space as the kernel and as every other process; UID separation
keeps `chmod`, `chown`, and file permission checks honest, but it stops
nothing that can execute arbitrary code, since arbitrary code can simply
read or write any address it likes. The profile below assumes this and
does not pretend otherwise: it is an administrative-mistake guard, not a
sandbox. The host (pico-host/discobsd-web, discobsd-term,
discobsd-connect) is the only place HTTP, WebSocket, TLS, and multi-user
authentication belong; the device runs a serial console and nothing that
resembles a network stack.

## 1. Current state, verified against the tree

Each claim below is checked against the file at the cited line. `refuted`
means the file's content does not match the claim as stated; the actual
content follows.

### 1.1 operator is locked with `*` in etc/passwd -- confirmed, but not the way it looks

`etc/passwd` line 4:

    operator:*:2:5:System &:/operator:/bin/sh

The literal `*` is there. But `usr.bin/login/login.c` does not check this
field directly. `getpwnam()` (`lib/libc/gen/getpwent.c`) treats a
numeric password field as a byte offset into `/etc/shadow` and, when the
caller is euid 0 (`getpw()`, `lib/libc/gen/getpwent.c:94-121`, the guard
at line 100 is `if (geteuid()) return;`), overwrites `pw_passwd` with the
bytes found there:

    p = strcmp(_pw_file, _PATH_PASSWD) == 0 ? _PATH_SHADOW : _pw_file;
    ...
    pos = atol(_pw_entry.pw_passwd);
    if (lseek(fd, pos, L_SET) != pos)
        goto bad;
    ...

`atol("*")` is `0`, so operator's real login password becomes whatever
`/etc/shadow` holds starting at byte offset 0 -- the first field of the
file's first line, which is `root`'s name, `"root"`. `login.c:296`
compares this against `crypt(typed_password, salt)`; a 4-byte literal
never equals a crypt digest, so the account is unreachable. operator
is locked, but by an accidental collision in the offset scheme, not by a
deliberate empty-vs-set distinction. A profile that relies on this same
mechanism for a password-free operator needs the offset to land somewhere
intentional (Section 2.2), not on whatever happens to sit at position 0.

### 1.2 root's password field and the "blank password" doc claim -- refuted

`etc/passwd` line 1:

    root:5:0:1:The Man:/root:/bin/sh

`etc/shadow` line 1:

    root:ro46DZg1ViGBs:0:1:The Man:/root:/bin/sh

`5` is a byte offset, not a password. Counting `etc/shadow` line 1 from
byte 0 (`r`=0, `o`=1, `o`=2, `t`=3, `:`=4), offset 5 lands exactly on the
`r` that opens `ro46DZg1ViGBs`, root's real crypt(3) digest. `getpw()`
reads forward from there to the next `:` and substitutes that string as
`pw_passwd`. So root's live password, as `login` resolves it, is the
13-character DES-crypt hash `ro46DZg1ViGBs`, not the empty string.

This contradicts the shipped documentation:

    README.md:90:              Log in to DiscoBSD with user `root` and a blank password.
    sys/arch/rp2040/doc/USER-ACCESS.md:41:  Log in as root with an empty password.
    distrib/stm32/README.md:67:            Log in to DiscoBSD with user `root` and a blank password.
    pico-host/discobsd-web:13 (comment): Log in as root with an empty password.

`login.c:260` only skips the password prompt when `!*pwd->pw_passwd` --
the resolved password string is empty. Root's resolved string is
`ro46DZg1ViGBs`, thirteen bytes, not empty, so the login prompt does ask
for a password and does check it against that hash. Either the shipped
image's `/etc/shadow` predates these four doc lines, or the doc lines
were never checked against the file that governs the actual login path.
The design in Section 4 treats "root logs in with a blank password
today" as false for the current tree and true only for whatever image a
given board was last flashed with -- the migration section below assumes
the worse case, that some boards in the field still boot an image where
it was true, since the maintainer's stated recollection is that it is.

### 1.3 wheel contains only root in etc/group -- confirmed

`etc/group` line 1:

    wheel:*:0:root

Exact match; no other member listed.

### 1.4 the RP2040 manifest omits su -- confirmed

`grep -n su distrib/rp2040/mi.rp2040` matches only unrelated substrings
(`shipped` in a comment at line 261, `cksum` at line 276). No `file
/usr/bin/su` line exists anywhere in the manifest's 366 lines, so a
built RP2040 image has no `su` binary regardless of whether `usr.bin/su`
compiles for the target.

### 1.5 usr.bin/su/su.c enforces wheel but still asks root's password -- confirmed

The wheel check sits at `usr.bin/su/su.c:104-121`:

    /*
     * Only allow those in group zero to su to root.
     */
    if (pwd->pw_uid == 0) {
        struct  group *gr;
        int i;

        if ((gr = getgrgid(0)) != NULL) {
            for (i = 0; gr->gr_mem[i] != NULL; i++)
                if (strcmp(buf, gr->gr_mem[i]) == 0)
                    goto userok;
            fprintf(stderr, "You do not have permission to su %s\n",
                user);
            exit(1);
        }
    userok:
        setpriority(PRIO_PROCESS, 0, -2);
    }

This is a hard gate: a caller not listed in `getgrgid(0)->gr_mem` (group
0, wheel) cannot reach the password prompt for a root target at all. But
the code that follows still asks for root's password:

    if (pwd->pw_passwd[0] == '\0' || getuid() == 0)
        goto ok;
    password = getpass("Password:");
    if (strcmp(pwd->pw_passwd, crypt(password, pwd->pw_passwd)) != 0) {

A wheel member who is not already root and whose target's `pw_passwd` is
non-empty is prompted and checked against root's crypt hash -- the exact
combination the maintainer wants replaced with a bare wheel check and no
prompt. Section 2.3 keeps root's `etc/shadow` hash non-empty (defense in
depth behind the `ttys` lock), which means stock `su.c` will keep
prompting even after `operator` joins wheel. The fix is not a `passwd`/
`shadow` edit -- it is deleting `su.c`'s password codepath, covered in
Section 3.

### 1.6 pico-host/discobsd-web binds 0.0.0.0, has no auth, and does not validate Origin or frame length -- confirmed

File: `/home/eirikr/Github/rpi/pico-host/discobsd-web` (9423 bytes, 280
lines).

- **Bind address.** Line 239: `bind = "0.0.0.0"`. Nothing in `main()`
  (lines 236-276) restricts this default; `--bind` is the only way to
  narrow it, and no caller in the repo passes it.
- **No authentication anywhere in the request path.** `do_GET`
  (lines 138-147) serves `PAGE` to any GET request with no credential
  check. `serve_ws` (lines 149-222) upgrades any request whose `Upgrade`
  header says `websocket` (checked only at line 139-140) straight to a
  live serial bridge; neither method reads a cookie, a header, or any
  other credential.
- **No Origin check.** `serve_ws` reads `Sec-WebSocket-Key` (line 150)
  to compute the RFC 6455 accept hash, but never reads
  `self.headers.get("Origin")`. Any page loaded in a browser that has
  network access to the bound port can open `new WebSocket(...)` against
  it and drive the console -- classic cross-site WebSocket hijacking,
  and the impact here is a live root-capable serial session.
- **No WebSocket frame length cap.** `ws_read` (lines 113-129) decodes
  the RFC 6455 length prefix, including the 127 case that unpacks a
  64-bit length (`struct.unpack(">Q", rf.read(8))[0]`, line 124) and then
  calls `rf.read(ln)` (line 126) with no upper bound. A client can claim
  an arbitrary frame length and force the handler to block reading (or,
  on a length small enough to complete but large enough to matter,
  allocate) with nothing to stop it; there is no per-connection or
  per-frame cap anywhere in the file.
- One session-serialization control does exist: `console_lock`
  (`ThreadingHTTPServer`'s per-request thread contends on
  `self.server.console_lock.acquire(blocking=False)`, lines 163-171),
  which refuses a second concurrent WebSocket rather than letting two
  clients fight over the same serial port. This is a correctness
  control, not an authentication control -- it stops two anonymous
  sessions from colliding, not an unauthorized one from opening.

`distrib/rp2040/host/71-discobsd-pico.rules` and
`pico-host/71-discobsd-pico.rules` are byte-identical. The rule
(line 13) is:

    SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", ATTRS{serial}=="rp2040", SYMLINK+="discobsd", TAG+="systemd"

`lib/libc/gen/getgrent.c:61` splits `gr_mem` on `,` (`grskip(p,',')`),
not whitespace -- `etc/group`'s existing single-member lines give no
evidence either way, but the parser is comma-delimited, so any new
multi-member wheel line in Section 2.4 must use commas, not spaces, or
`getgrgid(0)->gr_mem` returns one member string that never matches
`strcmp` and the wheel gate silently rejects everyone.

It sets `SYMLINK+` only -- no `GROUP=` and no `MODE=`. The node's
permissions come entirely from whatever generic USB-serial rule the
distribution ships (commonly `dialout` or `uucp` at mode 0660), so
"restricted to a dedicated host group" is not something this rule does
today; it is inherited, undocumented, and distribution-dependent.

## 2. Device account model

### 2.1 UID/GID choice

`etc/passwd` already reserves UID 2 / GID 5 for `operator`, with home
`/operator` and shell `/bin/sh` -- the exact shape the maintainer wants
for the password-free account. Reuse it rather than minting a new UID:
the manifest, `/operator` directory, and shell are already provisioned,
and a second general-purpose account would just be `operator` under
another name. `bin` (UID 3) and `daemon` (UID 1) stay non-login
(`nologin` shell or no interactive use); `sys` (UID 4) already carries
shell `nologin` and is the pattern to match for any account that must
exist in `passwd` for ownership purposes but never authenticates.

### 2.2 etc/passwd and etc/shadow entries

    # etc/passwd (unchanged shape, offset field updated -- see below)
    root:R:0:1:The Man:/root:/bin/sh
    operator:O:2:5:System &:/operator:/bin/sh

    # etc/shadow
    root:<real-crypt-hash>:0:1:The Man:/root:/bin/sh
    operator::2:5:System &:/operator:/bin/sh

`O` is the byte offset in `etc/shadow` of the character immediately
following `operator:` -- the position of the `:` that closes an *empty*
password field, so `getpw()`'s forward scan for the next `:` (`getpwent.c`
`getpw()`, around line 116) returns a zero-length string on the first
iteration. That makes `pwd->pw_passwd` `""` after `getpwnam("operator")`
resolves it, which is exactly the condition `login.c:260` already tests:

    if (!passwd_req || (pwd && !*pwd->pw_passwd))
        break;

`passwd_req` stays `1` for an interactive (non `-f`) login, so the first
disjunct is false; the second is true precisely because the resolved
password is empty. No login.c change is required to make an empty
`etc/shadow` password field skip the prompt -- the mechanism already
exists and already does the right thing once the *offset* in
`etc/passwd` is computed correctly, which it is not today for `operator`
(Section 1.1: it accidentally points at byte 0, not at operator's own
field).

`R` is root's offset, computed the same way, pointing at whatever real
crypt hash `etc/shadow` carries for root (Section 4 covers what that
hash should be during migration). Root's `passwd` shell stays `/bin/sh`
in the file only because the manifest format requires a shell field;
root never reaches a login prompt because of the `ttys` change below,
not because of the passwd entry.

This offset scheme is authoritative only while the image still runs
stock `/usr/bin/login` (migration steps 1-6, Section 5.2); Section 2.5's
trimmed `serial-login` never reads `pw_passwd` at all and makes this
offset math moot for the account it authenticates once it replaces
`login` in step 7. Both are documented here because both are live at
different points in the migration, not because they compete for the
same shipped image. `usr.sbin/mkpasswd` looks like the obvious
generator for these offsets but is not: it builds `ndbm` `.pag`/`.dir`
databases (`mkpasswd.c:144-160`), a lookup path `getpwent.c` never
opens -- this port's `getpw()` does a raw `fopen()`/`lseek()` against
`_PATH_SHADOW` (Section 1.2) with no dbm involved. No in-tree tool
computes these offsets; `etc/passwd`'s current values were hand-fitted
to `etc/shadow`'s current byte layout, and stay valid only as long as no
line above a given entry in `etc/shadow` changes length. Editing any
earlier line (including the `usermod`-equivalent of lengthening a
gecos field) invalidates every offset below it silently -- a wrong
offset does not fail to compile or flash, it just resolves to the wrong
bytes at login time, which is exactly the accidental-lock mechanism
Section 1.1 found for `operator` today. Recompute every affected offset
by hand (byte-count `etc/shadow` from the top) whenever any line above
the one being edited changes, and verify with the printf-style debug
lines already commented out in `login.c:215`/`294` (temporarily
uncomment, test, recomment -- do not ship the tree with them live,
since they print cleartext passwords to whatever captures login's
stdout).

### 2.3 root stays locked at login, not by password

Locking root's *login* path, rather than relying on a hash nobody can
guess, is the more direct control given `login.c`'s own logic:
`login.c:238-251` refuses root login outright on any tty `rootterm()`
calls insecure, and `etc/ttys`' `secure`/`insecure` keyword on each line
is exactly that switch. Today every line in `etc/ttys` (console,
tty0-tty5, ttyUSB0) is marked `secure`:

    console "/usr/libexec/getty std.default" xterm on secure #special
    ttyUSB0 "/usr/libexec/getty std.default" xterm off secure

Changing `console`'s (and any enabled line's) trailing keyword from
`secure` to `insecure` makes `rootterm()` return false for every serial
line the RP2040 exposes, so `login.c:238` prints "root login refused on
this terminal" and denies the attempt before any password is even
requested. This is stronger than a locked hash: it holds even if
`etc/shadow`'s root entry is ever set back to a guessable or empty
value, and it matches the design goal directly -- "direct root login
stays locked" becomes a `ttys` policy, not a hope that nobody resets the
hash. Root's `etc/shadow` hash should still be a real, non-empty,
unknown-to-nobody-in-particular value (Section 4), as defense in depth
if the `ttys` line is ever reverted, but the `ttys` change is the
primary control.

### 2.4 wheel membership

    # etc/group line 1
    wheel:*:0:root,operator

Add `operator` to `wheel`'s member list, comma-separated per
`getgrent.c`'s parser (confirmed above -- a space-separated list parses
as one non-matching member and breaks the gate silently). `su.c`'s
wheel check (Section 1.5) reads this list by name via
`getgrgid(0)->gr_mem`; adding `operator` here is the membership half of
the authorization change; Section 3 covers the other half, removing the
password prompt `su.c` still runs after the membership check passes.

### 2.5 Trimmed serial-login versus reusing usr.bin/login

`usr.bin/login/login.c` is 599 lines and its built `a.out` is 23401
bytes (measured: `ls -la usr.bin/login/login`). It carries machinery this
profile does not want on a single-account, serial-only device:
`-h`/`-f`/`-p` flag handling for remote/pre-authenticated/pty logins that
make no sense with no network stack, `#ifdef KERBEROS` ticket
acquisition (`login.c:46-51`, `368-388`), `dolastlog()`/`lastlog.h`
tracking, `checknologin()`, `motd()`, and the timeout/alarm and
`rootterm()`/tty-security logic that only matters once multiple tty
lines and remote logins are in play.

A trimmed `serial-login` for this profile does four things and nothing
else: reads a bounded username (a fixed-size buffer, no `-f`/`-h`/`-p`
parsing), looks it up with `getpwnam()`, rejects UID 0 outright before
any password logic runs (root never reaches this binary's password path
regardless of the `ttys` secure/insecure setting -- belt and suspenders
with Section 2.3), and either exits with an error (unknown user, UID 0)
or sets a fixed, minimal environment (`PATH`, `HOME`, `SHELL`, `USER`,
`TERM`) and `execve`s `/bin/sh`. It links no `crypt()`, no `lastlog.h`,
no `syslog.h`, no Kerberos headers, no `getpass()` -- for a password-free
account, there is no password codepath to carry at all, and root's
absence from this binary's accepted UID set removes the need for
`rootterm()` and the `ttys` `secure` keyword parsing on this path (the
`ttys` change in Section 2.3 still gates whether `serial-login` or a
future `login` even starts for a given line, at the `getty`/`ttys`
level, so both controls coexist rather than substitute for each other).

Estimating the trimmed size: `su.c`, at 178 lines with one crypt/getpass
codepath, one group lookup, and one exec, builds to 18984 bytes. A
`serial-login` with less logic than `su` (no `crypt()`, no password
prompt, one `getpwnam()`, one UID check, one `execve()`, versus `su`'s
`getpwuid()` + `getpwnam()` + group walk + `crypt()` + `getpass()` +
`execve()`) should land under that, in the neighborhood of 12-16 KB once
`libc.a`'s stdio and `getpwnam()` machinery link in -- the bulk of both
binaries' size on this toolchain is the static libc pull-in around
`getpwnam()`/`getgrgid()`, not the caller's own logic, so the reduction
tracks the removed *codepaths* (Kerberos, lastlog, syslog, crypt, getopt)
more than the removed lines. This is an estimate, not a measurement:
confirm it by building the trimmed binary and running `ls -la` /
`size(1)` against it before citing a number in a commit message or a
flash-budget table.

## 3. Manifest changes

### 3.1 su carries a stripped password codepath, not stock usr.bin/su

Shipping stock `usr.bin/su` reintroduces the exact prompt Section 1.5
and Section 2.4 just removed the need for: `su.c:124-134`'s
`getpass()`/`crypt()` check still runs for any wheel member su-ing to a
non-empty-hash root, which is what Section 2.3 requires root to have.
The file-level change is to `usr.bin/su/su.c`: delete lines 124-134 (the
`if (pwd->pw_passwd[0] == '\0' || getuid() == 0) goto ok;` through the
`crypt()` comparison and its failure branch) so the function falls
through from the `userok:`/wheel-check block (lines 107-121, unchanged)
straight to `ok:` (line 135). No `crypt()`, `getpass()`, or
`pwd->pw_passwd` comparison remains reachable; the `getgrgid(0)` walk at
lines 111-118 becomes the sole gate, which is what "a small setuid su
lets only wheel members become root without a password" means as code,
not as a root password left blank. Keep the `pwd->pw_uid == 0` target
restriction (line 107) as-is -- this `su` still only grants root, not an
arbitrary target account, which is the "minimal su" the maintainer asked
for. This is a source change belonging to the implementation that
follows this design document, not something this document ships;
naming it here is so the manifest entry below is not built from
unmodified `usr.bin/su`.

### 3.2 New manifest entries

`distrib/rp2040/mi.rp2040` needs two new `file` entries. Following the
manifest's existing grouping (setuid-relevant binaries get their own
mode, matching how `/usr/bin/passwd` is called out in the utilbox
comment at manifest line ~247 for the same reason -- setuid root cannot
share a multicall binary). `tools/fsutil/manifest.c:371` parses
`filemode`'s argument with `strtoul(arg, 0, 0)` -- base 0, so a leading
`0` is required for octal; `4755` with no leading zero parses as decimal
4755 (0x1293), silently dropping the setuid bit from the image while
the manifest text looks correct. Every existing mode in the manifest
already carries a leading zero (`0775`, `0664`, `0666`, `0644`); the new
entries must match that convention, not the shell convention where a
bare `4755` is understood as octal:

    #
    # su: setuid root, wheel-gated, modified per Section 3.1 to drop
    # the password codepath (usr.bin/su, wheel-only, no crypt/getpass).
    # Not part of any box multicall binary for the same reason passwd
    # stays out of utilbox -- a shared a.out would hand every linked
    # name root.
    #
    default
    owner 0
    group 0
    filemode 04755
    file /usr/bin/su

    #
    # serial-login: trimmed getty-invoked login for the console line.
    # Rejects UID 0; no setuid bit needed if getty already runs as root
    # and serial-login execve()s the target shell after a setuid(pw_uid)
    # drop, matching how /usr/libexec/getty is already listed unprivileged.
    #
    default
    filemode 0755
    file /usr/libexec/serial-login

`sys/kern/kern_exec.c:142` (`if (ip->i_mode & ISUID)`) and
`tools/fsutil/inode.c:656` (`mode & (07777 | INODE_MODE_FMT)`) confirm
the setuid bit survives from manifest text through the built filesystem
image to the kernel's exec check, provided the octal literal is written
correctly above -- verify with `size`/a manifest dump
(`tools/fsutil/manifest.c:295`, `printf ("mode %#o\n", mode)`) against
the built image before flashing, not by reading the manifest source
alone.

If `serial-login` replaces `/usr/bin/login` in `etc/ttys`'s getty
invocation rather than adding a second path, remove the existing
`file /usr/bin/login` line (manifest, `/usr/bin/login` in the current
listing) so the image does not carry both the full login and the
trimmed replacement. If `login` stays for a transition period (Section
5), keep both entries and switch `etc/ttys` only after the migration
test sequence passes.

## 4. Host WebUI hardening: pico-host/discobsd-web

Ordered by how much of the exposure each change closes; the first two
are the changes that matter most given the finding in Section 1.6 that
there is currently no authentication gate of any kind between the
network and a root-capable serial console.

### 4.1 Default bind to loopback

    # discobsd-web:239
    -    bind = "0.0.0.0"
    +    bind = "127.0.0.1"

A user who wants LAN exposure passes `--bind 0.0.0.0` explicitly; the
tool should not make that choice silently. The `lan_ip()` banner text
(lines 267-271) already prints both the loopback and LAN URLs -- keep
printing both, but only the one matching the actual bind address is
reachable unless the operator opted in.

### 4.2 Require host-side authentication for non-loopback binds

When `bind` resolves to anything other than `127.0.0.1`/`::1`, refuse to
start (or require a `--token FILE` argument naming a shared secret) and
check it in `do_GET`/`serve_ws` before serving `PAGE` or upgrading the
WebSocket -- a `Authorization: Bearer <token>` header or a `?token=`
query parameter checked with `hmac.compare_digest` against a
locally-generated, file-permission-protected secret is enough for a
single-operator tool; it does not need a user database, since the host
already owns "real" auth per the stated division of responsibility. The
check belongs at the top of both `do_GET` (line 138) and `serve_ws`
(line 149), before any bytes go to the client.

### 4.3 Validate the WebSocket Origin header

In `serve_ws` (line 149), before computing the accept hash, read
`self.headers.get("Origin")` and compare it against the host/port the
server itself is bound to (or an explicit allowlist for the LAN case).
Reject with a plain HTTP error, not a WebSocket upgrade, on mismatch.
This closes the cross-site-WebSocket-hijacking path noted in
Section 1.6 independent of the token check in 4.2 -- a page cannot
forge the token, but Origin validation is the standard, low-cost second
control and catches the loopback case too (a malicious page open in the
same browser that has the tool running on `127.0.0.1`).

### 4.4 Cap WebSocket frame length

In `ws_read` (line 113), reject the 64-bit-length marker before it is
ever unpacked, and cap the 16-bit case too -- 65536 bytes is generous
for keystroke traffic. The check has to sit inside the branch that
reads the length byte (lines 120-124), not after: by the time `ln` holds
a decoded value, a legitimate 127-byte frame (`ln == 127` from the 7-bit
field, the boundary case RFC 6455 reserves for "read more") is
indistinguishable from the marker that already triggered the 8-byte
unpack, so testing `ln == 127` afterward rejects ordinary short frames
and never catches the oversize case it was meant for:

    ln = h[1] & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", rf.read(2))[0]
    elif ln == 127:
        return None, None  # 64-bit length marker: refuse before reading it
    if ln > 65536:
        return None, None

### 4.5 Pin the CDN assets locally

`XTERM`/`XTERM_CSS` (lines 29-30) load from `cdn.jsdelivr.net` at
runtime. This is a supply-chain and offline-availability issue more than
a direct auth issue, but it belongs in the same hardening pass: vendor
`xterm.min.js`/`xterm.min.css` into `pico-host/` and serve them from a
new `do_GET` branch (`self.path == "/xterm.js"` etc.), removing the
external fetch and the CDN as a trust dependency for a tool whose entire
job is a root-capable console.

### 4.6 udev rule: dedicated group

`distrib/rp2040/host/71-discobsd-pico.rules` and
`pico-host/71-discobsd-pico.rules` line 13 gains `GROUP=` and `MODE=`,
and the symlink-only line becomes:

    SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", ATTRS{serial}=="rp2040", SYMLINK+="discobsd", GROUP="discobsd", MODE="0660", TAG+="systemd"

paired with a one-line note in `USER-ACCESS.md` to create the group and
add the operating user to it (`groupadd discobsd && usermod -aG discobsd
$USER`). This stops the node's permissions from depending on whatever
generic serial-device rule a given distribution happens to ship
(Section 1.6), and keeps `discobsd-web`/`discobsd-term` runnable only by
accounts the host administrator explicitly added.

## 5. Migration and risk

### 5.1 The lockout risk

The board in the maintainer's hands may currently boot an image where
root truly has no password -- the tree's own docs assert this in three
places (Section 1.2), and whichever is true for a given flashed image,
the failure mode to avoid is the same: a `ttys` or `passwd`/`shadow`
edit that locks root before an alternate path to a root shell is proven
to work leaves the board recoverable only by reflashing, since there is
no MMU-protected recovery console and no second communication path
besides the one serial line this profile is busy locking down.

The order of operations below is built to make every step reversible
until the last one, and to prove the replacement path works while the
old one is still live.

### 5.2 Migration sequence

1. **Add, do not yet remove.** Add `operator` to `wheel` (Section 2.4)
   and set `operator`'s `etc/shadow` password field empty via the
   correct offset (Section 2.2). Leave root's `etc/passwd`/`etc/shadow`
   offset and hash, and every `etc/ttys` line's `secure` keyword,
   exactly as they are. Build and flash this image. At this point root
   login is unchanged and still works exactly as before -- this step
   only adds a second, working path.

2. **Prove operator login works.** Over the serial console: at the
   login prompt, type `operator`, press enter with no password typed.
   Confirm the shell is `/bin/sh`, `id` reports the operator UID/GID
   (not 0), and `pwd`/`echo $HOME` show `/operator`.

3. **Prove su-to-root works, password-free, from operator.** This step
   requires the Section 3.1 `su` (password codepath removed) to be
   built and installed -- stock `usr.bin/su` will still prompt here
   regardless of root's hash, per Section 1.5. From the operator shell,
   run `su`. Confirm no password prompt appears at all: the wheel
   membership added in step 1 satisfies the remaining `getgrgid(0)` gate
   (`su.c:107-121`, unchanged), and with lines 124-134 removed there is
   no `pw_passwd`/`crypt()` comparison left to reach regardless of
   whether root's `etc/shadow` hash is empty or set -- root's hash stays
   set, per Section 2.3, and this step's pass/fail is independent of
   that value. Confirm `id` after `su` reports UID 0. Confirm `exit`
   returns to the operator shell without disturbing the still-open
   serial session.

4. **Prove a non-wheel account cannot su to root.** Temporarily log in
   as another non-wheel, non-root account (or add a throwaway one) and
   confirm `su` prints "You do not have permission to su root" and exits
   2, per `su.c:115-117`.

5. **Only after steps 2-4 pass on the actual board**, flip
   `etc/ttys`'s `console` (and any enabled) line from `secure` to
   `insecure` (Section 2.3) and rebuild/reflash. Immediately re-test:
   confirm a direct `root` login attempt at the console prompt is
   refused with "root login refused on this terminal" before any
   password prompt appears, and re-confirm step 2 and step 3 still pass
   on the same boot.

6. **Keep a known-good fallback image on hand until step 5 is verified
   on hardware, not just reasoned about from the source.** The `flash.uf2`
   /`flash.bin` this profile replaces is the recovery path if step 5's
   image locks the board in a way steps 2-4 did not predict; BOOTSEL
   reflash is the only recovery available once root login is refused at
   the tty level, so that fallback image must be reachable (on the flashing
   host, not only "in git history") before step 5 is attempted, not
   after.

7. **Strip and re-manifest last.** Once steps 1-6 are verified on
   hardware, remove `crypt`-adjacent, password-changing (`/usr/bin/passwd`
   stays only if a future account needs to set its own password; this
   profile's single operator account never does, so it can go), Kerberos
   `#ifdef` paths, remote-login flag handling, and `lastlog` machinery
   from the RP2040 profile's manifest and build, and switch `etc/ttys`'s
   getty invocation from `/usr/bin/login` to `/usr/libexec/serial-login`
   (Section 2.5) if the trimmed binary was built and independently
   tested against steps 2-4 first. This step is size and attack-surface
   cleanup after the account model is proven, not a prerequisite for it
   -- do not combine it with step 5's `ttys` edit in one untested flash.

### 5.3 What each step is a falsifier for

Step 2 falsifies "operator's shadow offset actually points at an empty
field" (Section 2.2) -- a password prompt appearing means the offset
math is wrong, not that the design is wrong. Step 3 falsifies "wheel
membership plus the Section 3.1 `su` build is sufficient for
password-free escalation" -- a prompt appearing here means either
`operator` is missing from wheel (check the comma-separated list,
Section 2.4) or the flashed image still carries stock `usr.bin/su`
rather than the modified build (check the manifest's setuid bit
actually landed, Section 3.2, before assuming the binary itself is
wrong). Step 4 falsifies "the wheel gate actually excludes
non-members" -- if a non-wheel account reaches root, `su.c`'s
`getgrgid(0)` lookup is resolving something other than the intended
`wheel` group, most likely because `etc/group`'s GID 0 line was edited
incorrectly. Step 5's re-test falsifies "insecure-tty root refusal
does not also break the operator/su path it shares a getty invocation
with." Each step that fails stops the sequence at that step; nothing
downstream is attempted until the failing step's root cause is
identified in the source, not worked around.

### 5.4 Documentation edits owed

Four lines assert a claim Section 1.2 shows is false for the current
tree, and will be false in a different way once `operator` exists --
neither should say "root" once this profile lands:

    README.md:90                                    Log in to DiscoBSD with user `root` and a blank password.
    distrib/stm32/README.md:67                       Log in to DiscoBSD with user `root` and a blank password.
    sys/arch/rp2040/doc/USER-ACCESS.md:41             Log in as root with an empty password.
    pico-host/discobsd-web:13 (comment)               Log in as root with an empty password.

`distrib/stm32/README.md` documents a different port (STM32, not
RP2040); confirm whether that board's own account model actually has a
blank root password before editing its line -- this profile governs
the RP2040 port only, and the STM32 line is listed here only because it
carries the same doc-vs-shadow discrepancy Section 1.2 found, not
because this document changes STM32's account model. For the RP2040
lines (README.md:90, USER-ACCESS.md:41, discobsd-web:13), replace with
"Log in as `operator`, no password. `su` to root without a password if
`operator` is in `wheel`; direct root login is refused." once step 5
of Section 5.2 is verified -- editing these before the hardware proof
passes leaves the documentation ahead of what the image actually does,
the same failure mode Section 1.2 diagnosed in the first place.
