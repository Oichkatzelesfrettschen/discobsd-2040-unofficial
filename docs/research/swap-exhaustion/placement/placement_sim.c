/*
 * Raw-swap placement study.  The program drives one modeled shell session
 * through several placement policies over the RP2040 swap map and reports,
 * per policy, how often a fork extent is refused while enough blocks are
 * free, and how the erases spread over the 4 KiB sectors.
 *
 * The cyclic next-fit policy is the kernel's: the Makefile links
 * sys/kern/subr_rmap.c and the policy calls malloc3_contiguous_next().  The
 * other policies work on the same struct map through the production mfree().
 * guarded() with a zero guard must place every extent where the production
 * function does; main() checks that on every allocation of every trace.
 *
 * docs/research/swap-exhaustion/placement-study.md states the workload
 * model and reads the numbers.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>

/* The kernel headers stand in for libc's; these three come from the host. */
int atoi (const char *);
void exit (int) __attribute__ ((noreturn));
void *memset (void *, int, size_t);

size_t rmap_malloc3_contiguous_next (struct map *mp, size_t d_size,
    size_t s_size, size_t u_size, size_t align, size_t *nextp, size_t a[3]);
void rmap_mfree (struct map *mp, size_t size, size_t addr);

#define ALIGN       4           /* SWAP_IMAGE_ALIGN */
#define FIRST       4           /* init_main.c frees [4, nswap) */
#define NSWAP       384
#define SECTORS     (NSWAP / ALIGN)
#define GUARD       196         /* btod(192 KiB window) + btod(USIZE) */
#define MAPENTS     32

enum policy {
    NEXT_FIT,                   /* production cyclic next-fit */
    GUARDED,                    /* next-fit that keeps a GUARD-block run */
    BOUNDARY,                   /* cursor picks the run, low boundary placed */
    BEST_FIT,                   /* smallest fitting run, low boundary */
    STRICT,                     /* guarded, target fixed at GUARD */
    NEXT_FIT_MOVE,              /* next-fit, then relocation on refusal */
    GUARDED_MOVE,               /* guarded, then relocation on refusal */
    NPOLICY
};

static const char *policy_name[NPOLICY] = {
    "next-fit", "guarded", "boundary", "best-fit", "strict",
    "next+move", "guard+move"
};

struct state {
    struct mapent ents[MAPENTS + 2];
    struct map map;
    size_t next;
    unsigned long erases[SECTORS];
    unsigned long blocks_written;
};

void
panic (char *msg)
{
    printf ("panic: %s\n", msg);
    exit (2);
}

void
log (int level, char *fmt, ...)
{
    (void) level;
    printf ("log: %s\n", fmt);
    exit (2);
}

static void
state_init (struct state *s, size_t cursor)
{
    memset (s, 0, sizeof (*s));
    s->map.m_map = s->ents;
    s->map.m_limit = s->ents + MAPENTS;
    s->map.m_name = "swapmap";
    rmap_mfree (&s->map, NSWAP - FIRST, FIRST);
    s->next = cursor;
}

static size_t
largest_run (const struct state *s)
{
    const struct mapent *bp;
    size_t best = 0;

    for (bp = s->ents; bp->m_size; bp++)
        if (bp->m_size > best)
            best = bp->m_size;
    return best;
}

static size_t
free_total (const struct state *s)
{
    const struct mapent *bp;
    size_t total = 0;

    for (bp = s->ents; bp->m_size; bp++)
        total += bp->m_size;
    return total;
}

/* Remove [base, base + span) from the free run that contains it. */
static void
take (struct state *s, size_t base, size_t span)
{
    struct mapent *bp, *endp, *move;
    size_t run_base, run_end;

    for (endp = s->ents; endp->m_size; endp++)
        continue;
    for (bp = s->ents; bp->m_size; bp++)
        if (base >= bp->m_addr && base + span <= bp->m_addr + bp->m_size)
            break;
    if (bp->m_size == 0)
        panic ("take: not free");
    run_base = bp->m_addr;
    run_end = run_base + bp->m_size;
    if (base == run_base) {
        bp->m_addr += span;
        bp->m_size -= span;
        if (bp->m_size == 0)
            do {
                *bp = bp[1];
            } while ((bp++)->m_size != 0);
    } else if (base + span == run_end) {
        bp->m_size -= span;
    } else {
        if (endp >= s->map.m_limit)
            panic ("take: map full");
        for (move = endp; move > bp; move--)
            move[1] = move[0];
        bp[1].m_addr = base + span;
        bp[1].m_size = run_end - bp[1].m_addr;
        bp->m_size = base - run_base;
    }
}

