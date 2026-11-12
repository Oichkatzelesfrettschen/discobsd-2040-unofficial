/*
 * Raw-swap wear study.  The program repeats the session model of
 * placement_sim.c on a sector bitmap and adds placement on a ring: the 95
 * sectors of blocks 4 to 383 joined end to start, so an extent may run past
 * block 383 and continue at block 4.
 *
 * The first policy is the kernel's cyclic next-fit restated on the bitmap.
 * It draws the same random numbers as placement_sim.c, so its refusal and
 * wear columns must equal the `next-fit` rows that program prints from the
 * production allocator; placement-wear.md records the comparison.
 *
 * Wear is reported twice: the mean over sessions of each session's hottest
 * sector against that session's mean, and the same ratio over the erases of
 * all sessions added together, which is what a flash part accumulates.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N           95          /* sectors: blocks 4..383, 4 blocks each */
#define GUARD       49          /* 196 blocks */
#define MAX_SLEEPERS 3

enum policy {
    LIN_NEXT,                   /* production next-fit on the bitmap */
    LIN_GUARD,                  /* interval map, guarded */
    RING_NEXT,                  /* ring, plain cyclic next-fit */
    RING_GUARD,                 /* ring, guarded */
    RING_GUARD_MOVE,            /* ring, guarded, relocation on refusal */
    NPOLICY
};

static const char *policy_name[NPOLICY] = {
    "lin-next", "lin-guard", "ring-next", "ring-guard", "ring-g+move"
};

struct state {
    unsigned char used[N];
    int next;
    unsigned long erases[N];
    unsigned long written;
};

static int
is_ring (enum policy policy)
{
    return policy >= RING_NEXT;
}

static int
fits (const struct state *s, int base, int len, int ring)
{
    int i;

    if (!ring && base + len > N)
        return 0;
    for (i = 0; i < len; i++)
        if (s->used[(base + i) % N])
            return 0;
    return 1;
}

static void
mark (struct state *s, int base, int len, int value)
{
    int i;

    for (i = 0; i < len; i++)
        s->used[(base + i) % N] = value;
}

/* Longest free run; on the ring a run may pass sector N - 1 into sector 0. */
static int
largest (const struct state *s, int ring)
{
    int best = 0, run = 0, i, limit = ring ? 2 * N : N;

    for (i = 0; i < limit; i++) {
        if (s->used[i % N]) {
            run = 0;
            continue;
        }
        if (++run > best)
            best = run;
    }
    return best > N ? N : best;
}

static int
free_total (const struct state *s)
{
    int total = 0, i;

    for (i = 0; i < N; i++)
        total += !s->used[i];
    return total;
}

/*
 * Returns the base sector, or -1.  Positions are tried in cursor order: on
 * the interval the kernel's two passes (at or above the cursor, then run
 * starts below it), on the ring every sector from the cursor round.  A
 * guarded policy takes the first position that leaves a free run of
 * min(guard, largest run) sectors, else the one that leaves the longest.
 */
static int
place (struct state *s, enum policy policy, int len, int guard)
{
    int ring = is_ring (policy), target, k, base, after, best = -1;
    int best_after = -1, selected = -1;

    target = largest (s, ring);
    if (target > guard)
        target = guard;
    for (k = 0; k < N && selected < 0; k++) {
        base = (s->next + k) % N;
        if (!ring && base < s->next && base > 0 && !s->used[base - 1])
            continue;           /* pass one considers run starts only */
        if (!fits (s, base, len, ring))
            continue;
        if (guard == 0) {
            selected = base;
            break;
        }
        mark (s, base, len, 1);
        after = largest (s, ring);
        mark (s, base, len, 0);
        if (after >= target)
            selected = base;
        else if (after > best_after) {
            best_after = after;
            best = base;
        }
    }
    if (selected < 0)
        selected = best;
    if (selected < 0)
        return -1;
    mark (s, selected, len, 1);
    s->next = (selected + len) % N;
    if (!ring && selected + len == N)
        s->next = N;            /* the kernel cursor rests past the map */
    for (k = 0; k < len; k++)
        s->erases[(selected + k) % N]++;
    s->written += len;
    return selected;
}

static unsigned long long rng_state;

static unsigned
rng (unsigned bound)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned) ((rng_state >> 33) % bound);
}

struct result {
    unsigned long requests[3], fragmented[3], moved, sessions, written;
    unsigned long total_erases[N];
    double wear_sum;
};

static const int fork_len[3] = { 17, 27, 46 };      /* 68, 108, 184 blocks */
static const int sleeper_len[MAX_SLEEPERS] = { 5, 3, 2 };

static int
guard_of (enum policy policy)
{
    return policy == LIN_NEXT || policy == RING_NEXT ? 0 : GUARD;
}

