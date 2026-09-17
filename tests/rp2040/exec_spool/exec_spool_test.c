/*
 * Host test for the production exec argument spool. The test includes the
 * shipped implementation and supplies bounded SwapRAM and buffer models
 * around it. Linker section collection removes unrelated exec routines whose
 * target-address assumptions cannot run on the host.
 *
 * The raw-swap reservation is not modelled: the Makefile links
 * sys/kern/subr_rmap.c, so the run the spool gets is the one the kernel's own
 * cyclic next-fit allocator picks. What remains here is a pair of wrappers
 * that record the arguments and the result, which is what the scenarios below
 * assert on. A model stood here until the allocator gained its cursor, and
 * then described an allocator the kernel no longer had.
 *
 * A plain malloc() from the spool would be a regression -- the reservation
 * has to go through the cursor, or two images in a row land on one erase
 * unit -- so that name is bound to a refusal rather than to the allocator.
 */

#define malloc spool_forbidden_malloc
#include "../../../sys/kern/exec_subr.c"
#undef malloc

/*
 * sys/kern/subr_rmap.c compiles with these names moved aside, because libc
 * owns malloc and mfree in a host binary. The Makefile carries the renames.
 */
size_t rmap_malloc3_contiguous_next (struct map *mp, size_t d_size,
    size_t s_size, size_t u_size, size_t align, size_t *nextp, size_t a[3]);
void rmap_mfree (struct map *mp, size_t size, size_t addr);

#define FLASH_BLOCKS 32
#define SPOOL_RAM_BYTES (NCARGS * 3)

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        failures++; \
        printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

static struct buf modeled_buffer;
static u_char modeled_buffer_data[MAXBSIZE];
static u_char modeled_flash[FLASH_BLOCKS * DEV_BSIZE];
static u_char modeled_swapram[SPOOL_RAM_BYTES];
static int backend_error;
static int buffer_busy;
static int buffer_claims;
static int buffer_releases;
static int flash_allocation_failure;
static int flash_allocation_live;
static int flash_reads;
static int flash_writes;
static int swapram_allowed;
static int swapram_live;
static int swapram_allocations;
static int swapram_frees;
static u_int swapram_size;
static const u_int swapram_offset = 37;
static size_t allocated_flash_block;
static size_t allocated_flash_span;
size_t swapnext;
static size_t published_cursor;
static int cursor_publications;
/*
 * The swap map the real allocator works on. Eight descriptors is more
 * headroom than a spool that holds one reservation at a time can use, so a
 * refusal here is a shortage of space rather than of descriptors.
 */
#define SPOOL_MAP_SLOTS 8
static struct mapent spool_map_entries[SPOOL_MAP_SLOTS];
static struct mapent exhausted_map_entries[1];
struct map swapmap[1] = {
    { spool_map_entries, &spool_map_entries[SPOOL_MAP_SLOTS - 1],
      "exec-spool-map" }
};

static void
bytes_zero (void *destination, size_t length)
{
    u_char *output = destination;

    while (length-- != 0)
        *output++ = 0;
}

static void
bytes_copy (void *destination, const void *source, size_t length)
{
    u_char *output = destination;
    const u_char *input = source;

    while (length-- != 0)
        *output++ = *input++;
}

static int
bytes_equal (const void *left, const void *right, size_t length)
{
    const u_char *left_byte = left;
    const u_char *right_byte = right;

    while (length-- != 0)
        if (*left_byte++ != *right_byte++)
            return 0;
    return 1;
}

static size_t
test_string_length (const char *string)
{
    size_t length = 0;

    while (string[length] != '\0')
        length++;
    return length + 1;
}

void
bzero (void *destination, size_t length)
{
    bytes_zero (destination, length);
}

void
bcopy (const void *source, void *destination, size_t length)
{
    bytes_copy (destination, source, length);
}

void
panic (char *message)
{
    printf ("FAIL panic: %s\n", message);
    __builtin_trap ();
}

