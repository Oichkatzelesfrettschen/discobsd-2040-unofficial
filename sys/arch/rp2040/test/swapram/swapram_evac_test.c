/*
 * Host test for the pool's evacuation to flash. It compiles the kernel's
 * swapram.c and subr_rmap.c against stand-in headers, so the code under
 * test is the shipped tier and the shipped map allocator; swap() writes
 * into an array that stands for the flash swap unit.
 *
 * Images enter the pool the way kern/vm_swap.c admits them (swapram_out,
 * three swapram_put, swapram_commit); an evacuation must then leave every
 * process's expanded bytes at the flash blocks its p_daddr, p_saddr and
 * p_addr name, empty the pool, and when the map cannot hold one image
 * leave every process, the pool and the map exactly as they were.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <machine/swapram.h>
#undef malloc                   /* the shim renamed the map allocator */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

struct proc proc[NPROC];
struct user u, u0;
char runin, runout;

#define NSWAP   512                     /* blocks in the stand-in unit */
static struct mapent swapent[NPROC * 3 + 2];
struct map swapmap[1] = { { swapent, &swapent[NPROC * 3 + 1], "swapmap" } };
static unsigned char flash[NSWAP * DEV_BSIZE];
static int writes;

static jmp_buf onpanic;
static const char *panicmsg;

void
panic(const char *s)
{
    panicmsg = s;
    longjmp(onpanic, 1);
}

void
wakeup(caddr_t chan)
{
    (void)chan;
}

/*
 * A process asleep on the epoch is waiting for the swapper to answer;
 * the swapper's turn is the service call, so sleep runs it.
 */
static int sleeps;
void
sleep(caddr_t chan, int pri)
{
    (void)chan; (void)pri;
    sleeps++;
    swapram_service();
}

