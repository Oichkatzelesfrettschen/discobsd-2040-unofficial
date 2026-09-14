#include <sys/param.h>
#include <sys/dir.h>
#include <sys/namei.h>

struct namecache namecache[NNAMECACHE];

extern struct namecache *nchhead;
extern struct namecache **nchtail;

static int
check_lru_chain(void)
{
    struct namecache **expected_previous = &nchhead;
    unsigned entry_index;

    if (nchhead != &namecache[0])
        return 1;

    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++) {
        struct namecache *entry = &namecache[entry_index];
        struct namecache *expected_next = entry_index + 1 < NNAMECACHE ?
            &namecache[entry_index + 1] : 0;

        if (entry->nc_prev != expected_previous)
            return 2;
        if (entry->nc_nxt != expected_next)
            return 3;
        if (entry->nc_forw != entry || entry->nc_back != entry)
            return 4;
        expected_previous = &entry->nc_nxt;
    }

    if (nchtail != expected_previous)
        return 5;
    return 0;
}

static int
check_hash_chains(void)
{
    unsigned bucket_index;

    for (bucket_index = 0; bucket_index < NCHHASH; bucket_index++) {
        if (nchash[bucket_index].nch_head[0] != &nchash[bucket_index])
            return 6;
        if (nchash[bucket_index].nch_head[1] != &nchash[bucket_index])
            return 7;
    }
    return 0;
}

static int
check_hash_indices(void)
{
    unsigned device_number;
    unsigned inode_number;
    unsigned name_hash;
    unsigned visited_buckets = 0;

    _Static_assert(NCHHASH < 8 * sizeof(visited_buckets),
        "hash coverage mask is too small");

    for (device_number = 0; device_number < 32; device_number++) {
        for (inode_number = 0; inode_number < 64; inode_number++) {
            for (name_hash = 0; name_hash < 256; name_hash++) {
                unsigned hash_value = (unsigned)(name_hash + inode_number +
                    13 * (int)device_number);
                unsigned expected_bucket = hash_value % NCHHASH;
                unsigned actual_bucket = NCHHASH_INDEX(name_hash,
                    inode_number, device_number);

                if (actual_bucket != expected_bucket ||
                    actual_bucket >= NCHHASH)
                    return 8;
                visited_buckets |= 1U << actual_bucket;
            }
        }
    }

    if (visited_buckets != (1U << NCHHASH) - 1)
        return 9;
    return 0;
}

int
main(void)
{
    int status;

    nchinit();
    status = check_lru_chain();
    if (status != 0)
        return status;
    status = check_hash_chains();
    if (status != 0)
        return status;
    return check_hash_indices();
}