struct buf *
geteblk (void)
{
    if (buffer_busy) {
        backend_error = 1;
        return NULL;
    }
    buffer_busy = 1;
    buffer_claims++;
    bzero (&modeled_buffer, sizeof modeled_buffer);
    modeled_buffer.b_addr = (caddr_t) modeled_buffer_data;
    return &modeled_buffer;
}

void
brelse (struct buf *bp)
{
    if (bp != &modeled_buffer || !buffer_busy)
        backend_error = 1;
    buffer_busy = 0;
    buffer_releases++;
}

size_t
spool_forbidden_malloc (struct map *map, size_t blocks)
{
    (void) map;
    (void) blocks;
    backend_error = 1;
    return 0;
}

/*
 * Record what the spool asked for, then let sys/kern/subr_rmap.c decide.
 * Injected failure empties the map rather than short-circuiting the call, so
 * the refusal comes from the allocator for the reason the board would see.
 */
size_t
malloc3_contiguous_next (struct map *map, size_t data_blocks,
    size_t stack_blocks, size_t user_blocks, size_t alignment,
    size_t *cursor, size_t addresses[3])
{
    size_t span;

    if (map != swapmap || stack_blocks != 0 || user_blocks != 0 ||
        alignment != SWAP_IMAGE_ALIGN || cursor != &swapnext ||
        flash_allocation_live)
        backend_error = 1;

    if (flash_allocation_failure)
        swapmap->m_map = exhausted_map_entries;
    span = rmap_malloc3_contiguous_next (map, data_blocks, stack_blocks,
        user_blocks, alignment, cursor, addresses);
    if (flash_allocation_failure)
        swapmap->m_map = spool_map_entries;

    if (span == 0)
        return 0;
    allocated_flash_block = addresses[0];
    allocated_flash_span = span;
    flash_allocation_live = 1;
    return span;
}

/* The kernel publishes the cursor to the watchdog scratch register after
 * every reservation; the model records the value and the count. */
void
swap_cursor_publish (size_t next)
{
    published_cursor = next;
    cursor_publications++;
}

/*
 * The spool returns the whole run it reserved. Anything else is a fault in
 * the spool, and passing it through to the allocator would only turn a
 * readable failure into a panic, so the wrapper records it and stops.
 */
void
mfree (struct map *map, size_t span, size_t block)
{
    if (map != swapmap || !flash_allocation_live ||
        span != allocated_flash_span || block != allocated_flash_block) {
        backend_error = 1;
        return;
    }
    flash_allocation_live = 0;
    allocated_flash_block = 0;
    allocated_flash_span = 0;
    rmap_mfree (map, span, block);
}

void
swap_with_buf (struct buf *bp, size_t block, size_t core_address,
    int count, int flags)
{
    u_char *core;
    size_t byte_offset = block * DEV_BSIZE;

    /* The target size_t carries the 32-bit buffer address; use the modeled
     * header's native host pointer after verifying the production argument is
     * its low 32 bits. */
    core = (u_char *) bp->b_addr;
    if (bp != &modeled_buffer || !buffer_busy || count < 0 ||
        core_address != (size_t) bp->b_addr ||
        byte_offset > sizeof modeled_flash - (size_t) count) {
        backend_error = 1;
        return;
    }
    if (flags & B_READ) {
        bytes_copy (core, modeled_flash + byte_offset, count);
        flash_reads++;
    } else {
        if ((flags & B_SWAPIMAGE) == 0)
            backend_error = 1;
        bytes_copy (modeled_flash + byte_offset, core, count);
        flash_writes++;
    }
}

int
swapram_spool_alloc (unsigned int size, unsigned int *offset)
{
    if (!swapram_allowed || swapram_live || size > sizeof modeled_swapram)
        return -1;
    swapram_live = 1;
    swapram_size = size;
    swapram_allocations++;
    *offset = swapram_offset;
    return 0;
}

void
swapram_spool_write (unsigned int offset, unsigned int position,
    const void *source, unsigned int length)
{
    if (!swapram_live || offset != swapram_offset ||
        position > swapram_size || length > swapram_size - position) {
        backend_error = 1;
        return;
    }
    bytes_copy (modeled_swapram + position, source, length);
}

