/*
 * Ownership gate over exact production fork, image and SwapRAM definitions.
 * Compression and storage I/O use deterministic boundary models; the pool and
 * resource-map allocators, slot metadata and LARGE inheritance are production.
 */
#include "hostkern.h"
#include <sys/param.h>
#ifdef FORK_SWAP_UNALIGNED
/*
 * The unaligned fixture compiles the vm_swap.c arms a port without
 * SWAP_IMAGE_ALIGN takes. The port header defines the macro, so the fixture
 * removes it before sys/systm.h chooses between the swapnext and swapstart
 * declarations and before the extracted definitions choose their arms.
 */
#undef SWAP_IMAGE_ALIGN
#endif
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/inode.h>
#include <sys/file.h>
#include <sys/vm.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/capacity.h>
#include <sys/storage_stats.h>
#include <machine/swapram.h>

struct user u;
struct proc proc[NPROC];
struct proc *freeproc, *allproc, *zombproc, *pidhash[PIDHSZ];
struct timeval time;
struct vmrate cnt;
struct forkstat forkstat;
char runout;
#ifdef SWAP_IMAGE_ALIGN
size_t swapnext;
#endif
struct user u0;
char __user_data_start[16], __user_data_end[16];
static struct mapent entries[32];
struct map swapmap[1] = { { entries, entries + 30, "swapmap" } };
static struct inode directory, text_inode;
static struct file descriptor;
static unsigned writes, publications, sleeps, io_waits, wakeups;
static int child_resume, copy_consumed_reservation;
#ifdef SWAP_IMAGE_ALIGN
static int run_vfork;
#endif
#ifdef SWAPRAM
static unsigned restores, restore_length, requeues, signals, expands;
static unsigned expand_lengths[3], expand_destinations[3];
static caddr_t restore_destination, requeued;
#endif
static unsigned char media[384 * 1024];
static unsigned char parent_bytes[264 * 1024];
static unsigned char u_image[USIZE];
static uint32_t storage_counts[STORAGE_METRIC_COUNT];
static unsigned write_sizes[3], write_blocks[3], write_sources[3];
static caddr_t wake_channels[8];
static unsigned wake_flash_free[8], wake_ram_free[8];

static unsigned int free_total(void);

void
storage_note(enum storage_metric metric, uint32_t amount)
{
    storage_counts[metric] += amount;
}

#ifdef SWAP_IMAGE_ALIGN
void
swap_cursor_publish(size_t cursor)
{
    HK_CHECK(cursor == swapnext);
    publications++;
}
#endif

void
swap(size_t block, size_t address, int count, int flags)
{
    const unsigned char *source;
    unsigned index;

#ifdef SWAP_IMAGE_ALIGN
    HK_CHECK((flags & B_SWAPIMAGE) != 0);
#else
    HK_CHECK((flags & B_SWAPIMAGE) == 0);
#endif
    HK_CHECK((flags & B_READ) == 0);
    HK_CHECK(block * 1024 + count <= sizeof(media));
    /* Production buffer and device waits enter tsleep with this contract. */
    HK_CHECK(u.u_procp->p_stat == SRUN);
    u.u_procp->p_stat = SSLEEP;
    u.u_procp->p_stat = SRUN;
    io_waits++;
    if (writes < 3) {
        write_sizes[writes] = count;
        write_blocks[writes] = block;
        write_sources[writes] = address;
    }
    if (address == u.u_procp->p_addr && count == USIZE) {
        bzero(u_image, sizeof(u_image));
        bcopy(&u, u_image,
            sizeof(u) < sizeof(u_image) ? sizeof(u) : sizeof(u_image));
        source = u_image;
    } else {
        HK_CHECK(address >= 0x20000000);
        HK_CHECK(address - 0x20000000 + count <= sizeof(parent_bytes));
        source = parent_bytes + address - 0x20000000;
    }
    for (index = 0; index < (unsigned)count; index++)
        media[block * 1024 + index] = source[index];
    writes++;
}

void
sleep(caddr_t channel, int priority)
{
    struct proc *child = (struct proc *)channel;

    HK_CHECK(priority == PSWP + 1);
    HK_CHECK(child == &proc[1]);
    child->p_flag &= ~SVFORK;
    child->p_flag |= SLOAD;
    child->p_daddr = proc[0].p_daddr;
    child->p_saddr = proc[0].p_saddr;
    sleeps++;
}

void sched(void) { panic("boot parent scheduled"); }
void fork_gate_log(int level, char *message, ...)
{
    (void)level; (void)message;
}
int fork_gate_setjmp(label_t *label) { (void)label; return child_resume; }
void ilock(struct inode *inode) { (void)inode; }
void irele(struct inode *inode) { inode->i_count--; }
void iput(struct inode *inode) { inode->i_count--; }
void exec_text_unhold(struct inode *inode) { (void)inode; }

/*
 * swapin boundary: the executable text restore, signal delivery and run-queue
 * insertion record their calls, and the SwapRAM decoder records the
 * destinations swapram_in computed without writing through them. The
 * destinations are logical process addresses truncated to the kernel's
 * 32-bit size_t, never host mappings.
 */
int
exec_text_restore(struct proc *p, caddr_t dst, size_t len)
{
    (void)p;
#ifdef SWAPRAM
    restores++;
    restore_destination = dst;
    restore_length = len;
#else
    (void)dst;
    (void)len;
#endif
    return 0;
}

void
psignal(struct proc *p, int signal)
{
    (void)p;
    (void)signal;
#ifdef SWAPRAM
    signals++;
#endif
}

void
setrq(struct proc *p)
{
#ifdef SWAPRAM
    requeues++;
    requeued = (caddr_t)p;
#else
    (void)p;
#endif
}

