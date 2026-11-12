/* Direct in-place exchange gate over the production vm_swap.c object. */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/vm.h>
#include <sys/storage_stats.h>
#include <machine/swapram.h>

#define WINDOW_BYTES MAXMEM
#define MEDIA_BYTES (384 * 1024)
#define SECTOR_BYTES (SWAP_IMAGE_ALIGN * DEV_BSIZE)
#define MAX_CASE_BYTES (72 * 1024)
/* Distance from __user_data_start to the stack top the fixture defines. */
#define USER_TOP_BYTES 98304

struct user u;
struct vmrate cnt;
char runin, runout;
size_t swapnext;
unsigned char exchange_window[WINDOW_BYTES];
unsigned char exchange_u0[USIZE];
__asm__(".globl __user_data_start\n"
    ".set __user_data_start, exchange_window\n"
    ".globl __user_data_end\n"
    ".set __user_data_end, exchange_window + 98304\n"
    ".globl u0\n"
    ".set u0, exchange_u0\n");

static struct mapent map_entries[16];
struct map swapmap[1] = { { map_entries, map_entries + 14, "swapmap", 0, 0, 0 } };
static struct buf exchange_buffer;
static unsigned char media[MEDIA_BYTES];
static unsigned char expected_out_data[MAX_CASE_BYTES];
static unsigned char expected_in_data[MAX_CASE_BYTES];
static unsigned char expected_out_stack[MAX_CASE_BYTES];
static unsigned char expected_in_stack[MAX_CASE_BYTES];
static unsigned char expected_out_u[USIZE];
static unsigned char expected_in_u[USIZE];
static unsigned erase_counts[MEDIA_BYTES / SECTOR_BYTES];
static unsigned program_counts[MEDIA_BYTES / SECTOR_BYTES];
static unsigned erase_total, program_total, media_writes;
static int fail_erase_sector, fail_program_sector;
static size_t image_base;
/*
 * In ring mode the swap map is a ring over blocks RING_FIRST to RING_END - 1
 * and the incoming image starts RING_TAIL blocks below the end, so every
 * image longer than that passes the end and continues at RING_FIRST. The
 * kernel folds such a block in swap_with_buf_result(), which this fixture
 * replaces with its media model; media_block() is that fold here, and the
 * model checks that no transfer of the exchange crosses the end.
 */
#define RING_FIRST SWAP_IMAGE_ALIGN
#define RING_END 380
#define RING_TAIL 8
static int ring_mode;

static size_t
media_block(size_t block)
{
    if (ring_mode && block >= RING_END)
        block -= RING_END - RING_FIRST;
    return block;
}

static unsigned char *
media_byte(size_t base, size_t offset)
{
    return media + media_block(base + offset / DEV_BSIZE) * DEV_BSIZE +
        offset % DEV_BSIZE;
}
static struct swap_image_reservation panic_reservation;
static struct proc panic_outgoing, panic_incoming;
/* Executable-backed clean text at the front of each data segment. */
static size_t outgoing_text, incoming_text;
static unsigned text_restores;
/* swaptext() tests the pointer and never follows it. */
static char text_inode;

static void
fill_bytes(unsigned char *bytes, size_t length, unsigned seed)
{
    size_t index;

    for (index = 0; index < length; index++)
        bytes[index] = (unsigned char)((index * 37 + seed) % 251);
}

static int
same_bytes(const unsigned char *first, const unsigned char *second,
    size_t length)
{
    size_t index;

    for (index = 0; index < length; index++)
        if (first[index] != second[index])
            return 0;
    return 1;
}

static size_t
image_span(size_t data_length, size_t stack_length)
{
    size_t blocks = btod(data_length) + btod(stack_length) + btod(USIZE);

    return (blocks + SWAP_IMAGE_ALIGN - 1) & ~(SWAP_IMAGE_ALIGN - 1);
}