void
swapram_spool_read (unsigned int offset, unsigned int position,
    void *destination, unsigned int length)
{
    if (!swapram_live || offset != swapram_offset ||
        position > swapram_size || length > swapram_size - position) {
        backend_error = 1;
        return;
    }
    bytes_copy (destination, modeled_swapram + position, length);
}

void
swapram_spool_free (unsigned int offset, unsigned int size)
{
    if (!swapram_live || offset != swapram_offset || size != swapram_size)
        backend_error = 1;
    swapram_live = 0;
    swapram_size = 0;
    swapram_frees++;
}

static void
reset_models (void)
{
    bytes_zero (&modeled_buffer, sizeof modeled_buffer);
    bytes_zero (modeled_buffer_data, sizeof modeled_buffer_data);
    bytes_zero (modeled_flash, sizeof modeled_flash);
    bytes_zero (modeled_swapram, sizeof modeled_swapram);
    backend_error = 0;
    buffer_busy = 0;
    buffer_claims = 0;
    buffer_releases = 0;
    flash_allocation_failure = 0;
    flash_allocation_live = 0;
    flash_reads = 0;
    flash_writes = 0;
    swapram_allowed = 1;
    swapram_live = 0;
    swapram_allocations = 0;
    swapram_frees = 0;
    swapram_size = 0;
    allocated_flash_block = 0;
    allocated_flash_span = 0;
    swapnext = SWAP_IMAGE_ALIGN;
    bytes_zero (spool_map_entries, sizeof spool_map_entries);
    bytes_zero (exhausted_map_entries, sizeof exhausted_map_entries);
    swapmap->m_map = spool_map_entries;
    /* init_main.c seeds the live map with exactly this call. */
    rmap_mfree (swapmap, FLASH_BLOCKS - SWAP_IMAGE_ALIGN, SWAP_IMAGE_ALIGN);
    published_cursor = 0;
    cursor_publications = 0;
}

static void
expect_records (struct exec_params *params, char **arguments,
    u_int argument_count, char **environment, u_int environment_count)
{
    static u_char observed[NCARGS + 1];
    char *expected;
    u_int index;
    u_short length;

    params->spool.pos = 0;
    params->spool.readblock = EXEC_SPOOL_BLOCK_NONE;
    for (index = 0; index < argument_count + environment_count; index++) {
        expected = index < argument_count ? arguments[index] :
            environment[index - argument_count];
        exec_spool_read (params, &length, sizeof length);
        CHECK (length == test_string_length (expected));
        CHECK (length <= sizeof observed);
        exec_spool_read (params, observed, length);
        CHECK (bytes_equal (observed, expected, length));
    }
    CHECK (params->spool.pos == params->spool.size);
}

static void
swapram_serialization (void)
{
    struct exec_params params;
    char *arguments[] = { "alpha", "beta", "gamma", NULL };
    char *environment[] = { "A=1", "B=two", NULL };

    reset_models ();
    bzero (&params, sizeof params);
    params.userargp = arguments;
    params.userenvp = environment;
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.spool.backing == EXEC_SPOOL_SWAPRAM);
    CHECK (params.argc == 3 && params.envc == 2);
    CHECK (params.argbc == 17 && params.envbc == 10);
    CHECK (swapram_allocations == 1 && swapram_live);
    CHECK (buffer_claims == 0 && flash_writes == 0);
    expect_records (&params, arguments, 3, environment, 2);
    exec_spool_discard (&params);
    CHECK (swapram_frees == 1 && !swapram_live);
    CHECK (params.spool.backing == EXEC_SPOOL_NONE);
    CHECK (!backend_error);
    printf ("swapram serialization: argv and env records replay and clean up\n");
}

