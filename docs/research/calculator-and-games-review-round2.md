# Calculator and games review, round 2: adjudication of the Codex and Grok reports

Advisory adjudication on PR #345.

Scope: branch `picocalc/calculator-and-games` at `c7160321`, base `main` at
`6a3cbc88`. Two independent reviews of that head are adjudicated here: a
Codex report of 19 demonstrated findings and a Grok report of 10 demonstrated
and 3 suspected findings. Each finding was decided from the source and, where
a probe was cheap, from a host execution. The 32 reported items merge into 28
rows: 20 confirmed, 6 partly confirmed, 2 refuted. A further 17 defects of the
same classes that neither report names are listed after the table.

Evidence class. Every probe below is a host execution: libcalc and libbn
sources compiled with `cc -std=c17 -Wall -Wextra -Werror` (libbn under
AddressSanitizer and UndefinedBehaviorSanitizer), the fourfall host build on a
Linux pty, and `tools/check_multicall_composition_test.py` fixtures under
`PYTHON=python3`. The host is IEEE binary64. No probe exercised the RP2040
boot-ROM float provider, qemu-arm, Renode, a cross build, the image, a board or
a serial port. The probe programs were scratch files outside the tree; the
commands are reproducible from the descriptions in each row.

Baseline runs on this head: `tools/check_multicall_composition_test.py` exits 0
(6 live-mode cases and 19 ledger mutations rejected). `tests/libcalc/calc_test.c`
against the shipped sources reports `PASS calc_test: 13511 checks`. The same
test against a scratch copy carrying the condition-estimate, complex-atanh and
log1p fixes proposed in rows 9 and 11 also reports `PASS calc_test: 13511
checks`, so those fixes regress nothing the suite measures.

## Authority for the libcalc range rows

`lib/libcalc/mathematics.h:27-28` promises: "A finite input of normal
magnitude whose exact result is a normal double gives the documented value
within the accuracy the tests state." Every libcalc range row below is a
counterexample to that sentence: normal inputs, a normal exact result, and an
overflowed or NaN output returned with `CALC_OK`. The choice per row is to fix
the code or to narrow that sentence. The fixes proposed share one primitive: a
power-of-two scale built from the IEEE exponent field in `calc_internal.h`
(the same union idiom as `calc__fabs`), which is exact for normal values on
both the host and the ROM provider, costs integer operations plus one soft
`dmul` per scaled value, and adds an estimated 60 to 100 bytes of Thumb text
once. Each scaled path runs only when the unscaled result is non-finite with
finite inputs, so the common path pays nothing. No libcalc or libbn consumer
ships in the image yet (`INT-3` in `docs/research/calculator-and-games-review.md`:
`lib/Makefile` `SUBDIR` waits for the first program), so these rows are
library-contract defects, not user-visible image defects.

## PID derivative recurrence: independent Tustin derivation

The derivative acts on the measurement: D(s) = -Kd s / (tau s + 1) M(s).
Substitute the bilinear transform s = (2/T)(1 - z^-1)/(1 + z^-1):

    (tau (2/T)(1 - z^-1)/(1 + z^-1) + 1) D = -Kd (2/T)(1 - z^-1)/(1 + z^-1) M

Multiply both sides by T(1 + z^-1):

    (2 tau (1 - z^-1) + T (1 + z^-1)) D = -2 Kd (1 - z^-1) M
    (2 tau + T) d[n] - (2 tau - T) d[n-1] = -2 Kd (m[n] - m[n-1])
    d[n] = (-2 Kd (m[n] - m[n-1]) + (2 tau - T) d[n-1]) / (2 tau + T)

The pole is z = a with a = (2 tau - T)/(2 tau + T), the bilinear image of
s = -1/tau. `lib/libcalc/calc_pid.c:55-57` computes
`-(2 Kd dm + (2 tau - T) d[n-1]) / (2 tau + T)`, which negates the
feedback term and moves the pole to z = -a. For tau much larger than T the
code's pole sits near z = -1, the Nyquist mode, so the "low-pass" derivative
rings with alternating sign instead of decaying monotonically.

Probe (Kd = tau = T = 1, measurements 1, 1, 1, a = 1/3): the code gives
d = -2/3, +2/9, -2/27; the recurrence above gives -2/3, -2/9, -2/27. This
matches the Codex numbers. A ramp m[n] = n T r discriminates in steady state:
the correct recurrence settles at d = -Kd r, the shipped one at
-Kd r T / (2 tau), equal only when T = 2 tau.