static void
put_image(size_t base, const unsigned char *data, size_t data_length,
    const unsigned char *stack, size_t stack_length,
    const unsigned char *uarea)
{
    size_t data_blocks = btod(data_length);
    size_t stack_blocks = btod(stack_length);
    size_t span = image_span(data_length, stack_length);

    size_t index;

    for (index = 0; index < span * DEV_BSIZE; index++)
        *media_byte(base, index) = 0xff;
    for (index = 0; index < data_length; index++)
        *media_byte(base, index) = data[index];
    for (index = 0; index < stack_length; index++)
        *media_byte(base + data_blocks, index) = stack[index];
    for (index = 0; index < USIZE; index++)
        *media_byte(base + data_blocks + stack_blocks, index) = uarea[index];
}

static int
same_media(size_t base, const unsigned char *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; index++)
        if (*media_byte(base, index) != bytes[index])
            return 0;
    return 1;
}

static int
image_matches(size_t base, const unsigned char *data, size_t data_length,
    const unsigned char *stack, size_t stack_length,
    const unsigned char *uarea)
{
    size_t data_blocks = btod(data_length);
    size_t stack_blocks = btod(stack_length);

    return same_media(base, data, data_length) &&
        same_media(base + data_blocks, stack, stack_length) &&
        same_media(base + data_blocks + stack_blocks, uarea, USIZE);
}

static void
install_resident(struct proc *process, const unsigned char *data,
    size_t data_length, const unsigned char *stack, size_t stack_length,
    const unsigned char *uarea, size_t text_length)
{
    process->p_dsize = data_length;
    process->p_ssize = stack_length;
    process->p_tsize = text_length;
    process->p_tip = text_length ? (struct inode *)&text_inode : NULL;
    process->p_daddr = (size_t)__user_data_start;
    process->p_saddr = (size_t)__user_data_end - stack_length;
    process->p_addr = (size_t)&u0;
    process->p_stat = SRUN;
    process->p_flag = SLOAD;
    bcopy(data, __user_data_start, data_length);
    bcopy(stack, __user_data_end - stack_length, stack_length);
    bcopy(uarea, &u0, USIZE);
}

static void
install_swapped(struct proc *process, size_t base, size_t data_length,
    size_t stack_length, size_t text_length)
{
    process->p_dsize = data_length;
    process->p_ssize = stack_length;
    process->p_tsize = text_length;
    process->p_tip = text_length ? (struct inode *)&text_inode : NULL;
    process->p_daddr = base;
    process->p_saddr = base + btod(data_length - text_length);
    process->p_addr = process->p_saddr + btod(stack_length);
    process->p_stat = SRUN;
    process->p_flag = 0;
}

static void
reset_case(size_t outgoing_data, size_t outgoing_stack,
    size_t incoming_data, size_t incoming_stack, int tail_available)
{
    size_t incoming_span = image_span(incoming_data - incoming_text,
        incoming_stack);

    bzero(&u, sizeof(u));
    bzero(&u0, USIZE);
    bzero(exchange_window, sizeof(exchange_window));
    bzero(media, sizeof(media));
    bzero(map_entries, sizeof(map_entries));
    bzero(erase_counts, sizeof(erase_counts));
    bzero(program_counts, sizeof(program_counts));
    bzero(&cnt, sizeof(cnt));
    erase_total = program_total = media_writes = 0;
    text_restores = 0;
    fail_erase_sector = fail_program_sector = -1;
    image_base = 40;
    swapnext = image_base + incoming_span;
    swapmap->m_ring_first = swapmap->m_ring_end = swapmap->m_guard = 0;
    if (ring_mode) {
        /* Everything outside the incoming image is one free run. */
        image_base = RING_END - RING_TAIL;
        swapmap->m_ring_first = RING_FIRST;
        swapmap->m_ring_end = RING_END;
        swapnext = media_block(image_base + incoming_span);
        if (incoming_span >= RING_TAIL) {
            map_entries[0].m_addr = swapnext;
            map_entries[0].m_size = image_base - swapnext;
        } else {
            /* A short image leaves a free run on each side of the end. */
            map_entries[0].m_addr = RING_FIRST;
            map_entries[0].m_size = image_base - RING_FIRST;
            map_entries[1].m_addr = image_base + incoming_span;
            map_entries[1].m_size = RING_END - map_entries[1].m_addr;
        }
    } else if (tail_available == 1) {
        map_entries[0].m_addr = image_base + incoming_span;
        map_entries[0].m_size = 380 - map_entries[0].m_addr;
    } else {
        /* Blocks after the extent are owned; 2 leaves a whole run beyond. */
        map_entries[0].m_addr = image_base + incoming_span + 8;
        map_entries[0].m_size = tail_available == 2 ?
            380 - map_entries[0].m_addr : 8;
    }
    swapmap->m_limit = map_entries + 14;
    fill_bytes(expected_out_data, outgoing_data, 11);
    fill_bytes(expected_out_stack, outgoing_stack, 29);
    fill_bytes(expected_out_u, USIZE, 47);
    fill_bytes(expected_in_data, incoming_data, 73);
    fill_bytes(expected_in_stack, incoming_stack, 101);
    fill_bytes(expected_in_u, USIZE, 131);
    bzero(&panic_outgoing, sizeof(panic_outgoing));
    bzero(&panic_incoming, sizeof(panic_incoming));
    install_resident(&panic_outgoing, expected_out_data, outgoing_data,
        expected_out_stack, outgoing_stack, expected_out_u, outgoing_text);
    install_swapped(&panic_incoming, image_base, incoming_data,
        incoming_stack, incoming_text);
    put_image(image_base, expected_in_data + incoming_text,
        incoming_data - incoming_text, expected_in_stack, incoming_stack,
        expected_in_u);
    u.u_procp = &panic_outgoing;
}