/* kern/vm_swp.c stand-in: count bytes from coreaddr to block blkno. */
void
swap(size_t blkno, size_t coreaddr, int count, int rdflg)
{
    if (rdflg != B_WRITE || blkno + btod(count) > NSWAP)
        panic("swap: bad write");
    memcpy(flash + blkno * DEV_BSIZE, (void *)coreaddr, count);
    writes++;
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static size_t
mapfree(void)
{
    struct mapent *ep;
    size_t n = 0;

    for (ep = swapmap->m_map; ep->m_size; ep++)
        n += ep->m_size;
    return n;
}

/* An image: data, stack and u bytes with a compressible texture. */
struct image {
    unsigned char *d, *s, *ub;
    size_t dlen, slen;
};

static unsigned
rnd(void)
{
    static unsigned x = 0x2545f491;

    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

static void
fill(unsigned char *p, size_t n, unsigned seed)
{
    size_t i;

    for (i = 0; i < n; i++)
        p[i] = (rnd() & 7) ? (seed + i / 16) & 0xff : rnd() & 0xff;
}

static void
mkimage(struct image *im, size_t dlen, size_t slen, unsigned seed)
{
    im->dlen = dlen;
    im->slen = slen;
    im->d = malloc(dlen ? dlen : 1);
    im->s = malloc(slen ? slen : 1);
    im->ub = malloc(USIZE);
    fill(im->d, dlen, seed);
    fill(im->s, slen, seed + 77);
    fill(im->ub, USIZE, seed + 99);
}

/* Admit image im for proc slot i the way swapout does; 1 when the pool took it. */
static int
admit(int i, struct image *im)
{
    struct proc *p = &proc[i];

    p->p_pid = 100 + i;
    p->p_flag = 0;
    p->p_daddr = p->p_saddr = p->p_addr = 0;
    if (!swapram_out(p, (caddr_t)im->d, im->dlen, (caddr_t)im->s, im->slen,
        USIZE))
        return 0;
    if (im->dlen)
        swapram_put(p, SWAPRAM_DATA, (caddr_t)im->d, im->dlen);
    if (im->slen)
        swapram_put(p, SWAPRAM_STACK, (caddr_t)im->s, im->slen);
    swapram_put(p, SWAPRAM_U, (caddr_t)im->ub, USIZE);
    swapram_commit(p);
    return 1;
}

static int
onflash(int i, struct image *im)
{
    struct proc *p = &proc[i];

    return (im->dlen == 0 ||
        memcmp(flash + p->p_daddr * DEV_BSIZE, im->d, im->dlen) == 0) &&
        (im->slen == 0 ||
        memcmp(flash + p->p_saddr * DEV_BSIZE, im->s, im->slen) == 0) &&
        memcmp(flash + p->p_addr * DEV_BSIZE, im->ub, USIZE) == 0;
}

static void
reset_map(size_t blocks)
{
    memset(swapent, 0, sizeof swapent);
    mfree(swapmap, blocks, 1);
    memset(flash, 0xee, sizeof flash);
    writes = 0;
}

static void
roundtrip(void)
{
    struct image im[8];
    int i, n = 0, taken;
    size_t before, expect = 0;

    reset_map(NSWAP - 1);
    for (i = 0; i < 8; i++) {
        mkimage(&im[i], (i * 1531) % 5000, i == 3 ? 0 : 300 + i * 97, i * 31);
        taken = admit(i, &im[i]);
        CHECK(taken);
        n += taken;
        expect += btod(im[i].dlen) + btod(im[i].slen) + btod(USIZE);
    }
    CHECK(swapram_images() == n);
    before = mapfree();
    CHECK(swapram_evacuate() == 0);
    CHECK(swapram_images() == 0);
    for (i = 0; i < 8; i++) {
        CHECK(onflash(i, &im[i]));
        CHECK(proc[i].p_addr != 0);
        CHECK(proc[i].p_daddr != proc[i].p_addr);
    }
    CHECK(before - mapfree() == expect);
    CHECK((size_t)writes == expect);
    /* the pool is whole again: the same images fit a second time */
    for (i = 0; i < 8; i++)
        CHECK(admit(i, &im[i]));
    CHECK(swapram_images() == 8);
    CHECK(swapram_evacuate() == 0);
    CHECK(swapram_images() == 0);
    printf("roundtrip: 8 images twice, %zu blocks each pass\n", expect);
}

static void
shortage(void)
{
    struct image im[6];
    struct proc saved[NPROC];
    int i, k;
    size_t before;

    /* room for about two images: the third reservation fails */
    for (k = 1; k <= 5; k++) {
        reset_map(k * 6);
        for (i = 0; i < 6; i++) {
            mkimage(&im[i], 1500, 400, 200 + i);
            CHECK(admit(i, &im[i]));
        }
        memcpy(saved, proc, sizeof saved);
        before = mapfree();
        CHECK(swapram_evacuate() == -1);
        CHECK(mapfree() == before);
        CHECK(writes == 0);
        CHECK(memcmp(saved, proc, sizeof saved) == 0);
        CHECK(swapram_images() == 6);
        /* and the pool still yields the images intact: bring them back */
        for (i = 0; i < 6; i++) {
            unsigned char *d = malloc(1500), *s = malloc(400), *ub = malloc(USIZE);

            swapram_in(&proc[i], (caddr_t)d, (caddr_t)s, (caddr_t)ub);
            CHECK(memcmp(d, im[i].d, 1500) == 0);
            CHECK(memcmp(s, im[i].s, 400) == 0);
            CHECK(memcmp(ub, im[i].ub, USIZE) == 0);
            free(d); free(s); free(ub);
        }
        CHECK(swapram_images() == 0);
    }
    /* exactly enough: every image moves */
    reset_map(6 * (btod(1500) + btod(400) + btod(USIZE)));
    for (i = 0; i < 6; i++)
        CHECK(admit(i, &im[i]));
    CHECK(swapram_evacuate() == 0);
    CHECK(mapfree() == 0);
    for (i = 0; i < 6; i++)
        CHECK(onflash(i, &im[i]));
    printf("shortage: five short maps refused whole, the exact map taken whole\n");
}

static void
admission(void)
{
    struct image im;

    reset_map(NSWAP - 1);
    mkimage(&im, 1000, 200, 7);
    swapram_admit(0);
    CHECK(admit(0, &im) == 0);
    CHECK(swapram_images() == 0);
    swapram_admit(1);
    CHECK(admit(0, &im) == 1);
    CHECK(swapram_evacuate() == 0);
    CHECK(onflash(0, &im));
    /* empty pool: an evacuation is a no-op that touches nothing */
    CHECK(swapram_evacuate() == 0);
    CHECK(writes == btod(1000) + btod(200) + btod(USIZE));
    printf("admission: closed pool refuses, open pool takes, empty evacuation writes nothing\n");
}

static void
service(void)
{
    struct image im;

    reset_map(NSWAP - 1);
    mkimage(&im, 2000, 100, 9);
    CHECK(admit(1, &im));
    swapram_evac = SWAPRAM_EVAC_IDLE;
    swapram_service();
    CHECK(swapram_images() == 1);
    swapram_evac = SWAPRAM_EVAC_PENDING;
    swapram_service();
    CHECK(swapram_evac == SWAPRAM_EVAC_DONE);
    CHECK(swapram_images() == 0);
    CHECK(onflash(1, &im));
    reset_map(2);
    CHECK(admit(1, &im));
    swapram_evac = SWAPRAM_EVAC_PENDING;
    swapram_service();
    CHECK(swapram_evac == SWAPRAM_EVAC_NOFLASH);
    CHECK(swapram_images() == 1);
    CHECK(admit(2, &im));       /* admission reopened after the attempt */
    printf("service: idle ignored, pending evacuated, shortage reported and admission reopened\n");
}

static void
epoch(void)
{
    struct image im, im2;
    int i;

    /* start from a drained pool and unmarked processes */
    reset_map(NSWAP - 1);
    CHECK(swapram_evacuate() == 0);
    for (i = 0; i < NPROC; i++)
        proc[i].p_flag = 0;

    /* SMALL with images: entering LARGE evacuates, closes, marks, counts */
    mkimage(&im, 3000, 500, 21);
    mkimage(&im2, 1000, 100, 22);
    CHECK(admit(0, &im));
    CHECK(admit(1, &im2));
    CHECK(swapram_epoch == SWAPRAM_SMALL);
    CHECK(swapram_ceiling(&proc[5]) == MAXMEM);
    sleeps = 0;
    CHECK(swapram_enter_large(&proc[5]) == 0);
    CHECK(sleeps == 1);
    CHECK(swapram_epoch == SWAPRAM_LARGE);
    CHECK(proc[5].p_flag & P_LARGE);
    CHECK(swapram_ceiling(&proc[5]) == MAXMEM + SWAPRAM_BONUS);
    CHECK(swapram_images() == 0);
    CHECK(onflash(0, &im) && onflash(1, &im2));
    CHECK(admit(2, &im2) == 0);             /* pool closed under LARGE */
    /* a second process joins without another evacuation */
    CHECK(swapram_enter_large(&proc[6]) == 0);
    CHECK(sleeps == 1);
    /* fork inherits; leaving one of three keeps the epoch */
    swapram_inherit(&proc[7], &proc[6]);
    CHECK(proc[7].p_flag & P_LARGE);
    swapram_leave_large(&proc[5]);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_LARGE);
    CHECK(admit(2, &im2) == 0);
    /* the last large process out returns SMALL and reopens the pool */
    swapram_leave_large(&proc[6]);
    swapram_leave_large(&proc[7]);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_SMALL);
    CHECK(admit(2, &im2) == 1);
    CHECK(swapram_images() == 1);
    /* leaving twice is idle; ceiling back to the window */
    swapram_leave_large(&proc[7]);
    CHECK(swapram_ceiling(&proc[7]) == MAXMEM);

    /* SMALL with the map too short: the request is refused, SMALL stays */
    reset_map(2);
    CHECK(admit(3, &im));
    for (i = 0; i < NPROC; i++)
        proc[i].p_flag &= ~P_LARGE;
    CHECK(swapram_enter_large(&proc[8]) == ENOMEM);
    CHECK(swapram_epoch == SWAPRAM_SMALL);
    CHECK((proc[8].p_flag & P_LARGE) == 0);
    CHECK(swapram_images() == 2);           /* both images still in the pool */
    CHECK(admit(9, &im2) == 1);             /* and the pool reopened */

    /* the operator's requests through the sysctl path */
    reset_map(NSWAP - 1);
    swapram_set_epoch(SWAPRAM_LARGE);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_LARGE);
    CHECK(swapram_images() == 0);
    CHECK(admit(10, &im2) == 0);
    swapram_set_epoch(SWAPRAM_SMALL);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_SMALL);
    CHECK(admit(10, &im2) == 1);
    /* SMALL is refused while a large process lives */
    CHECK(swapram_enter_large(&proc[11]) == 0);
    swapram_set_epoch(SWAPRAM_SMALL);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_LARGE);
    swapram_leave_large(&proc[11]);
    swapram_service();
    CHECK(swapram_epoch == SWAPRAM_SMALL);
    printf("epoch: enter evacuates and closes, joins, inherits, last leaver reopens, refusal keeps SMALL, operator requests honored\n");
}

int
main(void)
{
    if (setjmp(onpanic)) {
        printf("FAIL: panic: %s\n", panicmsg);
        return 1;
    }
    roundtrip();
    shortage();
    admission();
    service();
    epoch();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("swapram evacuation: ok\n");
    return 0;
}