The recurrence is the one in `docs/research/picomite-maths-clean-room-spec.md:556`
(the legacy formula, carried without a "Required" correction) and in the
oracle at `tests/libcalc/calc_test.c:1974-1975`. With the corrected
recurrence the first test vector becomes
`2.025, 1.384285714285714, 0.796377551020408, -0.122733236151604,
0.165814244064973` (its tau = 0.02 is below T/2 = 0.05, so a = -0.43 and the
correct response still rings, decaying). `calc_pid_init` accepts tau = 0
(`calc_pid.c:15`); there the shipped recurrence degenerates to an accumulator
(probe: d holds -2 for a held step) and the correct one to an undamped
alternation of +-2Kd/T, so tau = 0 has no useful meaning under either form.

## Merged findings, by severity

| Id | Sources | Verdict | Evidence | Fix | Gate that catches the class |
| --- | --- | --- | --- | --- | --- |
| 1 | Codex 1 | CONFIRMED, major | `tools/check_multicall_composition.py:603` iterates `boxes` from the ledger, and `validate_archive_summary` (`:808-811`) takes its consumer set from the same ledger. Fixture probe: appending `pack /usr/games/unlistedbox` and `link /usr/games/notindispatch` / `target /usr/games/unlistedbox` to the live manifest is accepted. The live manifest already differs from the ledger: 85 links, 81 of them to multicall boxes, against the ledger's 84 and 80. | Derive the live box set from the live tree: every directory whose Makefile includes `share/mk/multicall-bss-overlay.mk` (the seven today), its `BINDIR` and `PROG`, and every manifest `link` whose target is a `pack`. Require the multicall link targets to equal the derived installed paths, and list the remaining link targets (`/usr/bin/compress`, `/usr/bin/stevie` today) as non-multicall explicitly. Feed the same list to the root Makefile relink loop (missed item C2) and to the archive consumer set. | Live-mode fixture case: unlisted packed box with a link must be rejected naming the box; second case: a box Makefile added under `sbin/` without a manifest entry must be rejected. |
| 2 | Codex 14, Grok demonstrated 2 | CONFIRMED, major | `check_multicall_composition.py:638-647`: live mode rejects shipped names outside dispatch but never requires dispatch names to ship; the equality with the ledger's `unshipped_dispatch_names` runs only in exact mode. Fixture probe: removing the `bubble` and `fixturetool` link pairs while both stay in `TOOLS` is accepted. The image then lacks `/usr/games/bubble` while gamebox still links its code. | In live mode require `set(dispatch) - shipped_basenames` to equal a live declaration (an `UNSHIPPED_TOOLS` Makefile variable read through `read_make_variable`, empty for every box today) instead of the ledger field. | Fixture case: drop one link with its name kept in `TOOLS`; must be rejected as an undeclared unshipped dispatch name. |
| 3 | Codex 2 | CONFIRMED, major (mitigated) | `check_multicall_composition.py:610-616` binds `PROG` to the ledger only in exact mode; `live_measurement_records` (`:668`) measures `makefile-dir/PROG`. Fixture probe with `PROG=different_gamebox`: accepted, and the multicall row measured `games/gamebox/different_gamebox`. The installed path `box["installed_path"]` also comes from the ledger, not from the live `BINDIR`. The `${FSIMG}` prerequisite would normally fail to pack `/usr/games/gamebox` from a fresh `DESTDIR`, so exploitation needs a stale install tree; the gate itself proves nothing about identity. | Require `Path(installed_path).name == PROG` and `installed_path == BINDIR + "/" + PROG` from live Make variables in every mode, and measure that path. | Fixture case: `PROG` differing from the installed basename must be rejected. |
| 4 | Codex 15 | CONFIRMED, minor | `check_multicall_composition.py:620` is a substring test. Fixture probe: `${LINK_MAP_FLAG}` removed from the link recipe and kept only in a trailing comment is accepted. The root target (`Makefile:764-767`) cleans the box but never removes `$$map_root/$$program.map`, so a stale map from an earlier run then supplies the archive members. | Delete each map in the Makefile loop before relinking, and have the checker reject a map older than the box binary it describes. | Fixture case: comment-only flag plus a pre-existing map must be rejected; Makefile contract test in `makefile_contract_failures` requires the `rm -f` line. |
| 5 | Codex 3 | CONFIRMED, major | `lib/libcalc/calc_stats.c:151` passes local `&v` to `calc_variance`, then writes `*out` unchecked at `:155`. Probe: `calc_stddev({1}, 1, 0, NULL)` is killed by SIGSEGV. `mathematics.h:11-12` promises `CALC_E_ARG`. | Test `out == NULL` first. | Add a NULL-output sweep to `calc_test.c` that calls every status-returning entry point with each required pointer NULL and expects `CALC_E_ARG`; a `calc_mutate.py` mutant deleting one guard must be rejected. |
| 6 | Codex 4 | CONFIRMED, major | Derivation above; probe reproduces the Codex sequence. Code, spec and oracle share the error, so the existing vector cannot catch it. | Change the sign of the feedback term in `calc_pid.c:55-57`, correct the spec formula at `picomite-maths-clean-room-spec.md:556` as a "Required" divergence from legacy, regenerate the vector, and reject tau = 0 (`tau > 0`) or document it. | Property tests independent of any stored vector: ramp steady state `d -> -Kd r`; for tau >= T/2 a held step gives a monotonically decaying, sign-constant derivative. Both reject the shipped recurrence. |
| 7 | Codex 11 | CONFIRMED, major (shipped) | `games/fourfall/fourfall.c:1303-1321` handles the key, then charges the whole wait to the post-input state. Model probe built on the logic half: a move 400 ms into the lock timer leaves 100 ms of the 500 ms (`FF_LOCK_MS`); charging before input leaves 500 ms. Pause and game-over waits are charged on resume, clamped to 1000 ms (`:1316-1317`). | Compute `dt` and call `ff_advance` before `readkey`/`handle`; the pause and game-over transitions then charge nothing because the state is not `S_PLAY` during the wait. | Move the loop body into a logic-half step function and add `logic_test.c` checks: a reset at 400 ms leaves 500 ms; resume after a 5 s pause advances 0 ms. A mutant restoring the shipped order must fail. |
| 8 | Codex 7 | CONFIRMED, major | `lib/libcalc/calc_complex.c:337-338`: for x = -1, `4x/d` rounds to -1 and `log1p(-1)` is -inf. Probe: `atanh(-1+1e-10i)` returns `CALC_E_RANGE`; reference -11.8594990552502+0.785398163422448i. The same core serves `atan` (`:379`), so `atan(1e-10+1i)` also returns `CALC_E_RANGE` (missed item L1), and the +1 side fails for tiny imaginary parts (missed item L2). | Evaluate on `|x|` and copy the sign of x onto the real part (exact odd symmetry); for `d < 2^-1000` use `0.5 (log hypot(1+|x|, y) - log hypot(1-|x|, y))`. Scratch patch: all four pole approaches return the reference values and the suite stays at 13,511 passing checks. Cost: about 30 bytes, one comparison on the common path. | Pole-approach vectors for atanh at +-1 and atan at +-i with imaginary or real offsets 1e-10, 1e-153 and 1e-160, generated by `gen_complex_vectors.py` from mpmath or an independent formula. |
| 9 | Codex 5 | CONFIRMED, minor | `calc_matrix.c:336-341`: the alternating test vector has entries up to 2 and the result is scaled after the solve, so `sum_abs(y)` overflows. Probe: `DBL_MIN * I` for n = 2 and n = 12 gives cond = inf and `calc_mat_inverse` returns `CALC_E_ILLCOND`; `2^-1000 * I` passes. The band is narrow (entries below about 2e-307). | Scale the test vectors before the solves: alternating vector times `2/(3n)`, sign vector `z` entries `+-1/n`. Every intermediate is then bounded by `norm1(A^-1)` and the estimate is algebraically unchanged. Scratch patch: cond = 1 for both sizes, inverse `CALC_OK`, 13,511 checks pass. Cost: about 12 bytes, 2n multiplies. | Scale-invariance family (shared gate below): cond(2^k A) == cond(A) exactly for k = +-1000 and A = `DBL_MIN * I`. |
| 10 | Codex 6 | CONFIRMED, minor | `calc_matrix.c:212-214` multiplies the diagonal directly. Probe: `diag(2^600, 2^600, 2^-600, 2^-600)` gives inf, and the reversed order gives 0 (missed item L7); the exact determinant is 1. | Accumulate a mantissa in [1, 2) and an integer exponent from the exponent field, apply the exponent once at the end. Cost: about 60 bytes, n <= 12 iterations of integer work. | Permutation test: every ordering of a diagonal with exact determinant 1 returns exactly 1. |
| 11 | Codex 9 | CONFIRMED, major | Probes: mean and median of `[1e308, 1e308]` are inf; population variance is inf; `correl(x, x)` for `x = [-1e200, 1e200]` is NaN; the 2x2 table `[[1,2],[3,4]]` gives 0.0793650793650794 but the same table times 1e300 gives NaN. All return `CALC_OK`. Mechanisms: `list_sum / n` (`calc_stats.c:67`), `0.5 * (x + y)` (`:123`), `d * d` (`:141`, `:211-213`), `rowtot * coltot / total` and `d * d / e` (`:375-377`). | Mean, variance, correlation: when the plain result is non-finite with finite inputs, rerun on inputs scaled by `2^-e` (e from the largest exponent) and rescale; correlation is scale-free, so scale each list independently and skip the rescale. Median: `x + 0.5 (y - x)` when the signs agree, else `0.5 (x + y)`. Chi-square: reorder to `rowtot * (coltot / total)` and `d * (d / e)`, which needs no scaling until a total overflows. Cost: zero on the common path, an estimated 150 to 250 bytes. | Scale-invariance family: mean, median, standard deviation and chi-square scale by exactly `2^k`, variance by `2^2k`, correlation is unchanged, for k = +-1000 on finite lists. |
| 12 | Codex 8 | CONFIRMED, major | `calc_vector.c:76-80` and `calc_quat.c:89-91, 107-111` divide by a full magnitude that overflows. Probes: `normalize([DBL_MAX]*3)` returns `[0 0 0]` with `CALC_OK`; `axis_angle(DBL_MAX axis, 90 deg)` returns `(0.707107 0 0 0)`; `quat_from_vector(DBL_MAX*3)` returns the zero quaternion with magnitude inf (missed item L3). | Return the largest component m and the scaled root `sqrt(sum (a_i/m)^2)` from `norm`, and form `(a_i / m) * inv` with `inv = 1 / root`. One extra multiply per element, about 10 bytes. | Scale-invariance family: `normalize(2^k v) == normalize(v)` bit for bit, k = +-1000 and `DBL_MAX`-scaled inputs. |
| 13 | Codex 12, Grok demonstrated 1 | PARTLY CONFIRMED, minor on target, major on host | `fourfall.c:1288-1290` enters raw mode before installing handlers, and only SIGINT and SIGTERM are caught. Pty probe: SIGQUIT leaves ICANON and ECHO off; SIGTERM restores both. On the target the sgtty RAW flag bypasses signal-character processing (`sys/kern/tty.c:1051`), so ^C, ^\ and ^Z generate no signal there and a signal arrives only from kill(1) or hangup; on the host termios path ISIG stays on (`games/gametty.h:30`). Codex's async-safety claim is half right: POSIX lists `tcsetattr` among async-signal-safe functions (XSH 2.4.3); `ioctl` is not listed, though here it is a direct trap with no libc state. | Install SIGINT, SIGTERM, SIGHUP and SIGQUIT handlers before `gtty_raw`; have them set a `volatile sig_atomic_t` flag; break the loop on the flag after `wait_input` (select already returns EINTR into the loop at `:1298`) and restore through the normal exit path. Handle SIGTSTP by restoring, re-raising with the default action and re-entering raw mode on SIGCONT, as `games/worm/worm.c:99-101, 229-232` does. | Extend `test_signals` in `games/fourfall/tests/play.py` to SIGQUIT, SIGHUP and SIGTSTP/SIGCONT, asserting ICANON and ECHO after each through `termios.tcgetattr` on the pty master. |
| 14 | Codex 10 | CONFIRMED, minor | `calc_elem.c:283-284` treats `|exponent| > 1e9` as non-integral, so `(-1)^1000000001` and `(-1)^1e300` return `CALC_E_DOMAIN` (probe), against `mathematics.h:362-364`. | Define integral as finite and `e == floor(e)`; choose the algorithm separately: `pow_int` for `|e| <= 2^31`, otherwise `sign * exp(e log|x|)` with the sign from parity (every `|e| >= 2^53` is even). About 30 bytes. | Vectors at `e = 2^31 + 1`, `2^53`, `1e300` for bases -1, -2 and -0.5. |
| 15 | Codex 13, Grok suspected 1 | CONFIRMED (truncation), PARTLY CONFIRMED (split arrows) | `fourfall.c:1117-1147` tests `FIONREAD` once, then blocks on each further byte. Pty probe: after `ESC [` the game emits 0 bytes in 1.5 s (648 bytes in the baseline interval), the next `q` is consumed as the final byte, and only a second `q` quits. The bare-Esc decision has no inter-byte delay, so an arrow sequence split across UART or USB deliveries reads as pause; that split is not measured on a board. | An incremental escape parser in the event loop: on ESC, wait up to a fixed deadline (25 to 50 ms) for the next byte through `wait_input`; abandon an incomplete sequence at the deadline; never block in `read` past `select`. | `play.py` cases: `ESC [` then `q` quits within the deadline; `ESC` then a 10 ms gap then `[A` rotates; gravity output continues during a truncated sequence. |
| 16 | Codex 18 | CONFIRMED, minor | `fourfall.c:808, 824` emit SGR 90; `sys/arch/rp2040/dev/lcdterm.c:389-396` decodes 30-37, 39, 40-47 and 49 only, so settled cells and the plumb line draw in the current foreground (white) on the PicoCalc. The host model accepts 90 (`games/fourfall/tests/play.py:391`). | Extend lcdterm rather than fourfall: add `90-97 -> sgr_fg = 8 + (v - 90)` in `do_sgr`. `st7365p.c:94-101` already has a 16-entry palette with index 8 = (127, 127, 127), and `resolve_attr` already carries a 4-bit foreground. Cost: about 16 bytes of kernel Thumb text against 11,484 bytes of headroom in the retained PICOCALC measurement (`sys/arch/rp2040/doc/PICOCALC.md:36`: text 119,332 of 130,816). Emitting `1;30` in fourfall costs the same bytes but renders bold black, invisible on host terminals that do not brighten bold. | `tests/picocalc/lcdterm_test.c`: SGR 90-97 select palette 8-15; `play.py` derives its accepted SGR set from one shared list that `lcdterm_test` also checks. |
| 17 | Codex 16, Grok demonstrated 5 | PARTLY CONFIRMED, minor | Probe: `BN_mask_bits(1, 2)` returns 0 and `BN_mask_bits(0, 0)` returns 1 (`lib/libbn/bn.c:311-318`). OpenSSL's word-index rule returns 1 and 0; that rule depends on `BN_BITS2`, so 32-bit and 64-bit OpenSSL builds differ from each other near word boundaries. The port follows the manual page's "already shorter than n bits" rule, as its comment says. dc calls it only with n = 8 after the length exceeds 8. | Keep the manual-page rule; state it in `bn.h` as a deliberate difference from the libcrypto implementation, and have the dc differential runner record that the case is outside its oracle. | A `bn_test.c` boundary case pinning the documented rule. |
| 18 | Codex 17 | CONFIRMED, minor | `tests/libbn/bn_test.c:243-246, 254-271` ties context choice to alias variant (`v & 1`), so each alias direction runs with one context choice; `op_div` (`:285-327`) never aliases quotient and remainder to the two operands at once. `docs/research/openbsd-dc-bn-port.md:235-237` claims every legal aliasing with and without a `BN_CTX`. | Iterate alias pattern and context choice as independent loops, and add `(dv=a, rem=d)` and `(dv=d, rem=a)`. | The expanded matrix itself; a `bn_mutate.py` mutant that writes the quotient before reading the divisor must fail. |
| 19 | Grok demonstrated 6 | CONFIRMED, minor | `games/fourfall/Makefile:14-15` links with `${CFLAGS}` from `share/mk/sys.mk:83`, which carries `-std=gnu17`; only the host and logic recipes (`:23, :31`) are strict. Commit `5e94d5e2` says strict C17 on host and Cortex-M0+. `lib/libbn/Makefile` and `lib/libcalc/Makefile:14-15` append `-std=c17 -pedantic-errors`, which wins as the later flag. | Append `-std=c17 -pedantic-errors` to fourfall's CFLAGS as the libraries do, or reword the commit. Whether the sgtty headers compile under strict C17 on the cross toolchain is not run. | The cross tier builds fourfall with the strict flags. |
| 20 | Grok demonstrated 7 | CONFIRMED, minor | `games/fourfall/Makefile:10, 31-32` hardcodes `-fsanitize=address,undefined`; `tests/libcalc/Makefile:43` and `tests/libbn/Makefile:38` probe through `tools/compiler-probe.sh` and record a skip. `docs/research/calculator-and-games-review.md:242-244` records the gap as fixed. | Use the same probe and receipt. | Run the target with a compiler lacking the sanitizer runtime; it must record a skip receipt. |
| 21 | Codex 19 | CONFIRMED, minor | `git diff --check main...HEAD` reports space before tab at `docs/research/fourfall-implementation.md:350`; `docs/research/picocalc-games-survey.md:139` uses the British spelling of "color" in prose, while the review's L-5 row claims American spelling throughout. | Fix both. | Add `git diff --check` over the PR range and a British-spelling word list over changed prose (quoted identifiers and `copy-reviews/` exempt) to the lint tier, calibrated on these two lines. |
| 22 | Grok demonstrated 4 | CONFIRMED, minor | Commit `14cd96e3` body says "32-bit limbs"; `lib/libbn/bn.c:4, 31-33` uses 16-bit limbs with 32-bit `BN_ULONG` words. | Reword while the branch history is still pre-review. | None; commit text. |
| 23 | Grok demonstrated 10 | CONFIRMED, minor | `docs/research/calculator-and-games-review.md:288` cites the 17,140-byte trial that `docs/research/fourfall-implementation.md:315-323` marks as predating the redesign; commit `20cde399` and `sys/arch/rp2040/doc/research/games.md:227` record 17,900 bytes raw. The same paragraph estimates 265 free blocks for `full`, while `PROFILES.md` measures 281. | Cite the measured 17,900 bytes and 281 blocks. | None; prose. |
| 24 | Grok demonstrated 3 | PARTLY CONFIRMED, minor | `check_multicall_composition.py:661-677` keeps the last existing directory among `program` and `program2`. It mirrors `sbin/sysbox/Makefile:34-35` exactly, so the checker measures the same directory the box consumes; the duplication of that search in two languages is the defect. `bin/date` (a `date.c` with no Makefile) beside `bin/date2` is a live decoy. | Have each box Makefile export the resolved applet directory (a `-V` variable) and read it through `read_make_variable`; reject a directory without a Makefile. | Fixture case: a `program2` decoy directory lacking a Makefile must be rejected. |
| 25 | Grok demonstrated 8 | PARTLY CONFIRMED, minor | `check_multicall_composition.py:752-753` rejects only zero free blocks or inodes; `docs/research/dpucp-2024-multicall-composition.md:106-108` states positive capacity as the live-mode policy, so the behavior matches its contract. | Keep the policy; print the live free-block count beside the retained 287 so a reviewer sees the delta. A numeric floor is an owner decision. | None unless a floor is adopted. |
| 26 | Grok demonstrated 9 | PARTLY CONFIRMED, minor | `sys/arch/rp2040/doc/PROFILES.md:36-39`: 16 + 18 + 133 = 167 against the measured combined 166 (447 - 281). The profile rows are mutually consistent; closure costs measured by difference need not add when two closures share a directory or indirect block. | One sentence stating that the combined row is measured and the closure costs do not sum. | None; prose. |
| 27 | Grok suspected 2 | REFUTED as a defect | A Hager estimate below the true condition number lets `calc_mat_inverse` return `CALC_OK` past the threshold, but `mathematics.h:207-214, 227-232` defines the status on the estimate and calls it a lower bound. An exact norm needs the computed inverse, and `out` must stay untouched on failure, so it would need an extra n*n buffer. | None. | None. |
| 28 | Grok suspected 3 | REFUTED | Bound: base-10000 groups of a b-bit value are at most `b/13.2877 + 1 <= floor(b/13) + 2 = passes`, and `size = 4 passes + 2` leaves the NUL and the sign. ASan probe: `BN_bn2dec` of `2^k - 1` and its negation for 3,754 bit lengths up to 20,000 (every k to 800, then k = 0 and 12 mod 13) reports no fault and every negative string starts with `-`. | None. | None. |