static int
sr_expand(unsigned int off, unsigned int clen, caddr_t dst, unsigned int rlen,
    size_t blk, struct buf *bp)
{
    (void)blk;
    HK_CHECK(bp == NULL);
    HK_CHECK(off + clen <= SWAPRAM_KB * 1024U);
#ifdef SWAPRAM
    if (expands < 3) {
        expand_lengths[expands] = rlen;
        expand_destinations[expands] = (unsigned int)(u_long)dst;
    }
    expands++;
#else
    (void)dst;
#endif
    return rlen;
}

/* Logical process addresses avoid host mappings of the RP2040 RAM window. */
static int
sr_encode(caddr_t source, size_t length, unsigned char *target,
    unsigned int capacity)
{
    unsigned int encoded = (length + 31) / 32, index;

    unsigned int offset = (unsigned int)(u_long)source - 0x20000000;

    if (target != NULL) {
        if (encoded > capacity)
            return -1;
        for (index = 0; index < encoded; index++)
            target[index] = parent_bytes[offset + index * 32];
    }
    return encoded;
}

#define log fork_gate_log
#define setjmp fork_gate_setjmp
#define fork kernel_fork
#define vfork kernel_vfork
#ifndef FORK_SWAP_FUNCTIONS
#define FORK_SWAP_FUNCTIONS "fork_swap_functions.c"
#endif
#include FORK_SWAP_FUNCTIONS
#undef fork
#undef vfork
#undef log
#undef setjmp

void
wakeup(caddr_t channel)
{
    if (wakeups < sizeof(wake_channels) / sizeof(wake_channels[0])) {
        wake_channels[wakeups] = channel;
        wake_flash_free[wakeups] = free_total();
        wake_ram_free[wakeups] = swapram_pool_avail(&sr_map);
    }
    wakeups++;
}

static int
same_bytes(const void *left, const void *right, unsigned int length)
{
    const unsigned char *first = left, *second = right;
    unsigned int index;

    for (index = 0; index < length; index++)
        if (first[index] != second[index])
            return 0;
    return 1;
}

static void
reset_fixture(void)
{
    bzero(&u, sizeof(u));
    bzero(proc, sizeof(proc));
    bzero(pidhash, sizeof(pidhash));
    bzero(entries, sizeof(entries));
    bzero(&directory, sizeof(directory));
    bzero(&text_inode, sizeof(text_inode));
    bzero(&descriptor, sizeof(descriptor));
    bzero(&cnt, sizeof(cnt));
    bzero(&forkstat, sizeof(forkstat));
    bzero(storage_counts, sizeof(storage_counts));
    bzero(sr_tab, sizeof(sr_tab));
    bzero(sr_pool, sizeof(sr_pool));
    bzero(sr_seg, sizeof(sr_seg));
    sr_ready = 0;
    sr_pool_init_once();
    sr_admit = sr_nlarge = sr_codec_owner = swapramdebug = 0;
    allproc = &proc[0];
    allproc->p_prev = &allproc;
    allproc->p_pid = 9;
    allproc->p_stat = SRUN;
    allproc->p_flag = SLOAD | P_LARGE;
    allproc->p_daddr = 0x20000000;
    allproc->p_saddr = 0x20027000;
    allproc->p_addr = 0x2003e800;
    allproc->p_dsize = 159744;
    allproc->p_tsize = 8040;
    allproc->p_ssize = 2244;
    allproc->p_tip = &text_inode;
    freeproc = &proc[1];
    freeproc->p_nxt = &proc[2];
    zombproc = NULL;
    pidhash[PIDHASH(9)] = allproc;
    u.u_procp = allproc;
    u.u_cdir = &directory;
    u.u_rdir = &directory;
    u.u_ofile[0] = &descriptor;
    u.u_lastfile = 0;
    directory.i_count = 2;
    text_inode.i_count = 1;
    descriptor.f_count = 1;
    mpid = 100;
#ifdef SWAP_IMAGE_ALIGN
    swapnext = 4;
#endif
    swapmap->m_limit = entries + 30;
#ifdef FORK_SWAP_FULL_MAP
    swapmap->m_limit = entries + 4;
#endif
    bzero(wake_channels, sizeof(wake_channels));
    bzero(wake_flash_free, sizeof(wake_flash_free));
    bzero(wake_ram_free, sizeof(wake_ram_free));
    writes = publications = sleeps = io_waits = wakeups = 0;
    runout = 0;
    child_resume = copy_consumed_reservation = 0;
    sr_nlarge = 1;
}

#ifdef SWAP_IMAGE_ALIGN
/* Original transcript and its two repeat layouts, as size/address pairs. */
static const unsigned recorded_maps[3][4][2] = {
    { {120, 4}, {4, 144}, {28, 324}, {12, 372} },
    { {60, 24}, {4, 104}, {100, 284}, {0, 0} },
    { {20, 4}, {60, 44}, {4, 124}, {80, 304} }
};

static void
seed_map(unsigned int map_index)
{
    unsigned int index;

    for (index = 0; index < 4; index++) {
        entries[index].m_size = recorded_maps[map_index][index][0];
        entries[index].m_addr = recorded_maps[map_index][index][1];
    }
}