struct buf *geteblk(void) { return &exchange_buffer; }
void brelse(struct buf *buffer) { HK_CHECK(buffer == &exchange_buffer); }
void setrq(struct proc *process) { (void)process; }
void psignal(struct proc *process, int signal) { (void)process; (void)signal; }
void wakeup(caddr_t channel) { (void)channel; }
void swap_cursor_publish(size_t cursor) { (void)cursor; }
void storage_note(enum storage_metric metric, uint32_t amount)
{ (void)metric; (void)amount; }
/* The executable yields the clean text the swap image leaves out. */
int
exec_text_restore(struct proc *process, caddr_t target, size_t length)
{
    HK_CHECK(target == (caddr_t)__user_data_start);
    HK_CHECK(length == (process == &panic_incoming ? incoming_text :
        outgoing_text));
    bcopy(process == &panic_incoming ? expected_in_data : expected_out_data,
        target, length);
    text_restores++;
    return 0;
}
void swap(size_t block, size_t address, int count, int flags)
{ (void)block; (void)address; (void)count; (void)flags; panic("unexpected swap"); }

int swapram_reserve(struct proc *process, caddr_t data, size_t data_length,
    caddr_t stack, size_t stack_length, size_t uarea_length)
{ (void)process; (void)data; (void)data_length; (void)stack;
  (void)stack_length; (void)uarea_length; return 0; }
void swapram_cancel(struct proc *process) { (void)process; }
void swapram_commit(struct proc *process) { (void)process; }
void swapram_in(struct proc *process, caddr_t data, caddr_t stack, caddr_t uarea)
{ (void)process; (void)data; (void)stack; (void)uarea; }
int swapram_present(struct proc *process) { (void)process; return 0; }
void swapram_put(struct proc *process, int segment, caddr_t source, size_t length)
{ (void)process; (void)segment; (void)source; (void)length; }

