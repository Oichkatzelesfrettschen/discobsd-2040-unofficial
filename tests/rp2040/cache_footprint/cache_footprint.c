#include <sys/param.h>
#include <sys/dir.h>
#include <sys/inode.h>
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
    unsigned bucket_index;
    unsigned char visited_buckets[NCHHASH] = {0};

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
                visited_buckets[actual_bucket] = 1;
            }
        }
    }

    for (bucket_index = 0; bucket_index < NCHHASH; bucket_index++)
        if (visited_buckets[bucket_index] == 0)
            return 9;
    return 0;
}

static int
check_cache_hit_validation(void)
{
    struct inode expected_inode = {0};
    struct inode replacement_inode = {0};
    struct namecache entry = {0};
    u_short expected_identifier = 17;

    expected_inode.i_id = expected_identifier;
    replacement_inode.i_id = 29;
    entry.nc_ip = &expected_inode;
    entry.nc_id = expected_identifier;
    if (!NCH_CACHE_HIT_VALID(&entry, &expected_inode,
        expected_identifier))
        return 10;

    entry.nc_ip = &replacement_inode;
    entry.nc_id = replacement_inode.i_id;
    if (entry.nc_id != entry.nc_ip->i_id)
        return 11;
    if (NCH_CACHE_HIT_VALID(&entry, &expected_inode,
        expected_identifier))
        return 12;

    entry.nc_ip = &expected_inode;
    entry.nc_id = expected_identifier;
    expected_inode.i_id++;
    if (NCH_CACHE_HIT_VALID(&entry, &expected_inode,
        expected_identifier))
        return 13;

    entry.nc_ip = 0;
    if (NCH_CACHE_HIT_VALID(&entry, &expected_inode,
        expected_identifier))
        return 14;
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
    status = check_hash_indices();
    if (status != 0)
        return status;
    return check_cache_hit_validation();
}