static void
recorded_refusals(void)
{
    struct mapent saved_entries[32];
    struct proc saved_procs[NPROC];
    struct user saved_user;
    struct proc *saved_hash[PIDHSZ];
    struct inode saved_directory, saved_text;
    struct file saved_file;
    struct sr_ent saved_slots[NPROC];
    struct swapram_seg saved_segments[NPROC + 1];
    struct swapram_pool saved_pool;
    struct swap_image_reservation reservation;
    struct forkstat saved_forkstat;
    struct vmrate saved_count;
    uint32_t saved_storage[STORAGE_METRIC_COUNT];
    unsigned int map_index, blocks, cursor, use_vfork;

    for (map_index = 0; map_index < 3; map_index++)
    for (blocks = 156; blocks <= 164; blocks += 4)
    for (cursor = 4; cursor < 384; cursor += 4)
    for (use_vfork = 0; use_vfork < 2; use_vfork++) {
        reset_fixture();
        seed_map(map_index);
        proc[0].p_tsize = blocks == 156 ? 8040 : blocks == 160 ? 4096 : 0;
        swapnext = cursor;
        HK_CHECK(swap_image_span(&proc[0]) == blocks);
        bcopy(entries, saved_entries, sizeof(entries));
        bcopy(proc, saved_procs, sizeof(proc));
        bcopy(&u, &saved_user, sizeof(u));
        bcopy(pidhash, saved_hash, sizeof(pidhash));
        bcopy(&directory, &saved_directory, sizeof(directory));
        bcopy(&text_inode, &saved_text, sizeof(text_inode));
        bcopy(&descriptor, &saved_file, sizeof(descriptor));
        bcopy(sr_tab, saved_slots, sizeof(sr_tab));
        bcopy(sr_seg, saved_segments, sizeof(sr_seg));
        bcopy(&sr_map, &saved_pool, sizeof(sr_map));
        bcopy(&forkstat, &saved_forkstat, sizeof(forkstat));
        bcopy(&cnt, &saved_count, sizeof(cnt));
        bcopy(storage_counts, saved_storage, sizeof(storage_counts));
        HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
            X_OLDSIZE, X_OLDSIZE) == ENOMEM);
        HK_CHECK(reservation.state == SWAP_IMAGE_EMPTY);
        swap_image_cancel(&reservation);
        if (use_vfork)
            kernel_vfork();
        else
            kernel_fork();
        HK_CHECK(u.u_error == ENOMEM);
        saved_user.u_error = ENOMEM;
        HK_CHECK(same_bytes(&u, &saved_user, sizeof(u)));
        HK_CHECK(same_bytes(entries, saved_entries, sizeof(entries)));
        HK_CHECK(swapnext == cursor);
        HK_CHECK(publications == 0);
        HK_CHECK(same_bytes(proc, saved_procs, sizeof(proc)));
        HK_CHECK(same_bytes(pidhash, saved_hash, sizeof(pidhash)));
        HK_CHECK(allproc == &proc[0]);
        HK_CHECK(freeproc == &proc[1]);
        HK_CHECK(zombproc == NULL);
        HK_CHECK(same_bytes(&directory, &saved_directory, sizeof(directory)));
        HK_CHECK(same_bytes(&text_inode, &saved_text, sizeof(text_inode)));
        HK_CHECK(same_bytes(&descriptor, &saved_file, sizeof(descriptor)));
        HK_CHECK(same_bytes(sr_tab, saved_slots, sizeof(sr_tab)));
        HK_CHECK(same_bytes(sr_seg, saved_segments, sizeof(sr_seg)));
        HK_CHECK(same_bytes(&sr_map, &saved_pool, sizeof(sr_map)));
        HK_CHECK(same_bytes(&forkstat, &saved_forkstat, sizeof(forkstat)));
        HK_CHECK(same_bytes(&cnt, &saved_count, sizeof(cnt)));
        HK_CHECK(same_bytes(storage_counts, saved_storage, sizeof(storage_counts)));
        HK_CHECK(sr_nlarge == 1);
        HK_CHECK(writes == 0);
        HK_CHECK(mpid == 100);
    }
}
#endif

static unsigned int
free_total(void)
{
    struct mapent *entry;
    unsigned int blocks = 0;

    for (entry = entries; entry->m_size; entry++)
        blocks += entry->m_size;
    return blocks;
}

#ifdef SWAP_IMAGE_ALIGN
static void
successful_fork_call(void)
{
    if (run_vfork)
        kernel_vfork();
    else
        kernel_fork();
    copy_consumed_reservation = 1;
    panic("fork returned");
}