int
swap_with_buf_result(struct buf *buffer, size_t block, size_t address,
    int count, int flags)
{
    size_t byte_offset = media_block(block) * DEV_BSIZE;
    size_t sector = byte_offset / SECTOR_BYTES;
    size_t relative_sector = sector - image_base / SWAP_IMAGE_ALIGN;
    const unsigned char *source = (const unsigned char *)address;
    size_t index;

    HK_CHECK(buffer == &exchange_buffer);
    HK_CHECK(byte_offset + (size_t)count <= sizeof(media));
    if (ring_mode)
        HK_CHECK(byte_offset + (size_t)count <= RING_END * DEV_BSIZE);
    if (flags & B_READ) {
        bcopy(media + byte_offset, (void *)address, count);
        return 0;
    }
    media_writes++;
    if ((flags & B_SWAPIMAGE) != 0 &&
        (byte_offset & (SECTOR_BYTES - 1)) == 0) {
        if ((int)relative_sector == fail_erase_sector)
            return -1;
        for (index = 0; index < SECTOR_BYTES; index++)
            media[byte_offset + index] = 0xff;
        erase_counts[sector]++;
        erase_total++;
        program_counts[sector] = 0;
    }
    if (program_counts[sector] == 0) {
        if ((int)relative_sector == fail_program_sector)
            return -1;
        program_total++;
    }
    program_counts[sector]++;
    for (index = 0; index < (size_t)count; index++) {
        if ((media[byte_offset + index] & source[index]) != source[index])
            return -1;
        media[byte_offset + index] &= source[index];
    }
    return 0;
}

static size_t
free_blocks(void)
{
    struct mapent *entry;
    size_t total = 0;

    for (entry = map_entries; entry->m_size != 0; entry++)
        total += entry->m_size;
    return total;
}

static void
run_exchange(void)
{
    swap_exchange_inplace(&panic_reservation, &panic_outgoing,
        &panic_incoming);
}

static void
successful_case(size_t outgoing_data, size_t outgoing_stack,
    size_t incoming_data, size_t incoming_stack)
{
    size_t outgoing_span = image_span(outgoing_data - outgoing_text,
        outgoing_stack);
    size_t incoming_span = image_span(incoming_data - incoming_text,
        incoming_stack);

    reset_case(outgoing_data, outgoing_stack, incoming_data, incoming_stack, 1);
    HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_outgoing,
        &panic_incoming, 0) == 0);
    HK_CHECK(panic_reservation.span == outgoing_span);
    if (outgoing_span > incoming_span) {
        int tail_reserved_before_media = free_blocks() ==
            (ring_mode ? RING_END - RING_FIRST : 380 - image_base) -
            outgoing_span;

        HK_CHECK(tail_reserved_before_media);
    }
    if (outgoing_data == incoming_data && outgoing_stack == incoming_stack &&
        outgoing_text == incoming_text)
        HK_CHECK(panic_reservation.span == panic_reservation.exchange_span);
    run_exchange();
    HK_CHECK(text_restores == (incoming_text != 0 ? 1u : 0u));
    HK_CHECK(same_bytes((unsigned char *)__user_data_start,
        expected_in_data, incoming_data));
    HK_CHECK(same_bytes((unsigned char *)__user_data_end - incoming_stack,
        expected_in_stack, incoming_stack));
    HK_CHECK(same_bytes((unsigned char *)&u0, expected_in_u, USIZE));
    HK_CHECK(image_matches(image_base, expected_out_data + outgoing_text,
        outgoing_data - outgoing_text, expected_out_stack, outgoing_stack,
        expected_out_u));
    HK_CHECK(erase_total == outgoing_span / SWAP_IMAGE_ALIGN);
    HK_CHECK(program_total == outgoing_span / SWAP_IMAGE_ALIGN);

    u.u_procp = &panic_incoming;
    HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_incoming,
        &panic_outgoing, 0) == 0);
    swap_exchange_inplace(&panic_reservation, &panic_incoming,
        &panic_outgoing);
    HK_CHECK(same_bytes((unsigned char *)__user_data_start,
        expected_out_data, outgoing_data));
    HK_CHECK(same_bytes((unsigned char *)__user_data_end - outgoing_stack,
        expected_out_stack, outgoing_stack));
    HK_CHECK(same_bytes((unsigned char *)&u0, expected_out_u, USIZE));
    HK_CHECK(image_matches(image_base, expected_in_data + incoming_text,
        incoming_data - incoming_text, expected_in_stack, incoming_stack,
        expected_in_u));
    HK_CHECK(text_restores == (incoming_text != 0 ? 1u : 0u) +
        (outgoing_text != 0 ? 1u : 0u));
    HK_CHECK(erase_total ==
        (outgoing_span + incoming_span) / SWAP_IMAGE_ALIGN);
    HK_CHECK(program_total ==
        (outgoing_span + incoming_span) / SWAP_IMAGE_ALIGN);
}