static void
raw_boundary_fallback (void)
{
    struct exec_params params;
    static char long_argument[MAXBSIZE + 489];
    char *arguments[] = { "first", long_argument, "last", NULL };
    char *environment[] = { "MODE=raw", NULL };
    size_t index;

    reset_models ();
    swapram_allowed = 0;
    for (index = 0; index + 1 < sizeof long_argument; index++)
        long_argument[index] = (char) ('a' + index % 23);
    long_argument[sizeof long_argument - 1] = '\0';
    bzero (&params, sizeof params);
    params.userargp = arguments;
    params.userenvp = environment;
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.spool.backing == EXEC_SPOOL_FLASH);
    CHECK (buffer_claims == 1 && buffer_busy);
    CHECK (flash_allocation_live && flash_writes == 2);
    CHECK (allocated_flash_block == SWAP_IMAGE_ALIGN);
    CHECK (swapnext == SWAP_IMAGE_ALIGN + allocated_flash_span);
    CHECK (cursor_publications == 1 && published_cursor == swapnext);
    expect_records (&params, arguments, 3, environment, 1);
    CHECK (flash_reads >= 2);
    CHECK (buffer_claims == 1);
    exec_spool_discard (&params);
    CHECK (!flash_allocation_live && !buffer_busy);
    CHECK (buffer_releases == 1 && !backend_error);
    printf ("raw fallback: one buffer replays a record across block boundaries\n");
}

static void
swapram_to_flash_migration (void)
{
    struct exec_params params;
    static char crossing_argument[MAXBSIZE + 73];
    char *arguments[] = { "migrate", crossing_argument, NULL };
    char *environment[] = { "WINDOW=LARGE", NULL };
    size_t index;

    reset_models ();
    for (index = 0; index + 1 < sizeof crossing_argument; index++)
        crossing_argument[index] = (char) ('A' + index % 19);
    crossing_argument[sizeof crossing_argument - 1] = '\0';
    bzero (&params, sizeof params);
    params.userargp = arguments;
    params.userenvp = environment;
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.spool.backing == EXEC_SPOOL_SWAPRAM);
    CHECK (exec_spool_to_flash (&params) == 0);
    CHECK (params.spool.backing == EXEC_SPOOL_FLASH);
    CHECK (!swapram_live && swapram_frees == 1);
    CHECK (buffer_claims == 1 && flash_writes == 2);
    expect_records (&params, arguments, 2, environment, 1);
    exec_spool_discard (&params);
    CHECK (buffer_releases == 1 && !backend_error);

    reset_models ();
    bzero (&params, sizeof params);
    params.userargp = arguments;
    params.userenvp = environment;
    CHECK (exec_save_args (&params) == 0);
    flash_allocation_failure = 1;
    CHECK (exec_spool_to_flash (&params) == ENOMEM);
    CHECK (params.spool.backing == EXEC_SPOOL_SWAPRAM);
    CHECK (swapram_live && swapram_frees == 0);
    CHECK (cursor_publications == 0);
    expect_records (&params, arguments, 2, environment, 1);
    exec_spool_discard (&params);
    CHECK (swapram_frees == 1 && !backend_error);
    printf ("migration: SwapRAM moves to flash and survives allocation refusal\n");
}

static void
flash_cursor_rotation (void)
{
    struct exec_params params;
    char *arguments[] = { "rotate", NULL };
    size_t first_block, first_span;

    reset_models ();
    swapram_allowed = 0;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.spool.backing == EXEC_SPOOL_FLASH);
    first_block = allocated_flash_block;
    first_span = allocated_flash_span;
    CHECK (first_block == SWAP_IMAGE_ALIGN);
    CHECK (swapnext == first_block + first_span);
    CHECK (cursor_publications == 1 && published_cursor == swapnext);
    exec_spool_discard (&params);

    /* The freed run stays behind the cursor: the next spool lands after it
     * rather than returning to the lowest free sector. */
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == 0);
    CHECK (allocated_flash_block == first_block + first_span);
    CHECK (cursor_publications == 2 && published_cursor == swapnext);
    exec_spool_discard (&params);

    /* A cursor at the end of the unit wraps to the first aligned run. */
    swapnext = FLASH_BLOCKS;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == 0);
    CHECK (allocated_flash_block == SWAP_IMAGE_ALIGN);
    CHECK (published_cursor == SWAP_IMAGE_ALIGN + allocated_flash_span);
    exec_spool_discard (&params);
    CHECK (!flash_allocation_live && !backend_error);
    printf ("flash rotation: the swap cursor advances, publishes and wraps\n");
}