/* Largest free run that remains when [base, base + span) leaves run bp. */
static size_t
largest_after (const struct state *s, const struct mapent *run, size_t base,
    size_t span)
{
    const struct mapent *bp;
    size_t best = 0, left, right;

    for (bp = s->ents; bp->m_size; bp++)
        if (bp != run && bp->m_size > best)
            best = bp->m_size;
    left = base - run->m_addr;
    right = run->m_addr + run->m_size - (base + span);
    if (left > best)
        best = left;
    if (right > best)
        best = right;
    return best;
}

/*
 * Next-fit in cursor order that keeps a free run of min(guard, largest run)
 * blocks whenever some placement does.  Inside a run the cursor position is
 * tried first, then the lowest aligned position that leaves the target run
 * below it, then the run's high boundary.  When every placement breaks the
 * target, the placement that leaves the largest run wins, earliest in
 * cursor order.  A zero guard reduces to the production search with a spare
 * descriptor.
 */
static size_t
guarded (struct state *s, size_t span, size_t guard, int strict)
{
    struct mapent *bp;
    size_t target, candidate, end, slid, after, best_after = 0;
    size_t tries[3], selected = 0, fallback = 0;
    int pass, i, ntries, found = 0, have_fallback = 0;

    target = largest_run (s);
    if (target > guard || strict)
        target = guard;
    for (pass = 0; pass < 2 && !found; pass++) {
        for (bp = s->ents; bp->m_size && !found; bp++) {
            end = bp->m_addr + bp->m_size;
            if (bp->m_size < span)
                continue;
            if (pass == 0) {
                candidate = s->next > bp->m_addr ? s->next : bp->m_addr;
                candidate = (candidate + ALIGN - 1) & ~(size_t) (ALIGN - 1);
                if (candidate > end - span)
                    continue;
            } else {
                candidate = bp->m_addr;
                if (candidate >= s->next)
                    continue;
            }
            ntries = 0;
            tries[ntries++] = candidate;
            slid = (bp->m_addr + target + ALIGN - 1) & ~(size_t) (ALIGN - 1);
            if (slid > candidate && slid <= end - span &&
                (pass == 0 || slid < s->next))
                tries[ntries++] = slid;
            if (end - span > candidate && (pass == 0 || end - span < s->next))
                tries[ntries++] = end - span;
            for (i = 0; i < ntries; i++) {
                after = largest_after (s, bp, tries[i], span);
                if (after >= target) {
                    selected = tries[i];
                    found = 1;
                    break;
                }
                if (!have_fallback || after > best_after) {
                    have_fallback = 1;
                    best_after = after;
                    fallback = tries[i];
                }
            }
        }
    }
    if (!found) {
        if (!have_fallback)
            return 0;
        selected = fallback;
    }
    take (s, selected, span);
    s->next = selected + span;
    return selected;
}

/* The cursor picks the run as next-fit does; the extent takes its low end. */
static size_t
boundary (struct state *s, size_t span)
{
    struct mapent *bp;
    size_t end, base;
    int pass;

    for (pass = 0; pass < 2; pass++)
        for (bp = s->ents; bp->m_size; bp++) {
            end = bp->m_addr + bp->m_size;
            if (bp->m_size < span)
                continue;
            if (pass == 0 ? end <= s->next : bp->m_addr >= s->next)
                continue;
            base = bp->m_addr;
            take (s, base, span);
            s->next = end;
            return base;
        }
    return 0;
}