static void
successful_forks(void)
{
    struct proc parent_before;
    struct user copied_user;
    unsigned int use_vfork, blocks, data_blocks, text_size, index;

    for (blocks = 156; blocks <= 164; blocks += 8)
    for (use_vfork = 0; use_vfork < 2; use_vfork++) {
        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = blocks;
        text_size = blocks == 156 ? 8040 : 0;
        proc[0].p_tsize = text_size;
        data_blocks = btod(proc[0].p_dsize - text_size);
        bcopy(&proc[0], &parent_before, sizeof(parent_before));
        run_vfork = use_vfork;
        HK_EXPECT_PANIC("fork returned", successful_fork_call);
        HK_CHECK(copy_consumed_reservation);
        HK_CHECK(u.u_error == 0);
        HK_CHECK(u.u_rval == proc[1].p_pid);
        HK_CHECK(allproc == &proc[1]);
        HK_CHECK(freeproc == &proc[2]);
        HK_CHECK(pidhash[PIDHASH(proc[1].p_pid)] == &proc[1]);
        HK_CHECK(proc[1].p_stat == SRUN);
        HK_CHECK((proc[1].p_flag & SSWAP) != 0);
        HK_CHECK(proc[1].p_daddr == (use_vfork ? parent_before.p_daddr : 4));
        HK_CHECK(proc[1].p_saddr == (use_vfork ? parent_before.p_saddr : 4 + data_blocks));
        HK_CHECK(proc[1].p_addr == 7 + data_blocks);
        HK_CHECK(free_total() == 0);
        HK_CHECK(swapnext == 4 + blocks);
        HK_CHECK(publications == 1);
        HK_CHECK(writes == 3);
        HK_CHECK(io_waits == 3);
        HK_CHECK(write_sizes[0] == parent_before.p_dsize - text_size);
        HK_CHECK(write_sizes[1] == 2244);
        HK_CHECK(write_sizes[2] == USIZE);
        HK_CHECK(write_blocks[0] == 4 && write_blocks[1] == 4 + data_blocks &&
            write_blocks[2] == 7 + data_blocks);
        HK_CHECK(write_sources[0] == parent_before.p_daddr + text_size);
        for (index = 0; index < 2; index++)
            HK_CHECK(same_bytes(media + write_blocks[index] * 1024,
                parent_bytes + (write_sources[index] - 0x20000000),
                write_sizes[index]));
        HK_CHECK(write_sources[1] == parent_before.p_saddr);
        HK_CHECK(write_sources[2] == parent_before.p_addr);
        bzero(&copied_user, sizeof(copied_user));
        bcopy(media + write_blocks[2] * 1024, &copied_user,
            sizeof(copied_user) < USIZE ? sizeof(copied_user) : USIZE);
        HK_CHECK(copied_user.u_procp == &proc[1]);
        HK_CHECK(copied_user.u_ru.ru_nswap == 1);
        parent_before.p_prev = &proc[1].p_nxt;
        HK_CHECK(same_bytes(&proc[0], &parent_before, sizeof(parent_before)));
        HK_CHECK(u.u_procp == &proc[0]);
        HK_CHECK(u.u_ru.ru_nswap == 1);
        HK_CHECK(cnt.v_swpout == 1);
        HK_CHECK(storage_counts[STORAGE_SWAPRAM_ATTEMPTS] == 1);
        HK_CHECK(storage_counts[STORAGE_SWAPRAM_ADMISSIONS] == 0);
        HK_CHECK(forkstat.cntfork == (use_vfork ? 0U : 1U));
        HK_CHECK(forkstat.cntvfork == (use_vfork ? 1U : 0U));
        HK_CHECK(sr_nlarge == 2);
        HK_CHECK(descriptor.f_count == 2);
        HK_CHECK(directory.i_count == 4);
        HK_CHECK(text_inode.i_count == 2);
        exec_text_release(&proc[1], NULL);
        HK_CHECK(text_inode.i_count == 1);
        HK_CHECK(proc[1].p_tip == NULL);
        HK_CHECK(sleeps == use_vfork);
    }
}

static void
reservation_lifecycle(void)
{
    struct swap_image_reservation reservation;
    struct proc saved_owner;
    unsigned int cursor, blocks;

    for (blocks = 156; blocks <= 164; blocks += 4)
    for (cursor = 4; cursor < 384; cursor += 4) {
        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = 380;
        proc[0].p_tsize = blocks == 156 ? 8040 : blocks == 160 ? 4096 : 0;
        swapnext = cursor;
        HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
            X_OLDSIZE, X_OLDSIZE) == 0);
        HK_CHECK(reservation.state == SWAP_IMAGE_RESERVED);
        HK_CHECK(reservation.span == blocks);
        HK_CHECK(free_total() == 380 - blocks);
        HK_CHECK(writes == 0);
        swap_image_cancel(&reservation);
        swap_image_cancel(&reservation);
        HK_CHECK(free_total() == 380);
        HK_CHECK(entries[0].m_addr == 4 && entries[0].m_size == 380);
        HK_CHECK(reservation.state == SWAP_IMAGE_EMPTY);
        HK_CHECK(writes == 0);
    }
    reset_fixture();
    entries[0].m_addr = 4;
    entries[0].m_size = 156;
    saved_owner = *freeproc;
    HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
        X_OLDSIZE, X_OLDSIZE) == 0);
    HK_CHECK(same_bytes(freeproc, &saved_owner, sizeof(saved_owner)));
    *freeproc = proc[0];
    u.u_procp = freeproc;
    freeproc->p_stat = SRUN;
    runout = 1;
    swapout(freeproc, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);
    HK_CHECK(reservation.state == SWAP_IMAGE_COMMITTED);
    HK_CHECK(free_total() == 0);
    HK_CHECK(io_waits == 3);
    HK_CHECK(runout == 0);
    HK_CHECK(wakeups == 1 && wake_channels[0] == (caddr_t)&runout);
    swap_image_release(freeproc);
    HK_CHECK(free_total() == 156);
    HK_CHECK(wake_flash_free[1] == 156);
    HK_CHECK(wakeups == 2 && wake_channels[1] == (caddr_t)&runout);
}

static void
swapram_lifecycle(void)
{
    struct swap_image_reservation reservation;
    struct proc saved_owner;
    unsigned int initial_free;

    reset_fixture();
    sr_admit = 1;
    initial_free = swapram_pool_avail(&sr_map);
    saved_owner = *freeproc;
    HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
        X_OLDSIZE, X_OLDSIZE) == 0);
    HK_CHECK(reservation.tier == SWAP_IMAGE_RAM);
    HK_CHECK(same_bytes(freeproc, &saved_owner, sizeof(saved_owner)));
    HK_CHECK(swapram_pool_avail(&sr_map) < initial_free);
    HK_CHECK(swapram_present(freeproc) == 0);
    HK_CHECK(writes == 0 && publications == 0);
    swap_image_cancel(&reservation);
    swap_image_cancel(&reservation);
    HK_CHECK(swapram_pool_avail(&sr_map) == initial_free);
    HK_CHECK(sr_tab[1].e_res == 0);
    HK_CHECK(writes == 0);
    /* An exhausted pool refuses without taking a flash extent. */
    sr_map.p_used = 0;
    HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
        X_OLDSIZE, X_OLDSIZE) == ENOMEM);
    HK_CHECK(sr_tab[1].e_res == 0);
    HK_CHECK(writes == 0 && publications == 0);
    reset_fixture();
    sr_admit = 1;
    kernel_fork();
    HK_CHECK(u.u_error == 0);
    HK_CHECK(swapram_present(&proc[1]));
    HK_CHECK(writes == 0 && publications == 0);
    HK_CHECK(proc[1].p_addr == 0 && proc[1].p_daddr == 0);
    HK_CHECK(cnt.v_swpout == 1);
    HK_CHECK(storage_counts[STORAGE_SWAPRAM_ATTEMPTS] == 1);
    HK_CHECK(storage_counts[STORAGE_SWAPRAM_ADMISSIONS] == 1);
    swap_image_release(&proc[1]);
    HK_CHECK(swapram_pool_avail(&sr_map) == sizeof(sr_pool));
    HK_CHECK(wakeups == 1 && wake_channels[0] == (caddr_t)&runout);
    HK_CHECK(wake_ram_free[0] == sizeof(sr_pool));
}
#endif