## Defects missed by both reviews

Composition checker (`tools/check_multicall_composition.py`), live-mode
identity:

- C1. The archive consumer set in live mode is the ledger's (`:808-811`), so a
  new box's link map is never read even if one is produced. Part of the row 1
  fix.
- C2. The root target relinks a hardcoded list of seven boxes (`Makefile:758-761`),
  a second copy of the denominator independent of the checker's. A new box is
  neither relinked nor mapped. Generate the list from the checker's live
  derivation (a `--list-boxes` mode) so one source owns it.
- C3. The pass line (`:1060-1065`) prints the retained ledger's 7 boxes and 80
  shipped names even when live mode measured a different tree (81 multicall
  links today). Print the live denominator in live mode and the ledger's only
  on exact replay.
- C4. Live mode reads `installed_path` from the ledger (`:628, :633`) rather than
  from live `BINDIR` and `PROG`. Part of the row 3 fix.
- C5. Live mode checks shipped names for overlap between boxes (`:642`) but not
  dispatch names, so two boxes may both dispatch one name while only one ships
  it. Require disjoint dispatch sets.

libcalc, overflow-unsafe intermediates (host probes):

- L1. `calc_c_func(CALC_C_ATAN, 1e-10+1i)` returns `CALC_E_RANGE` through the
  shared `catanh_` core; fixed by row 8.