static void
grid_cases(void)
{
    /* Sector multiples and sizes that end inside a block and a sector. */
    static const size_t data_sizes[] = { 0, 3000, 4096, 32768, 37000, 65536 };
    static const size_t stack_sizes[] = { 1024, 5000, 8192, 24576 };
    size_t od, os, id, is;

    for (od = 0; od < sizeof(data_sizes) / sizeof(data_sizes[0]); od++)
        for (os = 0; os < sizeof(stack_sizes) / sizeof(stack_sizes[0]); os++)
            for (id = 0; id < sizeof(data_sizes) / sizeof(data_sizes[0]); id++)
                for (is = 0; is < sizeof(stack_sizes) / sizeof(stack_sizes[0]);
                    is++)
                    if (data_sizes[od] + stack_sizes[os] <= USER_TOP_BYTES &&
                        data_sizes[id] + stack_sizes[is] <= USER_TOP_BYTES)
                        successful_case(data_sizes[od], stack_sizes[os],
                            data_sizes[id], stack_sizes[is]);
}

/*
 * Clean text stays out of the image and returns from the executable, so the
 * saved data starts above it in the window while its image offset stays 0.
 */
static void
text_cases(void)
{
    static const struct {
        size_t out_data, out_stack, out_text;
        size_t in_data, in_stack, in_text;
    } cases[] = {
        { 32768, 8192, 8192, 4096, 1024, 0 },
        { 4096, 1024, 0, 32768, 8192, 8192 },
        { 32768, 8192, 8192, 32768, 8192, 8192 },
        { 37000, 5000, 5000, 65536, 24576, 12288 },
        { 65536, 24576, 65536, 32768, 8192, 4096 },
        { 65536, 24576, 3000, 37000, 5000, 37000 },
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        outgoing_text = cases[index].out_text;
        incoming_text = cases[index].in_text;
        successful_case(cases[index].out_data, cases[index].out_stack,
            cases[index].in_data, cases[index].in_stack);
    }
    outgoing_text = incoming_text = 0;
}

static void
tail_refusal(void)
{
    unsigned char before[MEDIA_BYTES];
    struct mapent map_before[16];
    size_t cursor_before;

    reset_case(65536, 24576, 4096, 1024, 0);
    bcopy(media, before, sizeof(before));
    bcopy(map_entries, map_before, sizeof(map_before));
    cursor_before = swapnext;
    HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_outgoing,
        &panic_incoming, 0) == ENOMEM);
    HK_CHECK(media_writes == 0);
    HK_CHECK(same_bytes(media, before, sizeof(media)));
    HK_CHECK(same_bytes((unsigned char *)map_entries,
        (unsigned char *)map_before, sizeof(map_before)));
    HK_CHECK(swapnext == cursor_before);
}

/* An owned tail with a whole free run elsewhere takes one fresh extent. */
static void
fresh_extent_fallback(void)
{
    unsigned char before[MEDIA_BYTES];
    struct mapent map_before[16];
    size_t run = image_base + image_span(4096, 1024) + 8;

    reset_case(65536, 24576, 4096, 1024, 2);
    bcopy(media, before, sizeof(before));
    bcopy(map_entries, map_before, sizeof(map_before));
    HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_outgoing,
        &panic_incoming, 0) == 0);
    HK_CHECK(panic_reservation.tier == SWAP_IMAGE_FLASH);
    HK_CHECK(panic_reservation.state == SWAP_IMAGE_RESERVED);
    HK_CHECK(panic_reservation.exchange_span == 0);
    HK_CHECK(panic_reservation.addr[0] == run);
    HK_CHECK(map_entries[0].m_addr == run + image_span(65536, 24576));
    HK_CHECK(media_writes == 0);
    HK_CHECK(same_bytes(media, before, sizeof(media)));
    swap_image_cancel(&panic_reservation);
    HK_CHECK(same_bytes((unsigned char *)map_entries,
        (unsigned char *)map_before, sizeof(map_before)));
}