static struct swap_image_reservation transition_reservation;
static struct proc *transition_process;

static void
prepare_transition(void)
{
    reset_fixture();
    entries[0].m_addr = 4;
    entries[0].m_size = 156;
    HK_CHECK(swap_image_reserve(&transition_reservation, freeproc, &proc[0],
        X_OLDSIZE, X_OLDSIZE) == 0);
    *freeproc = proc[0];
    u.u_procp = freeproc;
    freeproc->p_stat = SRUN;
    transition_process = freeproc;
}

static void
copy_transition_call(void)
{
    swap_image_copy_out(&transition_reservation, transition_process);
}

static void
commit_transition_call(void)
{
    swap_image_commit(&transition_reservation, transition_process);
}

static void
cancel_transition_call(void)
{
    swap_image_cancel(&transition_reservation);
}

static void
invalid_reservation_transitions(void)
{
    prepare_transition();
    transition_process = &proc[0];
    HK_EXPECT_PANIC("swap image: copy owner", copy_transition_call);
    HK_CHECK(transition_reservation.state == SWAP_IMAGE_RESERVED);
    HK_CHECK(writes == 0);
    swap_image_cancel(&transition_reservation);

    prepare_transition();
    copy_transition_call();
    HK_CHECK(transition_reservation.state == SWAP_IMAGE_COPIED);
    HK_EXPECT_PANIC("swap image: copy owner", copy_transition_call);

    prepare_transition();
    HK_EXPECT_PANIC("swap image: commit owner", commit_transition_call);
    swap_image_cancel(&transition_reservation);

    prepare_transition();
    copy_transition_call();
    transition_process = &proc[0];
    HK_EXPECT_PANIC("swap image: commit owner", commit_transition_call);

    prepare_transition();
    copy_transition_call();
    commit_transition_call();
    HK_CHECK(transition_reservation.state == SWAP_IMAGE_COMMITTED);
    HK_EXPECT_PANIC("swap image: commit owner", commit_transition_call);
    HK_EXPECT_PANIC("swap image: cancel state", cancel_transition_call);

    prepare_transition();
    copy_transition_call();
    HK_EXPECT_PANIC("swap image: cancel state", cancel_transition_call);
}

static void
full_descriptor_reservation(void)
{
#ifdef FORK_SWAP_FULL_MAP
    struct swap_image_reservation reservation;
    unsigned int index;

    reset_fixture();
    entries[0].m_addr = 4;
    entries[0].m_size = 200;
    for (index = 1; index < 4; index++) {
        entries[index].m_addr = 200 + index * 20;
        entries[index].m_size = 4;
    }
    swapnext = 8;
    HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
        X_OLDSIZE, X_OLDSIZE) == 0);
    HK_CHECK(reservation.addr[0] == 48);
    HK_CHECK(free_total() == 56);
    swap_image_cancel(&reservation);
    HK_CHECK(free_total() == 212);
    HK_CHECK(entries[0].m_size == 200);
#endif
}

static void
process_limit_refusals(void)
{
    reset_fixture();
    freeproc = NULL;
    kernel_fork();
    HK_CHECK(u.u_error == EAGAIN);
    HK_CHECK(allproc == &proc[0]);
    HK_CHECK(writes == 0);
    reset_fixture();
    freeproc = NULL;
    kernel_vfork();
    HK_CHECK(u.u_error == EAGAIN);
    HK_CHECK(allproc == &proc[0]);
    HK_CHECK(writes == 0);
}

/*
 * Restoring a RAM-tier image through the production swapin() frees pool
 * capacity, so swapin() wakes the swapper on runout once the pool holds the
 * space. The flash route wakes through swap_image_release(); this control
 * keeps the RAM route from diverging.
 */