- L2. `atanh(1+1e-153i)` and `atanh(1+1e-160i)` return `CALC_E_RANGE`
  (references 176.494333204324 and 184.553381029804). Two mechanisms:
  `calc__log1p` (`lib/libcalc/calc_util.c:52`) forms `log(u) * x`, which
  overflows for x above about 2.5e305, and `d` underflows for |y| below about
  1e-154. Return `calc__log(x)` for `x > 2^53` in `calc__log1p` and use the
  row 8 log-difference branch; the scratch patch confirms both.
- L3. `calc_quat_from_vector(DBL_MAX, DBL_MAX, DBL_MAX)` returns the zero
  quaternion with `CALC_OK`; fixed by row 12.
- L4. `calc_rescale([-1e308, 0, 1e308] -> [0, 1])` returns `[0, 0, NaN]` with
  `CALC_OK` (`lib/libcalc/calc_interp.c:33-38`): `span` overflows. Compute with
  halved differences (`0.5 hi - 0.5 lo`) when `span` is not finite.
- L5. `calc_interp_table(xs = [-1e308, 1e308], ys = [0, 1], x = 0)` returns 0,
  exact 0.5 (`calc_interp.c:75`). Same halving for the `t` ratio.
- L6. `calc_dot([1e200, -1e200, 1], [1e200, 1e200, 1])` returns NaN, exact 1.
  Narrow the contract here rather than fix: dot, cross product, matrix
  products, rotation, `calc_lerp` and `calc_blend` are raw bilinear forms whose
  formula the header states (`mathematics.h:311, 368`); a scaled dot product
  costs a max pass per call. Add to `mathematics.h:27-28` that the promise
  covers results whose documented intermediate products are finite for these
  routines.
