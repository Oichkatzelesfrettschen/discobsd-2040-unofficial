/*
 * Reporting and process plumbing for a gate that carries the tree's own
 * userland headers.
 *
 * include/sys/types.h reads "typedef u_int size_t" and include/stdio.h spells
 * stderr as &_iob[2], so a translation unit holding both the tree's headers
 * and the host's would take a four-byte size_t from one and an eight-byte one
 * from the other, and would hand the host's fprintf a FILE the host does not
 * own.  The checking side therefore reaches the tree's headers alone and
 * names here everything it borrows from the host; bg_shim.c is the one
 * translation unit on the other side, and it carries no tree header.
 *
 * At ILP32 the tree's size_t and the host's unsigned are the same four bytes,
 * so a length crossing the boundary means the same thing to both sides.
 */
#ifndef BG_SHIM_H
#define BG_SHIM_H

void	bg_fail(const char *file, int line, const char *what);
void	bg_note(const char *msg);
int	bg_verdict(const char *suite);

/* One byte on a fresh pipe installed as descriptor 0, which is where
   subs.c's readc() reads, and a sink on descriptor 1, which is where its
   buflush() writes. */
void	bg_feed(int byte);
void	bg_silence_stdout(void);

/*
 * Run fn in a child and report its low eight bits, or the child's exit status
 * when fn did not return.  getout() ends backgammon through exit(-1), so 255
 * is how a byte readc() quits on shows up, and every byte readc() hands back
 * is under 128; observing from a child rather than in process is what keeps a
 * regression in the quit character reportable instead of taking the gate down
 * with it.  -2 says the child died on a signal.
 */
int	bg_child(int (*fn)(void));

extern unsigned bg_checks;
extern unsigned bg_failures;

#define CHECK(cond)							\
	do {								\
		bg_checks++;						\
		if (!(cond))						\
			bg_fail(__FILE__, __LINE__, #cond);		\
	} while (0)

#endif /* BG_SHIM_H */