#ifdef SWAPRAM
static void
swapin_ram_release(void)
{
    size_t data_dest, stack_dest, tsize, ssize, dsize;
    unsigned int held;

    reset_fixture();
    sr_admit = 1;
    kernel_fork();
    HK_CHECK(u.u_error == 0);
    HK_CHECK(swapram_present(&proc[1]));
    HK_CHECK((proc[1].p_flag & (SLOAD | P_LARGE)) == P_LARGE);
    HK_CHECK(proc[1].p_stat == SRUN);
    held = swapram_pool_avail(&sr_map);
    HK_CHECK(held < sizeof(sr_pool));
    HK_CHECK(wakeups == 0);
    tsize = proc[1].p_tsize;
    dsize = proc[1].p_dsize;
    ssize = proc[1].p_ssize;
    stack_dest = user_top(&proc[1]) - ssize;
    data_dest = (size_t)__user_data_start + tsize;
    u.u_procp = &proc[0];
    swapin(&proc[1]);
    {
        int swapin_woke_swapper = wakeups == 1 &&
            wake_channels[0] == (caddr_t)&runout;
        int swapin_pool_free_at_wake = wake_ram_free[0] == sizeof(sr_pool);

        HK_CHECK(swapin_woke_swapper);
        HK_CHECK(swapin_pool_free_at_wake);
    }
    HK_CHECK(swapram_pool_avail(&sr_map) == sizeof(sr_pool));
    HK_CHECK(swapram_present(&proc[1]) == 0);
    HK_CHECK(sr_tab[1].e_res == 0);
    HK_CHECK(free_total() == 0 && entries[0].m_size == 0);
    HK_CHECK(writes == 0 && io_waits == 0);
    HK_CHECK(restores == 1 && restore_length == tsize);
    HK_CHECK((unsigned int)(u_long)restore_destination ==
        (unsigned int)(size_t)__user_data_start);
    HK_CHECK(expands == 3);
    HK_CHECK(expand_lengths[0] == dsize - tsize);
    HK_CHECK(expand_lengths[1] == ssize);
    HK_CHECK(expand_lengths[2] == USIZE);
    HK_CHECK(expand_destinations[0] == (unsigned int)data_dest);
    HK_CHECK(expand_destinations[1] == (unsigned int)stack_dest);
    HK_CHECK(expand_destinations[2] == (unsigned int)(size_t)&u0);
    HK_CHECK(proc[1].p_daddr == (size_t)__user_data_start);
    HK_CHECK(proc[1].p_saddr == stack_dest);
    HK_CHECK(proc[1].p_addr == (size_t)&u0);
    HK_CHECK((proc[1].p_flag & SLOAD) != 0 && proc[1].p_time == 0);
    HK_CHECK(requeues == 1 && requeued == (caddr_t)&proc[1]);
    HK_CHECK(signals == 0);
    HK_CHECK(cnt.v_swpin == 1);
}
#endif

/* Refusal returns -1 from newproc() and writes no image byte. */
static void
newproc_refusal_result(void)
{
    int refusal_result;

    reset_fixture();
    refusal_result = newproc(0);
    HK_CHECK(refusal_result == -1);
    HK_CHECK(allproc == &proc[0] && freeproc == &proc[1]);
    HK_CHECK(mpid == 100 && writes == 0 && sleeps == 0);
}

/* Result of newproc() in the parent and resumed-child contexts. */
static void
newproc_results(void)
{
    unsigned int use_vfork;
    int parent_result, child_result;

    for (use_vfork = 0; use_vfork < 2; use_vfork++) {
        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = 380;
        parent_result = newproc(use_vfork);
        HK_CHECK(parent_result == 0);
        HK_CHECK(u.u_procp == &proc[0]);
        HK_CHECK(allproc == &proc[1] && proc[1].p_pid == 101);
        HK_CHECK(proc[0].p_stat == SRUN && proc[1].p_stat == SRUN);
        HK_CHECK((proc[1].p_flag & SSWAP) != 0 && writes == 3);

        /*
         * The saved context makes setjmp() return nonzero in the child before
         * any image byte moves. The resume has no swapout on its path.
         */
        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = 380;
        child_resume = 1;
        child_result = newproc(use_vfork);
        HK_CHECK(child_result == 1);
        HK_CHECK(allproc == &proc[1] && proc[1].p_pid == 101);
        HK_CHECK(proc[1].p_stat == SIDL && (proc[1].p_flag & SSWAP) == 0);
        HK_CHECK(writes == 0 && sleeps == 0);
    }

    /* fork1() translates the three results into the user-visible ones. */
    for (use_vfork = 0; use_vfork < 2; use_vfork++) {
        reset_fixture();
        u.u_rval = -7;
        if (use_vfork)
            kernel_vfork();
        else
            kernel_fork();
        HK_CHECK(u.u_error == ENOMEM && u.u_rval == -7);

        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = 380;
        u.u_rval = -7;
        if (use_vfork)
            kernel_vfork();
        else
            kernel_fork();
        HK_CHECK(u.u_error == 0);
        HK_CHECK(u.u_rval == 101);

        reset_fixture();
        entries[0].m_addr = 4;
        entries[0].m_size = 380;
        child_resume = 1;
        time.tv_sec = 1234;
        u.u_rval = -7;
        u.u_ru.ru_nswap = 5;
        u.u_cru.ru_nswap = 6;
        if (use_vfork)
            kernel_vfork();
        else
            kernel_fork();
        HK_CHECK(u.u_error == 0);
        HK_CHECK(u.u_rval == 0);
        HK_CHECK(u.u_start == 1234);
        HK_CHECK(u.u_ru.ru_nswap == 0 && u.u_cru.ru_nswap == 0);
    }
}

/*
 * Process 0 at boot owns no data or stack, holds no executable text and
 * runs from the kernel u area, and init_main.c seeds the swap map with every
 * block past the reserved head before it forks process 1.
 */
static void
boot_fixture(void)
{
    unsigned int blocks = sizeof(media) / 1024;

    reset_fixture();
    bzero(entries, sizeof(entries));
    pidhash[PIDHASH(9)] = NULL;
    pidhash[PIDHASH(0)] = allproc;
    allproc->p_pid = 0;
    allproc->p_flag = SLOAD | SSYS;
    allproc->p_daddr = allproc->p_saddr = 0;
    allproc->p_dsize = allproc->p_ssize = allproc->p_tsize = 0;
    allproc->p_tip = NULL;
    u.u_lastfile = -1;
    u.u_ofile[0] = NULL;
    u.u_rdir = NULL;
    mpid = 0;
    sr_nlarge = 0;
#ifdef SWAP_IMAGE_ALIGN
    mfree(swapmap, blocks - SWAP_IMAGE_ALIGN, SWAP_IMAGE_ALIGN);
#else
    mfree(swapmap, blocks, 1);
#endif
}

static void boot_parent_call(void) { fork_swap_boot(); panic("boot returned"); }