static void
script_argument_order (void)
{
    struct exec_params params;
    char *user_arguments[] = { "ignored-name", "one", "two", NULL };
    char *expected[] = { "/bin/sh", "-e", "/tmp/script", "one", "two" };

    reset_models ();
    bzero (&params, sizeof params);
    params.userfname = "/tmp/script";
    params.userargp = user_arguments;
    params.sh.interpreted = 1;
    bytes_copy (params.sh.interpname, "/bin/sh", sizeof "/bin/sh");
    bytes_copy (params.sh.interparg, "-e", sizeof "-e");
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.argc == 5 && params.envc == 0);
    expect_records (&params, expected, 5, NULL, 0);
    exec_spool_discard (&params);

    reset_models ();
    bzero (&params, sizeof params);
    params.userfname = "/tmp/script";
    params.userargp = user_arguments;
    params.sh.interpreted = 1;
    bytes_copy (params.sh.interpname, "/bin/sh", sizeof "/bin/sh");
    expected[1] = "/tmp/script";
    expected[2] = "one";
    expected[3] = "two";
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.argc == 4);
    expect_records (&params, expected, 4, NULL, 0);
    exec_spool_discard (&params);
    CHECK (!backend_error);
    printf ("script ordering: interpreter, option, script, and tail preserve ABI order\n");
}

static void
argument_limits (void)
{
    struct exec_params params;
    static char exact_limit[NCARGS];
    static char over_limit[NCARGS + 1];
    static char half_limit[NCARGS / 2 + 1];
    char *arguments[2];
    char *environment[2];
    size_t index;

    reset_models ();
    for (index = 0; index + 1 < sizeof exact_limit; index++)
        exact_limit[index] = 'x';
    exact_limit[sizeof exact_limit - 1] = '\0';
    arguments[0] = exact_limit;
    arguments[1] = NULL;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == 0);
    CHECK (params.argbc == NCARGS && params.spool.size == NCARGS + 2);
    exec_spool_discard (&params);

    reset_models ();
    for (index = 0; index + 1 < sizeof over_limit; index++)
        over_limit[index] = 'y';
    over_limit[sizeof over_limit - 1] = '\0';
    arguments[0] = over_limit;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == E2BIG);
    CHECK (!swapram_live && !flash_allocation_live && buffer_claims == 0);

    reset_models ();
    for (index = 0; index + 1 < sizeof half_limit; index++)
        half_limit[index] = 'z';
    half_limit[sizeof half_limit - 1] = '\0';
    arguments[0] = half_limit;
    environment[0] = half_limit;
    environment[1] = NULL;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    params.userenvp = environment;
    CHECK (exec_save_args (&params) == E2BIG);
    CHECK (!swapram_live && !flash_allocation_live && buffer_claims == 0);
    CHECK (!backend_error);
    printf ("limits: NCARGS exact boundary succeeds and overflow allocates nothing\n");
}

static void
allocation_failures (void)
{
    struct exec_params params;
    char *arguments[] = { "allocation", "failure", NULL };

    reset_models ();
    swapram_allowed = 0;
    flash_allocation_failure = 1;
    bzero (&params, sizeof params);
    params.userargp = arguments;
    CHECK (exec_save_args (&params) == ENOMEM);
    CHECK (params.spool.backing == EXEC_SPOOL_NONE);
    CHECK (!flash_allocation_live && buffer_claims == 0);
    CHECK (!backend_error);
    printf ("allocation refusal: failed backends leave every reservation free\n");
}

int
main (void)
{
    swapram_serialization ();
    raw_boundary_fallback ();
    swapram_to_flash_migration ();
    flash_cursor_rotation ();
    script_argument_order ();
    argument_limits ();
    allocation_failures ();
    if (failures != 0) {
        printf ("exec spool: %d failures\n", failures);
        return 1;
    }
    printf ("exec spool: ok\n");
    return 0;
}