/* While the request has no run, the smallest unmoved extent is rewritten. */
static void
relocate (struct state *s, enum policy policy, int len, int *held[],
    const int size[], int count, struct result *r)
{
    int moved[8] = { 0 }, i, pick, base;

    while (largest (s, is_ring (policy)) < len) {
        pick = -1;
        for (i = 0; i < count; i++)
            if (*held[i] >= 0 && !moved[i] &&
                (pick < 0 || size[i] < size[pick]))
                pick = i;
        if (pick < 0)
            return;
        moved[pick] = 1;
        mark (s, *held[pick], size[pick], 0);
        base = place (s, policy, size[pick], N);
        if (base < 0) {
            fprintf (stderr, "relocate: lost extent\n");
            exit (2);
        }
        *held[pick] = base;
        r->moved += size[pick];
    }
}

static void
session (enum policy policy, unsigned long long seed, int nsleepers,
    int transients, int late, int commands, struct result *r)
{
    struct state s;
    int sleeper[MAX_SLEEPERS] = { -1, -1, -1 };
    int shell, child, spare, placed = 0, command, i, kind, ring;
    unsigned long max = 0, total = 0;

    ring = is_ring (policy);
    rng_state = seed;
    memset (&s, 0, sizeof (s));
    s.next = rng (N);
    for (command = 0; command < commands; command++) {
        shell = -1;
        for (i = 0; i < transients; i++) {
            spare = place (&s, policy, 5, guard_of (policy));
            if (i + 1 == transients)
                shell = spare;
            else if (spare >= 0)
                mark (&s, spare, 5, 0);
        }
        kind = rng (20);
        child = -1;
        if (kind < 9) {
            kind = kind < 4 ? 0 : kind < 7 ? 1 : 2;
            r->requests[kind]++;
            if (policy == RING_GUARD_MOVE &&
                largest (&s, ring) < fork_len[kind] &&
                free_total (&s) >= fork_len[kind]) {
                int *held[4] = { &shell, &sleeper[0], &sleeper[1],
                    &sleeper[2] };
                const int size[4] = { 5, sleeper_len[0], sleeper_len[1],
                    sleeper_len[2] };

                relocate (&s, policy, fork_len[kind], held, size, 4, r);
            }
            if (largest (&s, ring) < fork_len[kind]) {
                if (free_total (&s) >= fork_len[kind])
                    r->fragmented[kind]++;
            } else
                child = place (&s, policy, fork_len[kind],
                    guard_of (policy));
        }
        if (placed < nsleepers && (!late || command >= 2 + 3 * placed)) {
            sleeper[placed] = place (&s, policy, sleeper_len[placed],
                guard_of (policy));
            placed++;
        }
        if (child >= 0)
            mark (&s, child, fork_len[kind], 0);
        if (shell >= 0)
            mark (&s, shell, 5, 0);
    }
    for (i = 0; i < N; i++) {
        total += s.erases[i];
        r->total_erases[i] += s.erases[i];
        if (s.erases[i] > max)
            max = s.erases[i];
    }
    r->wear_sum += (double) max * N / total;
    r->written += s.written;
    r->sessions++;
}

int
main (int argc, char **argv)
{
    int sessions = argc > 1 ? atoi (argv[1]) : 2000;
    int commands = argc > 2 ? atoi (argv[2]) : 400;
    int nsleepers, transients, late, policy, i;

    printf ("sessions=%d commands=%d guard=%d sectors\n", sessions,
        commands, GUARD);
    printf ("%-8s %-5s %-4s %-12s %7s %7s %9s %9s %8s %8s\n", "sleepers",
        "trans", "late", "policy", "ref108", "ref184", "wear/sess",
        "wear/all", "kB/cmd", "moved/1k");
    for (nsleepers = 1; nsleepers <= MAX_SLEEPERS; nsleepers += 2)
        for (transients = 1; transients <= 2; transients++)
            for (late = 0; late <= 1; late++)
                for (policy = 0; policy < NPOLICY; policy++) {
                    struct result r;
                    unsigned long max = 0, total = 0;

                    memset (&r, 0, sizeof (r));
                    for (i = 0; i < sessions; i++)
                        session (policy, 0x9e3779b97f4a7c15ULL * (i + 1),
                            nsleepers, transients, late, commands, &r);
                    for (i = 0; i < N; i++) {
                        total += r.total_erases[i];
                        if (r.total_erases[i] > max)
                            max = r.total_erases[i];
                    }
                    printf ("%-8d %-5d %-4d %-12s %6.2f%% %6.2f%% %9.2f "
                        "%9.3f %8.1f %8.1f\n", nsleepers, transients, late,
                        policy_name[policy],
                        100.0 * r.fragmented[1] / r.requests[1],
                        100.0 * r.fragmented[2] / r.requests[2],
                        r.wear_sum / r.sessions, (double) max * N / total,
                        4.0 * r.written / r.sessions / commands,
                        4000.0 * r.moved / r.sessions / commands);
                }
    return 0;
}
