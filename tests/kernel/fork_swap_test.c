/*
 * Ownership gate over exact production fork, image and SwapRAM definitions.
 * Compression and storage I/O use deterministic boundary models; the pool and
 * resource-map allocators, slot metadata and LARGE inheritance are production.
 */
#include "hostkern.h"
#include <sys/param.h>
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
size_t swapnext;
static struct mapent entries[32];
struct map swapmap[1] = { { entries, entries + 30, "swapmap" } };
static struct inode directory, text_inode;
static struct file descriptor;
static unsigned writes, publications, sleeps;
static int child_resume, copy_consumed_reservation, run_vfork;
static unsigned char media[384 * 1024];
static unsigned char parent_bytes[264 * 1024];
static uint32_t storage_counts[STORAGE_METRIC_COUNT];
static unsigned write_sizes[3], write_blocks[3], write_sources[3];

void
storage_note(enum storage_metric metric, uint32_t amount)
{
    storage_counts[metric] += amount;
}

void
swap_cursor_publish(size_t cursor)
{
    HK_CHECK(cursor == swapnext);
    publications++;
}

void
swap(size_t block, size_t address, int count, int flags)
{
    unsigned index;

    HK_CHECK((flags & B_SWAPIMAGE) != 0);
    HK_CHECK((flags & B_READ) == 0);
    HK_CHECK(block * 1024 + count <= sizeof(media));
    HK_CHECK(u.u_procp->p_stat == SIDL);
    if (writes < 3) {
        write_sizes[writes] = count;
        write_blocks[writes] = block;
        write_sources[writes] = address;
    }
    HK_CHECK(address >= 0x20000000);
    HK_CHECK(address - 0x20000000 + count <= sizeof(parent_bytes));
    for (index = 0; index < (unsigned)count; index++)
        media[block * 1024 + index] = parent_bytes[address - 0x20000000 + index];
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

void wakeup(caddr_t channel) { (void)channel; }
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
    swapnext = 4;
    swapmap->m_limit = entries + 30;
#ifdef FORK_SWAP_FULL_MAP
    swapmap->m_limit = entries + 4;
#endif
    writes = publications = sleeps = 0;
    child_resume = copy_consumed_reservation = 0;
    sr_nlarge = 1;
}

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

static unsigned int
free_total(void)
{
    struct mapent *entry;
    unsigned int blocks = 0;

    for (entry = entries; entry->m_size; entry++)
        blocks += entry->m_size;
    return blocks;
}

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
        HK_CHECK(write_sizes[0] == parent_before.p_dsize - text_size);
        HK_CHECK(write_sizes[1] == 2244);
        HK_CHECK(write_sizes[2] == USIZE);
        HK_CHECK(write_blocks[0] == 4 && write_blocks[1] == 4 + data_blocks &&
            write_blocks[2] == 7 + data_blocks);
        HK_CHECK(write_sources[0] == parent_before.p_daddr + text_size);
        for (index = 0; index < 3; index++)
            HK_CHECK(same_bytes(media + write_blocks[index] * 1024,
                parent_bytes + (write_sources[index] - 0x20000000),
                write_sizes[index]));
        HK_CHECK(write_sources[1] == parent_before.p_saddr);
        HK_CHECK(write_sources[2] == parent_before.p_addr);
        parent_before.p_prev = &proc[1].p_nxt;
        HK_CHECK(same_bytes(&proc[0], &parent_before, sizeof(parent_before)));
        HK_CHECK(u.u_procp == &proc[0]);
        HK_CHECK(u.u_ru.ru_nswap == 1);
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
    freeproc->p_stat = SIDL;
    swapout(freeproc, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);
    HK_CHECK(reservation.state == SWAP_IMAGE_COMMITTED);
    HK_CHECK(free_total() == 0);
    swap_image_release(freeproc);
    HK_CHECK(free_total() == 156);
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
    swap_image_release(&proc[1]);
    HK_CHECK(swapram_pool_avail(&sr_map) == sizeof(sr_pool));
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
    successful_forks();
    if (hk_failures)
        return hk_verdict("fork swap refusal and reservation");
    reservation_lifecycle();
    swapram_lifecycle();
    full_descriptor_reservation();
    process_limit_refusals();
    reset_fixture();
    HK_EXPECT_PANIC("cannot reserve init swap", boot_refusal_call);
    reset_fixture();
    HK_EXPECT_PANIC("out of swap space", unreserved_refusal_call);
    return hk_verdict("fork swap refusal and reservation");
}