static void
boot_process_one(void)
{
    struct user copied_user;
    unsigned int blocks = sizeof(media) / 1024, span;

    boot_fixture();
    span = swap_image_span(&proc[0]);
    HK_CHECK(span >= btod(USIZE));
#ifdef SWAP_IMAGE_ALIGN
    HK_CHECK(span % SWAP_IMAGE_ALIGN == 0);
    HK_CHECK(free_total() == blocks - SWAP_IMAGE_ALIGN);
#else
    HK_CHECK(span == btod(USIZE));
    HK_CHECK(free_total() == blocks);
#endif
    HK_EXPECT_PANIC("boot parent scheduled", boot_parent_call);
    HK_CHECK(allproc == &proc[1] && proc[1].p_pid == 1 &&
        proc[1].p_ppid == 0);
    HK_CHECK(proc[1].p_stat == SRUN && (proc[1].p_flag & SSWAP) != 0);
    HK_CHECK(proc[1].p_dsize == 0 && proc[1].p_ssize == 0);
    HK_CHECK(proc[1].p_daddr == proc[1].p_addr);
    HK_CHECK(proc[1].p_saddr == proc[1].p_addr);
    HK_CHECK(writes == 1 && io_waits == 1);
    HK_CHECK(write_sizes[0] == USIZE);
    HK_CHECK(write_blocks[0] == proc[1].p_addr);
    HK_CHECK(free_total() == blocks -
#ifdef SWAP_IMAGE_ALIGN
        SWAP_IMAGE_ALIGN -
#endif
        span);
#ifdef SWAP_IMAGE_ALIGN
    HK_CHECK(proc[1].p_addr == SWAP_IMAGE_ALIGN);
    HK_CHECK(swapnext == SWAP_IMAGE_ALIGN + span && publications == 1);
#endif
    bzero(&copied_user, sizeof(copied_user));
    bcopy(media + write_blocks[0] * 1024, &copied_user,
        sizeof(copied_user) < USIZE ? sizeof(copied_user) : USIZE);
    HK_CHECK(copied_user.u_procp == &proc[1]);
    HK_CHECK(u.u_procp == &proc[0] && proc[0].p_stat == SRUN);

    /* The resumed child of the boot fork returns without scheduling. */
    boot_fixture();
    child_resume = 1;
    fork_swap_boot();
    HK_CHECK(allproc == &proc[1] && proc[1].p_pid == 1);
    HK_CHECK(proc[1].p_stat == SIDL && writes == 0);

#ifdef SWAPRAM
    boot_fixture();
    sr_admit = 1;
    HK_EXPECT_PANIC("boot parent scheduled", boot_parent_call);
    HK_CHECK(swapram_present(&proc[1]));
    HK_CHECK(proc[1].p_addr == 0 && proc[1].p_daddr == 0);
    HK_CHECK(writes == 0 && publications == 0);
    HK_CHECK(free_total() == blocks - SWAP_IMAGE_ALIGN);
    HK_CHECK(storage_counts[STORAGE_SWAPRAM_ADMISSIONS] == 1);
#endif
}

#ifndef SWAP_IMAGE_ALIGN
/*
 * Seed three free runs the allocator cannot merge: ascending addresses with
 * gaps. A run holds a segment plus a remainder smaller than the next
 * segment, so first-fit takes each segment from its own run.
 */
static unsigned int geometry_data, geometry_stack, geometry_uarea;

static void
seed_runs(unsigned int data, unsigned int stack, unsigned int uarea)
{
    bzero(entries, sizeof(entries));
    entries[0].m_size = data;
    entries[0].m_addr = 10;
    entries[1].m_size = stack;
    entries[1].m_addr = 200;
    entries[2].m_size = uarea;
    entries[2].m_addr = 300;
}

static void
nonaligned_fixture(void)
{
    reset_fixture();
    geometry_data = btod(proc[0].p_dsize - swaptext(&proc[0]));
    geometry_stack = btod(proc[0].p_ssize);
    geometry_uarea = btod(USIZE);
    HK_CHECK(swap_image_span(&proc[0]) ==
        geometry_data + geometry_stack + geometry_uarea);
    HK_CHECK(10 + geometry_data + 1 < 200 && 200 + geometry_stack + 1 < 300);
    HK_CHECK(geometry_stack > 1 && geometry_uarea > 1);
}

static void
nonaligned_success_and_cancel(void)
{
    struct swap_image_reservation reservation;
    struct mapent seeded[32];
    unsigned int pass, runs;

    for (pass = 0; pass < 2; pass++) {
        nonaligned_fixture();
        /* Pass 0 leaves remainders; pass 1 consumes every run exactly. */
        runs = pass ? 0 : 1;
        seed_runs(geometry_data + runs, geometry_stack + runs,
            geometry_uarea + 2 * runs);
        bcopy(entries, seeded, sizeof(entries));
        HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
            X_OLDSIZE, X_OLDSIZE) == 0);
        HK_CHECK(reservation.state == SWAP_IMAGE_RESERVED);
        HK_CHECK(reservation.tier != SWAP_IMAGE_RAM);
        HK_CHECK(reservation.addr[0] == 10 && reservation.addr[1] == 200 &&
            reservation.addr[2] == 300);
        HK_CHECK(reservation.span ==
            geometry_data + geometry_stack + geometry_uarea);
        HK_CHECK(free_total() == 4 * runs);
        if (runs) {
            HK_CHECK(entries[0].m_size == 1 &&
                entries[0].m_addr == 10 + geometry_data);
            HK_CHECK(entries[1].m_size == 1 &&
                entries[1].m_addr == 200 + geometry_stack);
            HK_CHECK(entries[2].m_size == 2 &&
                entries[2].m_addr == 300 + geometry_uarea);
            HK_CHECK(entries[3].m_size == 0);
        } else {
            HK_CHECK(entries[0].m_size == 0);
        }
        HK_CHECK(writes == 0);
        swap_image_cancel(&reservation);
        swap_image_cancel(&reservation);
        {
            int cancel_restored = same_bytes(entries, seeded, sizeof(entries));

            HK_CHECK(cancel_restored);
        }
        HK_CHECK(reservation.state == SWAP_IMAGE_EMPTY);
        HK_CHECK(writes == 0);
    }
}