static size_t
best_fit (struct state *s, size_t span)
{
    struct mapent *bp, *best = NULL;
    size_t base;

    for (bp = s->ents; bp->m_size; bp++)
        if (bp->m_size >= span && (best == NULL || bp->m_size < best->m_size))
            best = bp;
    if (best == NULL)
        return 0;
    base = best->m_addr;
    take (s, base, span);
    return base;
}

static unsigned long mismatches;

static size_t
place (struct state *s, enum policy policy, size_t span)
{
    size_t a[3], base = 0;

    switch (policy) {
    case NEXT_FIT:
    case NEXT_FIT_MOVE: {
        struct state shadow = *s;

        shadow.map.m_map = shadow.ents;
        shadow.map.m_limit = shadow.ents + MAPENTS;
        if (rmap_malloc3_contiguous_next (&s->map, span, 0, 0, ALIGN,
            &s->next, a) != 0)
            base = a[0];
        /* Calibration: a zero guard reproduces the production choice. */
        if (guarded (&shadow, span, 0, 0) != base)
            mismatches++;
        break;
    }
    case GUARDED:
    case GUARDED_MOVE:
        base = guarded (s, span, GUARD, 0);
        break;
    case STRICT:
        base = guarded (s, span, GUARD, 1);
        break;
    case BOUNDARY:
        base = boundary (s, span);
        break;
    case BEST_FIT:
        base = best_fit (s, span);
        break;
    default:
        break;
    }
    if (base != 0) {
        size_t sector;

        for (sector = base / ALIGN; sector < (base + span) / ALIGN; sector++)
            s->erases[sector]++;
        s->blocks_written += span;
    }
    return base;
}

static unsigned long long rng_state;

static unsigned
rng (unsigned bound)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned) ((rng_state >> 33) % bound);
}

struct result {
    unsigned long requests[3], refused[3], fragmented[3];
    unsigned long sleeper_refused, moves, moved_blocks;
    double wear_ratio_sum;
    unsigned long sessions, blocks_written;
};

static const size_t fork_span[3] = { 68, 108, 184 };

/*
 * Relocation on refusal: while no free run holds `span` blocks, the smallest
 * held extent that has not moved goes to the placement that leaves the
 * largest free run.  Each move rewrites the extent once.
 */
static void
relocate (struct state *s, size_t span, size_t *held[], const size_t size[],
    int count, struct result *r)
{
    int moved[8], i, pick;
    size_t sector, base;

    for (i = 0; i < count; i++)
        moved[i] = 0;
    while (largest_run (s) < span) {
        pick = -1;
        for (i = 0; i < count; i++)
            if (*held[i] != 0 && !moved[i] &&
                (pick < 0 || size[i] < size[pick]))
                pick = i;
        if (pick < 0)
            return;
        moved[pick] = 1;
        rmap_mfree (&s->map, size[pick], *held[pick]);
        base = guarded (s, size[pick], NSWAP, 1);
        if (base == 0)
            panic ("relocate: lost extent");
        *held[pick] = base;
        for (sector = base / ALIGN; sector < (base + size[pick]) / ALIGN;
            sector++)
            s->erases[sector]++;
        s->blocks_written += size[pick];
        r->moves++;
        r->moved_blocks += size[pick];
    }
}

#define MAX_SLEEPERS 3

/*
 * One login session.  The sleepers are swapped out once and stay.  Every
 * command takes `transients` 20-block extents at the cursor and keeps the
 * last one (the invoking shell, asleep in wait) until the command ends; a
 * forking command then asks for one extent of its own span.
 */