- L7. `calc_mat_det(diag(2^-600, 2^-600, 2^600, 2^600))` returns 0; fixed by
  row 10.
- L8. `calc_pid_init` accepts tau = 0 (`calc_pid.c:15`, `mathematics.h:432`),
  where neither recurrence is a usable derivative; see the derivation section.

fourfall (`games/fourfall/fourfall.c`), terminal state and test coverage:

- F1. The select-failure exit (`:1298-1301`) restores the tty but writes no
  SGR reset and leaves the cursor wherever the last frame parked it, unlike
  the quit path (`:1325-1328`). Route every exit through one restore function.
- F2. `GAMEBOX_TEST` disables gravity and the lock timer (`:1295, :1315`), and
  `test_timed` (`tests/play.py:613-645`) checks only that a piece falls, locks
  and that pause freezes the clock. No test observes the order of input
  handling and time charging, which is how row 7 shipped. The row 7 gate closes
  this.
- F3. On the target, RAW mode delivers ^C as byte 3, which `handle` ignores, so
  `q` is the only keyboard quit; the SIGINT handler serves only an external
  kill. Not a defect; the help line and implementation notes should say so.

## Shared gate for the libcalc range class

One table-driven family in `tests/libcalc/calc_test.c` catches rows 9 to 12
and L2 to L5: for each scale-homogeneous routine f with f(2^k x) = 2^(pk) f(x),
evaluate at k = -1000, -500, 500, 1000 on inputs whose scaled values stay
normal (so the ROM provider's flush-to-zero cannot intervene), and require the
exact power-of-two relation, with p = 1 for mean, median, standard deviation,
determinant per row, magnitude and chi-square, p = 2 for variance, and p = 0
for correlation, normalize and the condition estimate. Add one `calc_mutate.py`
mutant per fix that restores the shipped expression; each must fail.

## Recommended fix order

1. libcalc contract and correctness, smallest diffs first: the
   `calc_stddev` NULL guard with the NULL-output sweep (row 5), the PID
   recurrence with spec, vector and ramp property (row 6), and the condition
   estimate (row 9), whose patch is already measured against the suite.
2. fourfall front-end loop as one change: charge time before input (row 7),
   flag-based signals installed before raw mode (row 13), the deadline-driven
   escape parser (row 15) and the single restore path (F1), with the
   logic-half step function and the `play.py` cases that gate them.
3. Composition checker live denominator as one design (rows 1 to 4, 24 and
   C1 to C5): derive boxes, installed paths, dispatch and unshipped names from
   the live manifest and Makefiles; drive the root relink loop from the same
   list; delete maps before relinking; one fixture case per gap.
4. libcalc range fixes with the shared scale-invariance gate (rows 8, 10 to
   12, 14 and L2 to L5), and the contract narrowing for raw bilinear forms
   (L6) in the same commit as its header text.
5. lcdterm SGR 90-97 (row 16) as its own kernel commit with the
   `lcdterm_test` cases; measure PICOCALC text after it.
6. libbn test matrix and the `BN_mask_bits` statement (rows 17, 18).
7. Build and prose hygiene: fourfall strict C17 image flags (row 19),
   sanitizer probe (row 20), `git diff --check` and the spelling gate (row 21),
   the stale size and profile sentences (rows 23, 26), and the commit-text
   rewrite (row 22) while the branch is still pre-review.

## Resolution

Each row's disposition on branch `picocalc/calculator-and-games`, by commit.

| Rows | Disposition | Commit |
| --- | --- | --- |
| 1, 2, 3, 4, 24, 25, C1-C5 | live denominator derived from the box Makefiles and the manifest; one fixture per gap; the live denominator printed beside the retained one | `5d1046e7` |
| 5 | `calc_stddev` guard and a NULL sweep over 64 entry points | `98b72277` |
| 6, L8 | Tustin recurrence in code, specification and vector; `tau > 0` required; ramp and held-step properties | `ded14a75` |
| 8, 9, 10, 11, 12, 14, L1-L5, L7 | power-of-two scaled reruns, mantissa-exponent determinant, pole-safe complex `atanh`, scaled condition vectors; the scale-invariance family and 21 mutants | `76444355` |
| L6 | the range promise narrowed for the raw bilinear forms | `51956b31` |
| 7, 13, 15, 19, 20, F1, F2, F3 | time charged before input, flag-only handlers installed before raw mode, SIGTSTP restore, a 40 ms escape deadline, one exit path, strict C17 image build, the shared sanitizer probe | `d31d6228` |
| 16 | lcdterm decodes SGR 90-97 and 100-107; kernel text falls by 16 bytes | `bed42223` |
| 17, 18 | aliasing and context as independent indices; the `BN_mask_bits` rule stated and pinned | `3f44476b` |
| 21 | spelling and whitespace fixed, and a calibrated gate for the class | `a536b5b8`, `49e289a6`, `d31d6228` |
| 23, 26 | measured gamebox and profile figures; the closure-sum note | `a536b5b8`, `d31d6228`, `89d0c5de` |
| 22 | not rewritten: the branch history is published, so the PR description corrects "32-bit limbs" to 16-bit limbs in 32-bit words | none |
| 27, 28 | refuted; no change | none |

Residual: the condition estimate is infinite when the inverse's 1-norm
exceeds `DBL_MAX` (for example `1e-300 * [[1, 1], [1, 1 + 1e-8]]`), recorded
in `docs/research/libcalc-implementation.md`.