static void
fault_matrix(void)
{
    size_t sector, sectors = image_span(32768, 8192) / SWAP_IMAGE_ALIGN;

    for (sector = 0; sector < sectors; sector++) {
        reset_case(32768, 8192, 32768, 8192, 1);
        HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_outgoing,
            &panic_incoming, 0) == 0);
        fail_erase_sector = sector;
        HK_EXPECT_PANIC("swap exchange: media fault", run_exchange);

        reset_case(32768, 8192, 32768, 8192, 1);
        HK_CHECK(swap_exchange_switch(&panic_reservation, &panic_outgoing,
            &panic_incoming, 0) == 0);
        fail_program_sector = sector;
        HK_EXPECT_PANIC("swap exchange: media fault", run_exchange);
    }
}

static size_t
replay_allocate(size_t span, size_t *cursor, size_t *base)
{
    size_t addresses[3];
    size_t allocated;

    allocated = malloc3_contiguous_next(swapmap, span, 0, 0,
        SWAP_IMAGE_ALIGN, cursor, addresses);
    if (allocated != 0)
        *base = addresses[0];
    return allocated;
}

/* Replay the measured boot image order through the production allocator. */
static void
boot_map_replay(void)
{
    static const size_t boot_spans[] = { 12, 8, 20, 20 };
    static const size_t test_spans[] = { 68, 108, 180 };
    static const unsigned expected_successes[] = { 95, 95, 86 };
    unsigned successes[3] = { 0, 0, 0 };
    size_t initial_cursor, cursor, base, index, test_index;

    for (initial_cursor = 4; initial_cursor < 384; initial_cursor += 4) {
        struct mapent boot_map[16];
        size_t boot_cursor;

        bzero(map_entries, sizeof(map_entries));
        map_entries[0].m_addr = 4;
        map_entries[0].m_size = 380;
        cursor = initial_cursor;
        for (index = 0; index < sizeof(boot_spans) / sizeof(boot_spans[0]);
            index++)
            HK_CHECK(replay_allocate(boot_spans[index], &cursor, &base) ==
                boot_spans[index]);
        bcopy(map_entries, boot_map, sizeof(boot_map));
        boot_cursor = cursor;

        for (test_index = 0;
            test_index < sizeof(test_spans) / sizeof(test_spans[0]);
            test_index++) {
            bcopy(boot_map, map_entries, sizeof(boot_map));
            cursor = boot_cursor;
            if (replay_allocate(test_spans[test_index], &cursor, &base) != 0)
                successes[test_index]++;
            if (initial_cursor == 4) {
                HK_CHECK(base == 64);
                HK_CHECK(map_entries[0].m_addr == 64 + test_spans[test_index]);
                HK_CHECK(map_entries[0].m_size == 320 - test_spans[test_index]);
            }
        }

        if (initial_cursor >= 148 && initial_cursor <= 180) {
            bcopy(boot_map, map_entries, sizeof(boot_map));
            cursor = boot_cursor;
            HK_CHECK(replay_allocate(180, &cursor, &base) == 0);
            HK_CHECK(map_entries[0].m_addr == 4);
            HK_CHECK(map_entries[0].m_size == initial_cursor - 4);
            HK_CHECK(map_entries[1].m_addr == initial_cursor + 60);
            HK_CHECK(map_entries[1].m_size == 324 - initial_cursor);
        }
    }
    for (test_index = 0;
        test_index < sizeof(test_spans) / sizeof(test_spans[0]); test_index++)
        HK_CHECK(successes[test_index] == expected_successes[test_index]);
}

int
main(void)
{
    grid_cases();
    text_cases();
    tail_refusal();
    fresh_extent_fallback();
    fault_matrix();
    boot_map_replay();
    /* The same exchanges on an image that passes the end of a ring map. */
    ring_mode = 1;
    grid_cases();
    text_cases();
    ring_mode = 0;
    return hk_verdict("swap exchange in place");
}