static void
session (enum policy policy, unsigned long long seed, int nsleepers,
    int transients, int late_sleepers, int commands, struct result *r)
{
    static const size_t sleeper_span[MAX_SLEEPERS] = { 20, 12, 8 };
    struct state s;
    size_t sleeper[MAX_SLEEPERS], shell, child, spare;
    unsigned long max = 0, total = 0;
    int placed = 0, command, i, kind, sector;

    rng_state = seed;
    state_init (&s, FIRST + ALIGN * rng ((NSWAP - FIRST) / ALIGN));
    memset (sleeper, 0, sizeof (sleeper));
    for (command = 0; command < commands; command++) {
        shell = 0;
        for (i = 0; i < transients; i++) {
            spare = place (&s, policy, 20);
            if (i + 1 == transients)
                shell = spare;
            else if (spare != 0)
                rmap_mfree (&s.map, 20, spare);
        }
        kind = rng (20);
        child = 0;
        if (kind < 9) {
            kind = kind < 4 ? 0 : kind < 7 ? 1 : 2;
            r->requests[kind]++;
            if ((policy == NEXT_FIT_MOVE || policy == GUARDED_MOVE) &&
                largest_run (&s) < fork_span[kind] &&
                free_total (&s) >= fork_span[kind]) {
                size_t *held[4] = { &shell, &sleeper[0], &sleeper[1],
                    &sleeper[2] };
                const size_t size[4] = { 20, sleeper_span[0],
                    sleeper_span[1], sleeper_span[2] };

                relocate (&s, fork_span[kind], held, size, 4, r);
            }
            if (largest_run (&s) < fork_span[kind]) {
                r->refused[kind]++;
                if (free_total (&s) >= fork_span[kind])
                    r->fragmented[kind]++;
            } else
                child = place (&s, policy, fork_span[kind]);
        }
        /*
         * A sleeper goes out before the first command, or, in the late
         * variant, while a command's extents are still held.
         */
        if (placed < nsleepers &&
            (!late_sleepers || command >= 2 + 3 * placed)) {
            sleeper[placed] = place (&s, policy, sleeper_span[placed]);
            if (sleeper[placed] == 0)
                r->sleeper_refused++;
            placed++;
        }
        if (child != 0)
            rmap_mfree (&s.map, fork_span[kind], child);
        if (shell != 0)
            rmap_mfree (&s.map, 20, shell);
    }
    for (sector = FIRST / ALIGN; sector < SECTORS; sector++) {
        total += s.erases[sector];
        if (s.erases[sector] > max)
            max = s.erases[sector];
    }
    r->wear_ratio_sum += (double) max * (SECTORS - FIRST / ALIGN) / total;
    r->blocks_written += s.blocks_written;
    r->sessions++;
}

int
main (int argc, char **argv)
{
    int sessions = argc > 1 ? atoi (argv[1]) : 2000;
    int commands = argc > 2 ? atoi (argv[2]) : 400;
    int nsleepers, transients, late, policy, i, kind;

    printf ("sessions=%d commands=%d guard=%d\n", sessions, commands, GUARD);
    printf ("%-9s %-5s %-4s %-10s  %7s %7s %7s  %8s %6s %9s\n", "sleepers",
        "trans", "late", "policy", "ref68", "ref108", "ref184",
        "wear max", "kB/cmd", "moved/1k");
    for (nsleepers = 1; nsleepers <= MAX_SLEEPERS; nsleepers += 2)
        for (transients = 1; transients <= 2; transients++)
            for (late = 0; late <= 1; late++)
                for (policy = 0; policy < NPOLICY; policy++) {
                    struct result r;

                    memset (&r, 0, sizeof (r));
                    for (i = 0; i < sessions; i++)
                        session (policy, 0x9e3779b97f4a7c15ULL * (i + 1),
                            nsleepers, transients, late, commands, &r);
                    printf ("%-9d %-5d %-4d %-10s ", nsleepers, transients,
                        late, policy_name[policy]);
                    for (kind = 0; kind < 3; kind++)
                        printf (" %6.2f%%", r.requests[kind] ? 100.0 *
                            r.fragmented[kind] / r.requests[kind] : 0.0);
                    printf ("  %8.2f %6.1f %9.1f\n",
                        r.wear_ratio_sum / r.sessions,
                        (double) r.blocks_written / r.sessions / commands,
                        1000.0 * r.moved_blocks / r.sessions / commands);
                    if (r.sleeper_refused != 0)
                        printf ("  sleeper refused: %lu\n",
                            r.sleeper_refused);
                }
    printf ("zero-guard placements differing from production: %lu\n",
        mismatches);
    return mismatches != 0;
}
