/*
 * What the stand-in 32-bit entry points of narrow_calls.c saw and what
 * they hand back, in plain integer types so the 64-bit test unit and the
 * 32-bit stand-in unit read one layout.
 */
#ifndef NARROW_CALLS_H
#define NARROW_CALLS_H

enum { NARROW_SLOTS = 4 };

extern int narrow_calls;		/* entry points entered since reset */
extern int narrow_null;			/* the time argument was a null pointer */
extern int narrow_old_null;		/* the second time argument was null */
extern unsigned long narrow_seen_sec[NARROW_SLOTS];
extern long narrow_seen_sub[NARROW_SLOTS];
extern unsigned long narrow_give_sec[NARROW_SLOTS];
extern long narrow_give_sub[NARROW_SLOTS];
extern int narrow_result;

void narrow_reset(void);

#endif
