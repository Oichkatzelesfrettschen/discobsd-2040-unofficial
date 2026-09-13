# Integration review: sh-lineedit, stevie-vi, swapram

Reviewer pass ahead of merging `sh-lineedit` and `stevie-vi` into `main` and
reflashing the board. Both branches were read via `git show`/`git diff
main..<branch>` from a detached scratch worktree
(`~/worktrees/discobsd/review-scratch`) with no changes made to either
branch. The swapram tier already ships on `main`
(`sys/arch/rp2040/rp2040/swapram.c`, `swapram_pool.c`, `sys/kern/vm_swap.c`)
and is re-reviewed against the six findings in
`sys/arch/rp2040/doc/research/codex-findings.txt`.

Findings were cross-checked in two conversational rounds with Codex CLI
(`codex exec -c model_reasoning_effort=medium -s workspace-write`, model
`gpt-daybreak-blue-latest` -- the model the user asked for; it was accepted
without falling back to the default). Codex independently reproduced the
`bin/sh/edit.c` bare-ESC byte-eating bug and the stevie-vi `:r` mid-buffer
`Fileend` desync, and surfaced the `nextline() == NULL` and `:w`/`writeit()`
findings below, which this review then verified line-by-line against the
source before including them. The `sh-lineedit` bare-ESC bug was additionally
confirmed by compiling `bin/sh/edit.c` under `HOSTBUILD` and driving
`editline()` through a pipe with the byte sequence `a b ESC c CR`: the
returned line is `ab`, not `abc` -- the `c` after the stray ESC is silently
dropped.

## High

1. **`bin/sh/edit.c:518-520,642-644` (stdsigs vs. editline) -- SIGHUP/SIGTERM
   during editing skip `tty_cooked()` and exit through the shell's own
   handler.** `bin/sh/main.c:162` calls `stdsigs()` once at shell startup,
   which (`bin/sh/fault.c:94-115`) installs `done` (calls `exit` after
   running trap 0, `bin/sh/error.c:56`) as the handler for `SIGHUP` and
   `SIGTERM`, and `fault` (longjmp to `errshell`) for several others.
   `editline()` only ever swaps out `SIGINT` (`edit.c:520`, restored at
   `edit.c:642`); it never touches these other handlers, and they remain
   armed for the whole time editline() is blocked in `read()` with the tty in
   CBREAK/no-echo (`edit.c:518`, `tty_raw()`). A `SIGHUP` (USB CDC-ACM line
   drop, a very normal event on this board) or a `kill -TERM` delivered while
   the user is mid-line never reaches editline()'s cleanup at `edit.c:642-644`
   -- `done()`/`fault()` run and terminate/unwind the shell directly, leaving
   the tty in CBREAK, no-echo. The next session on that console inherits an
   unusable terminal until `stty sane`/reset.
   Fix: register a small `atexit()`-equivalent (this port has no libc
   `atexit`; add a global `editline_active` flag `tty_cooked()`-checked from
   `done()`/`fault()` before they call `exit`/`longjmp`), or have
   `editline()` install its own handlers for `SIGHUP`/`SIGTERM`/`SIGQUIT`
   that call `tty_cooked()` then re-raise/chain to the shell's handler.