static void
nonaligned_partial_refusal(void)
{
    struct swap_image_reservation reservation;
    struct mapent seeded[32];
    struct proc saved_procs[NPROC];
    unsigned int missing, use_vfork;

    /*
     * Each layout lets the allocator take two segments before the third
     * misses, so the refusal has to hand the partial takes back.
     */
    for (missing = 0; missing < 3; missing++)
    for (use_vfork = 0; use_vfork < 2; use_vfork++) {
        nonaligned_fixture();
        seed_runs(geometry_data + 1, geometry_stack + 1, geometry_uarea + 2);
        if (missing == 0)
            entries[0].m_size = geometry_data - 1;
        else if (missing == 1)
            entries[1].m_size = geometry_stack - 1;
        else
            entries[2].m_size = geometry_uarea - 1;
        bcopy(entries, seeded, sizeof(entries));
        bcopy(proc, saved_procs, sizeof(proc));
        HK_CHECK(swap_image_reserve(&reservation, freeproc, &proc[0],
            X_OLDSIZE, X_OLDSIZE) == ENOMEM);
        HK_CHECK(reservation.state == SWAP_IMAGE_EMPTY);
        {
            int refusal_restored = same_bytes(entries, seeded,
                sizeof(entries));

            HK_CHECK(refusal_restored);
        }
        swap_image_cancel(&reservation);
        if (use_vfork)
            kernel_vfork();
        else
            kernel_fork();
        HK_CHECK(u.u_error == ENOMEM);
        HK_CHECK(same_bytes(entries, seeded, sizeof(entries)));
        HK_CHECK(same_bytes(proc, saved_procs, sizeof(proc)));
        HK_CHECK(allproc == &proc[0] && freeproc == &proc[1]);
        HK_CHECK(mpid == 100 && writes == 0);
    }
}

static void
nonaligned_release(void)
{
    struct mapent seeded[32];
    unsigned int pass, runs, total;

    for (pass = 0; pass < 2; pass++) {
        nonaligned_fixture();
        runs = pass ? 0 : 1;
        seed_runs(geometry_data + runs, geometry_stack + runs,
            geometry_uarea + 2 * runs);
        bcopy(entries, seeded, sizeof(entries));
        total = free_total();
        kernel_fork();
        HK_CHECK(u.u_error == 0);
        HK_CHECK(proc[1].p_daddr == 10 && proc[1].p_saddr == 200 &&
            proc[1].p_addr == 300);
        HK_CHECK(writes == 3 && io_waits == 3);
        HK_CHECK(write_sizes[0] == proc[0].p_dsize - proc[0].p_tsize);
        HK_CHECK(write_sizes[1] == proc[0].p_ssize);
        HK_CHECK(write_sizes[2] == USIZE);
        HK_CHECK(write_blocks[0] == 10 && write_blocks[1] == 200 &&
            write_blocks[2] == 300);
        HK_CHECK(free_total() == 4 * runs);
        HK_CHECK(wakeups == 0);
        swap_image_release(&proc[1]);
        {
            int release_restored = same_bytes(entries, seeded,
                sizeof(entries));

            HK_CHECK(release_restored);
        }
        HK_CHECK(free_total() == total);
        HK_CHECK(wakeups == 1 && wake_channels[0] == (caddr_t)&runout);
        HK_CHECK(wake_flash_free[0] == total);
    }
}
#endif

static void boot_refusal_call(void) { fork_swap_boot(); panic("boot child"); }
static void unreserved_refusal_call(void)
{
    swapout(&proc[0], X_FREECORE, X_OLDSIZE, X_OLDSIZE, NULL);
}

int
main(void)
{
    unsigned int index;

    for (index = 0; index < sizeof(media); index++)
        media[index] = (index * 17 + 3) & 255;
    for (index = 0; index < sizeof(parent_bytes); index++)
        parent_bytes[index] = (index * 11 + 9) & 255;
    newproc_refusal_result();
#ifdef SWAP_IMAGE_ALIGN
    recorded_refusals();
    for (index = 0; index < sizeof(media); index++)
        if (media[index] != ((index * 17 + 3) & 255))
            break;
    HK_CHECK(index == sizeof(media));
    for (index = 0; index < sizeof(parent_bytes); index++)
        if (parent_bytes[index] != ((index * 11 + 9) & 255))
            break;
    HK_CHECK(index == sizeof(parent_bytes));
    if (hk_failures)
        return hk_verdict("fork swap refusal and reservation");
    newproc_results();
    boot_process_one();
    successful_forks();
    if (hk_failures)
        return hk_verdict("fork swap refusal and reservation");
    reservation_lifecycle();
#ifdef SWAPRAM
    swapram_lifecycle();
    swapin_ram_release();
#endif
#else
    nonaligned_success_and_cancel();
    nonaligned_partial_refusal();
    nonaligned_release();
    newproc_results();
    boot_process_one();
#endif
    invalid_reservation_transitions();
    full_descriptor_reservation();
    process_limit_refusals();
    reset_fixture();
    HK_EXPECT_PANIC("cannot reserve init swap", boot_refusal_call);
    reset_fixture();
    HK_EXPECT_PANIC("out of swap space", unreserved_refusal_call);
    return hk_verdict("fork swap refusal and reservation");
}
