# Report to the 2.11BSD maintainer: `_fwalk` in patch 499

This report was drafted with an AI assistant. The finding and the patch are
offered for your judgment; nothing here has run on a PDP-11 or under SIMH, and
the report says so wherever it matters. It has not been sent: it is retained
here under `AGENTS.md`, which requires an explicit request naming it before a
report leaves this tree.

---

## Finding

`_fwalk` in `lib/libc/stdio/findfp.c`, as published in patch 499, never
advances its list cursor and tests the wrong sense of `_flags`, so
`fflush(NULL)` and `exit()` loop forever once a program has more than
`FOPEN_MAX` streams open at one time.

## Mechanism

From the patch 499 text at `http://www.2bsd.com/2.11BSD/499`, the new
`lib/libc/stdio/findfp.c`:

```c
int
_fwalk(int (*function)(FILE *))
{
	register FILE *fp;
	register struct flink *g;
	register int n, ret;

	ret = 0;
	for (n = 0, fp = __sF; n < FOPEN_MAX; n++, fp++)
		if (fp->_flags != 0)
			ret |= (*function)(fp);
	for (g = fpole; g; g->next)
		if (g->sfile._flags == 0)
			ret |= (*function)(&g->sfile);
	return (ret);
}
```

Two separate faults in the second loop:

1. The third clause of the `for` is `g->next`, not `g = g->next`. The
   expression is evaluated and its value discarded, so `g` keeps its initial
   value. The loop's controlling expression `g` therefore never becomes null
   and the loop does not terminate.

2. The membership test is inverted. `_flags == 0` marks a *free* stream. The
   new `stdio.h` comments the field "flags, below; this FILE is free if 0",
   and `__sfp` in the same file claims a slot on exactly that test:

   ```c
   for (n = 0, fp = __sF; n < FOPEN_MAX; fp++, n++)
           if (fp->_flags == 0)
                   goto found;
   ```

   The static-array loop immediately above in `_fwalk` uses the correct
   `_flags != 0`. As written, the dynamic loop would visit only free streams
   and skip every live one, so even with the advance repaired it would flush
   nothing.

Compare 4.4BSD's `_fwalk`, which tests `fp->_flags != 0` and advances with
`g = g->next`.

## Consequence a user sees

`fpole` is null until a program needs a ninth simultaneous stream, because
`__sF` holds `FOPEN_MAX` and patch 499 sets `FOPEN_MAX` to 8. Once `__sfp`
has malloc'd a `struct flink`, two paths reach the defect:

- `fflush(NULL)` returns `_fwalk(__sflush)` directly, from the new
  `lib/libc/stdio/fflush.c`.
- `_cleanup`, which `exit()` calls, is `(void) _fwalk(__sflush)`.

So a program that opens its ninth stream hangs at exit, with its buffered
output unwritten, rather than terminating. Fault 1 makes the hang
unconditional; fault 2 means buffered data in dynamically allocated streams
would be lost even after the advance is repaired.

Programs that stay within eight streams are unaffected, which is most of
them, and is presumably why the patch tested clean.

## Environment

| Surface | Value |
| --- | --- |
| Source | patch 499 as published at `http://www.2bsd.com/2.11BSD/499`, fetched over plain HTTP on 2026-09-19 |
| Also present in | a git import of the patch tape, at `lib/libc/stdio/findfp.c` lines 143 to 145 |
| Verified how | reading the published patch text and the `stdio.h` it installs |
| Not verified | no PDP-11, no SIMH run, no compiled 2.11BSD system |

HTTPS to `www.2bsd.com` refused the connection from here; the fetch was plain
HTTP. If that matters to you for provenance, the same text is in the patch
tape.

## Evidence rank

This is a reading of the published source against the header it ships with,
and against the 4.4BSD original it derives from. It is not a measurement. The
non-termination of a `for` whose increment clause has no effect is a language
fact rather than an observation, but the reachability claim -- that a ninth
stream is what populates `fpole` -- rests on `FOPEN_MAX` being 8 in the
shipped `stdio.h` and on `__sfp` being the only writer of `fpole`, both read
from the patch text and neither executed.

## Reproduction

Not run here. On a 2.11BSD system with patch 499 applied, this should hang at
`exit`:

```c
#include <stdio.h>

int
main(void)
{
	int i;

	/* FOPEN_MAX is 8; the ninth stream comes from fpole. */
	for (i = 0; i < 9; i++)
		if (fopen("/etc/motd", "r") == NULL) {
			perror("fopen");
			return 1;
		}
	printf("opened nine streams\n");
	return 0;	/* exit -> _cleanup -> _fwalk */
}
```

`fflush(NULL)` in place of the return should hang at the same point. If it
does not hang, the reachability argument above is wrong and I would like to
know it.

## Patch

Against `/usr/src/lib/libc/stdio/findfp.c` at patch level 499. 2.11BSD
distributes patches as diffs rather than as `git am` mailboxes, so this is a
plain unified diff and carries no git metadata.

```diff
--- lib/libc/stdio/findfp.c.orig
+++ lib/libc/stdio/findfp.c
@@
 	ret = 0;
 	for (n = 0, fp = __sF; n < FOPEN_MAX; n++, fp++)
 		if (fp->_flags != 0)
 			ret |= (*function)(fp);
-	for (g = fpole; g; g->next)
-		if (g->sfile._flags == 0)
+	for (g = fpole; g; g = g->next)
+		if (g->sfile._flags != 0)
 			ret |= (*function)(&g->sfile);
 	return (ret);
 }
```

Suggested message:

    libc: advance and test the dynamic stream list in _fwalk

    The second loop of _fwalk used g->next as its increment expression,
    which evaluates and discards the pointer rather than advancing g, so
    the loop never terminated once fpole was non-empty. It also tested
    _flags == 0, which marks a free stream, where the static-array loop
    above and 4.4BSD both test _flags != 0.

    fflush(NULL) and _cleanup, which exit() calls, both reach _fwalk, so
    a program holding more than FOPEN_MAX streams hung at exit with its
    buffered output unwritten.

## Measurement before and after

None. No build of 2.11BSD was made here, so no text or data delta is offered.
The change is two tokens and should not move either measurably.

## What this leaves open

- Whether any shipped 2.11BSD program opens more than eight streams, which
  decides whether this is latent or actually biting users.
- `_cleanup` flushes through `__sflush` with the comment `` `cheating' `` and
  the `_fwalk(fclose)` call commented out just above it. Repairing `_fwalk`
  makes that path reach streams it previously skipped, so if any of them are
  in a state `__sflush` does not expect, the fix could surface a second
  problem. I have not analyzed that.
- I have not reviewed the rest of patch 499's stdio for defects of the same
  shape; this one was found while evaluating the patch for a downstream port,
  not by a systematic audit.