2. **`usr.bin/stevie/cmdline.c:138-141`, `linefunc.c:21-27`,
   `main.c:337-348` -- `:r` with the cursor on the last line passes `NULL`
   as the insertion pointer, and `readfile()` writes through it.**
   `nextline(Curschar)` returns `NULL` when there is no line after the
   cursor (`linefunc.c:25-26`) -- true for every empty buffer and for any
   cursor position on the file's last line. `cmdline.c:138-141` passes that
   `NULL` straight to `readfile(arg, pp, 1)` with no check. Inside
   `readfile()` (`main.c:337`), the guard `if (fromp >= Filemax)` is false for
   `fromp == NULL` (comparing address 0 against a large heap pointer), so
   the loop `for (p=Fileend; p>fromp; p--) *p = *(p-1);` (`main.c:344-345`)
   walks `p` from `Fileend` down through address 1, writing through every
   address in between -- far below `Filemem` -- and then `*fromp++ = c;`
   (`main.c:346`) dereferences a null pointer. Trivially reached: open a new
   file (`stevie newfile`) and run `:r template` -- the buffer is empty, so
   `nextline()` returns `NULL` on the very first character. This crashes or
   corrupts memory, not just the edit buffer.
   Fix: in `cmdline.c`'s `:r` handler, `pp = nextline(Curschar); if (pp ==
   NULL) pp = Fileend;` before calling `readfile()`.

3. **`usr.bin/stevie/main.c:325-328` -- a `:r` of a nonexistent/misspelled
   filename silently erases the current buffer.** `readfile()`'s
   `fopen()`-failure path unconditionally sets `Fileend = Filemem;`
   (`main.c:326`) before returning. That reset is correct for the initial
   load and for `:e` (both call it with an already-empty buffer,
   `cmdline.c:97,119`), but `:r` (`cmdline.c:132-143`) calls `readfile()` on
   a buffer that can hold unsaved edits, and does not special-case this
   return. The whole in-memory file is truncated to zero length, and
   `cmdline.c:141` then marks it `CHANGED` -- a subsequent `:w`/`:wq`
   overwrites the real file on disk with an empty one. Trivially reached: a
   typo in a `:r` filename discards the buffer.
   Fix: give `readfile()` a `nochangename`-style flag (or a return code the
   caller checks) so a failed open on a `:r` call leaves `Fileend` and the
   buffer untouched; only the initial-load/`:e` callers (which already reset
   `Fileend` themselves before calling) should get the truncate-on-failure
   behavior.

4. **`usr.bin/stevie/main.c:337-348` -- a successful `:r` before the current
   end of buffer desyncs `Fileend` from the true content, silently dropping
   the buffer's tail (and can overrun the allocation by one byte when the
   buffer is already full).** `readfile()`'s insert loop only raises
   `Fileend` with `if (Fileend < fromp) Fileend = fromp;` (`main.c:347-348`),
   not unconditionally the way `inschar()`/`insstr()`/`appchar()` in
   `misccmds.c` do (`Fileend++`/`Fileend += n` after every guarded shift,
   `misccmds.c:181-182,200-202,220-221`). When `fromp` (the `:r` insertion
   point) is strictly before the pre-existing `Fileend`, each inserted
   character shifts the buffer's true tail one slot to the right without
   growing `Fileend` to match, until `fromp` finally catches up to the old
   `Fileend`. Every byte shifted into that gap is invisible to the rest of
   the editor (nothing iterates past `Fileend`) until then -- the file's
   trailing bytes are silently dropped from what `:w` will write out. If the
   buffer is already at capacity (`Fileend == Filemax - 1`, the largest legal
   value) when this happens, the very first shift iteration writes to
   `*Fileend` at the old value -- one byte past the tracked content but
   still one byte short of `Filemax`, so this specific case does not
   overrun the malloc'd block, but it does start silently discarding
   content on the very first inserted character. This is a routine `:r`
   into the middle of a file (e.g. `:r header.h` above existing code), not
   an edge case.
   Fix: make `readfile()`'s insertion loop bump `Fileend` unconditionally by
   one per character read, the same pattern `inschar()` uses, rather than
   the conditional `if (Fileend < fromp)`.

5. **`usr.bin/stevie/cmdline.c:93-97` -- plain `:w` clears the
   "unsaved changes" flag even when the write fails.** `writeit()`
   (`cmdline.c:235-247`) already calls `UNCHANGED` itself on success
   (`cmdline.c:245`) and returns 0 on a failed `fopen()`
   (`cmdline.c:234-236`). But the `:w` handler in `cmdline.c:93-96` calls
   `writeit(Filename); UNCHANGED;` unconditionally, ignoring the return
   value -- so a failed write (read-only filesystem, permission error,
   directory missing) still clears `Changed`. `:q` (`cmdline.c:85-90`)
   trusts that flag to decide whether to warn; with it cleared, `:q` exits
   silently and the edits are gone with no write having actually happened.
   Fix: `if (arg == NULL) writeit(Filename); else writeit(arg);` -- drop the
   redundant, unconditional `UNCHANGED;` at `cmdline.c:96` and let
   `writeit()`'s own call be the only one.

## Medium

6. **`usr.bin/stevie/cmdline.c:235-247` (`writeit`) -- `putc()`/`fclose()`
   errors are not checked.** A full filesystem or device on the way out
   (QSPI NOR, if `:w` ever targets it) can truncate the written file
   mid-stream while `writeit()` still reports success and clears `Changed`
   (see #5's mechanism -- this is the same silent-success problem one layer
   deeper). Fix: track `ferror(f)` across the `putc()` loop and the
   `fclose()` return, and only report success/clear `Changed` when both are
   clean.

7. **`bin/sh/edit.c:564-580` -- a bare ESC (or ESC followed by anything but
   `[`) blocks the prompt on a second `read()` and silently discards the
   next byte typed.** Confirmed empirically (see above): the sequence `ESC`
   then `c` returns the line `ab` for input `abESCc<CR>`, not `abc`. On a
   real tty this is not a hang forever -- it is exactly the same blocking
   state as the normal idle prompt -- but the user sees no feedback for
   pressing ESC (common on terminals that use it as an Alt-key prefix, or
   from vi-trained fingers) and the next keystroke vanishes rather than
   being inserted or echoed. Task's concern (b) is confirmed as a real,
   reproducible defect, not merely a latent one.
   Fix: after reading ESC, poll `fdin` for readiness with a short timeout
   (a minimal method on this tree: a 1-byte non-blocking `read()` after
   setting `O_NONBLOCK` transiently, or add a poll()/select() wrapper) and
   if nothing follows within the timeout, treat it as a no-op ESC and go
   back to normal key handling for whatever arrives next instead of
   `continue`-ing past it. At minimum, when the second byte is not `[`,
   push it back into the normal per-character dispatch instead of
   discarding it (a one-byte "ungetc" local to `editline()` is enough).

8. **`sys/arch/rp2040/rp2040/swapram.c:209`,
   `sys/arch/rp2040/rp2040/swapram_pool.c:51-58` -- pool fragmentation
   under long-running swap churn still forces avoidable flash fallback.**
   Confirmed still present from `codex-findings.txt` item 6. The allocator
   is pure first-fit with coalescing on free and no compaction; a mixed
   sequence of swap-out/swap-in across differently sized processes can
   leave enough total free bytes (`swapram_pool_avail()`) split across
   segments too small individually to satisfy a new worst-case reservation
   (`swapram_out()`, `swapram.c:209-216`), even though
   `swapram_pool_largest()` exists (`swapram_pool.c:157-165`) to report the
   largest contiguous run. That function is never called outside
   `sys/arch/rp2040/test/swapram/swapram_test.c` -- production code has no
   visibility into whether a flash fallback was "pool full" or "pool
   fragmented," and no compaction path to recover from the latter. This is
   not a correctness bug -- the flash fallback at `sys/kern/vm_swap.c:132`
   already covers the failure -- but it silently increases NOR wear and
   swap latency over the device's life exactly when the RAM tier was
   supposed to prevent that.
   Fix (no code change required to be safe to ship): call
   `swapram_pool_largest()` alongside `swapram_pool_avail()` in the
   `swapramdebug` print at `swapram.c:211-214`, so a fragmented-vs-full
   flash fallback is distinguishable in the field. Compaction (moving live
   images to defragment) is a larger change and is scope for a follow-up,
   not this integration.

## Low

9. **`bin/sh/edit.c:525-539,567-580` -- SIGINT during ESC-sequence parsing
   is not observed until the next full keystroke.** `got_intr` is only
   checked right after the outer `read()` at the top of the loop
   (`edit.c:527`); a SIGINT that arrives while blocked in one of the nested
   nested reads inside the ESC branch (`edit.c:567,569,575,578`) causes that
   nested `read()` to return `EINTR`/-1, which `continue`s back to the outer
   `read()` -- so the line-clear-and-redraw response to Ctrl-C is deferred
   until another byte arrives, not immediate. Not a hang (the process is
   still blocked exactly as it would be at an idle prompt) and not
   reachable via a normal single ESC keypress, only via Ctrl-C landing
   inside a longer escape sequence -- narrow window, cosmetic delay. Fix:
   check `got_intr` immediately after each of the nested reads in the ESC
   branch too, not only after the outer one.

10. **`bin/sh/edit.h:15-18` vs. `edit.c:651-655` -- `editline()`'s header
    comment promises the output buffer is "NUL-terminated defensively," but
    the implementation only copies `n` bytes with no trailing NUL.**
    `main.c`'s caller does not rely on this (it uses the returned length,
    `main.c:146-147`), so this is not exploitable today, but it is a
    documented contract the code does not honor, and a future caller that
    trusts the comment over the return value will over-read. Fix: add
    `if (n < bufsz) buf[n] = '\0';` before the `return n;` at `edit.c:656`.

11. **`bin/sh/edit.c:163-170` -- `edit_onintr()` reinstalls itself with a
    plain `signal()` call, which this port's own comment says is not
    BSD-reliable (SysV semantics: the handler is reset to default on
    entry).** There is a narrow race between signal delivery and the
    reinstall where a second, near-simultaneous SIGINT hits `SIG_DFL` and
    terminates the shell instead of being caught -- the same class of bug as
    finding #1 (tty left in CBREAK), triggered by a double Ctrl-C instead of
    an external signal. Extremely narrow window (microseconds), acceptable
    for an interactive human typist; noted for completeness. No fix
    required for this integration; `sigaction()` with `SA_RESTART` (if this
    libc ever gains it) would close the window for good.

12. **`usr.bin/stevie/window.c:30-74` and `bin/sh/edit.c` (raw-mode entry)
    -- no signal handler at all runs on SIGQUIT/SIGKILL from outside the
    session.** In both branches, `SIGKILL` is inherently uncatchable in any
    program (accepted industry-wide limitation, not a defect to fix here).
    stevie-vi additionally installs no handler for any signal (`ISIG` is
    cleared in both its RAW and HOSTBUILD paths, so keyboard Ctrl-C/Ctrl-\
    never generate a signal while editing) -- only an external `kill` can
    wedge its terminal, same mechanism and same fix direction as finding #1.
    Tracked as one Low item since the fix (route cleanup through the
    process's termination signal handlers) is the same for both tools and
    is sized as a small follow-up, not a blocker.

13. **`sys/arch/rp2040/rp2040/swapram.c:150` (`sr_expand`) -- decoder
    success is judged solely by output length (`out == rlen`), not by full
    consumption of the compressed input or the decoder reaching
    `HSDR_FINISH_DONE`.** Confirmed still present from `codex-findings.txt`
    item 3. Only reachable if the pool's own bytes are corrupted (bit flip,
    a bug elsewhere writing past a reservation) -- by the time this matters,
    something else has already gone wrong. Cheap to harden: after the loop
    at `swapram.c:166-186`, additionally require `in == clen` and
    `fres == HSDR_FINISH_DONE` before returning `out`, so a corrupted image
    panics loudly instead of silently restoring the wrong bytes.

14. **`sys/arch/rp2040/rp2040/swapram_pool.c:86` -- `off + want > mp->p_size`
    remains a textbook unsigned-overflow-prone bounds check in the
    standalone allocator API.** Confirmed still present, and confirmed
    unreachable through every current production caller: `off` always comes
    from a prior successful `swapram_pool_alloc()`, and `want`/`newsize` are
    codec-bounded (`SR_WORST()`) or already-validated trim sizes -- nothing
    in `sys/kern/vm_swap.c` or `swapram.c` passes attacker- or
    corruption-controlled values into `swapram_pool_free()`/`_trim()`. Worth
    hardening because the file's own header comment advertises it as a
    general first-fit allocator other callers may reuse later, at which
    point the assumption stops holding. Fix:
    `if (off > mp->p_size || want > mp->p_size - off) return -1;`.

15. **`sys/arch/rp2040/rp2040/swapram.c:247-248,261-262`
    (`swapram_put`/`swapram_commit`) -- codec/accounting failures panic
    with no path to release the in-flight reservation back to the pool and
    retry through flash.** Confirmed still present, and confirmed that
    ordinary operation cannot reach it: `SR_WORST()`'s `n + n/8 + 4` bound
    covers heatshrink's proven worst case (9 bits/byte for an
    all-literal stream, i.e. `9n/8` bits, plus at most one flush byte)
    regardless of the encoder's window/lookahead configuration, since that
    configuration only affects back-reference encoding, not the literal
    worst case. This panic is functioning as an assertion on an invariant
    the code has already proven, not a live bug. Recommended for defense in
    depth only, as a follow-up: give `swapram_put()`/`swapram_commit()`
    error returns and a `swapram_abort()` that frees `e_off`/`e_res`, so a
    future change to the codec or its configuration fails safe (falls back
    to flash) instead of panicking.

## Items from codex-findings.txt re-verified this pass and found not
## reachable (no action needed)

- **Zero-byte image / `swapram_pool_free(..., size=0)` panic on swapin**
  (prior item 1): not reachable. `sys/kern/vm_swap.c:130` always passes
  `USIZE` (3072, a nonzero compile-time constant) as `ulen`, and
  `swapram.c:187` always compresses that segment; heatshrink cannot encode a
  nonempty input into zero output bytes, so `e_fill`/`e_res` can never reach
  zero for a live entry, and `swapram_in()`'s `swapram_pool_free(&sr_map,
  e->e_off, e->e_res)` (`swapram.c:306`) never receives `size == 0` on any
  path exercised by this kernel.
- **Unprotected shared encoder/decoder/pool state, reentrancy risk** (prior
  item 5): not reachable on this target. RP2040 swap runs on one core with
  no preemption inside `swapram_out`/`_put`/`_commit`/`_in` (the module
  comment at `swapram.c:15-21` documents this invariant, and nothing in
  `sys/kern/vm_swap.c` calls back into this path from an interrupt context).
  This stops being true the day SMP or kernel preemption is added to this
  port -- worth a one-line `#error`-style guard or a comment cross-reference
  if that ever becomes a live roadmap item, but not a defect today.

## Verdict

**sh-lineedit: not safe to flash as-is.** Finding #1 (SIGHUP/SIGTERM wedges
the tty out from under `editline()`) is a realistic, easily triggered
failure on a board whose console is a USB CDC-ACM line that can drop
carrier mid-session. Finding #7 (bare-ESC eats the next keystroke) is
already confirmed and will surprise every user who touches the Esc key
out of vi habit. Both are small, well-scoped fixes (see above); recommend
applying #1 and #7 before merge. #9, #10, #11 are safe to defer.

**stevie-vi: not safe to flash as-is.** Findings #2 through #5 are all in
the `:r` and `:w` paths -- exactly the two commands anyone editing a real
file will use -- and #2 (`nextline()` returning `NULL` into `readfile()`) is
a memory-safety bug reachable by running `:r` on a brand-new file, which is
an entirely ordinary first action. #3, #4, and #5 are silent-data-loss bugs
in the write and read-into-buffer paths, not crashes, and are individually
easy to hit by a plain typo or a full filesystem. Recommend fixing #2-#5
before merge; #6, #12 can follow.

**swapram (already on main): no High or Medium correctness bug found; safe
to continue running as shipped.** The six-item prior review holds up under
re-inspection: two items were already unreachable given this kernel's actual
call pattern (verified here, not merely re-asserted), one is a real but
low-consequence hardening gap unique to hypothetical future callers, one is
a proven-unreachable assertion, one is a genuine defense-in-depth data-
integrity gap gated behind pool corruption that would already be
catastrophic, and one (fragmentation/`swapram_pool_largest()` unused) is a
real, live observability and efficiency gap worth a one-line fix
(item #8) but not a correctness defect blocking continued use.
