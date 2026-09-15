#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>

struct namecache namecache[NNAMECACHE];
u_int nextinodeid;

#ifdef LINEAR_NAME_CACHE
struct namecache *nchrecycle_test(void);

static int
check_linear_cache_init(void)
{
    unsigned entry_index;

    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++) {
        struct namecache *entry = &namecache[entry_index];

        if (entry->nc_ip != 0)
            return 1;
        if (entry->nc_dev != NODEV || entry->nc_idev != NODEV)
            return 2;
        if (entry->nc_used != 0)
            return 3;
    }
    return 0;
}

static int
check_linear_replacement(void)
{
    struct inode cached_inodes[NNAMECACHE] = {{0}};
    struct namecache *entry;
    unsigned entry_index;

    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++) {
        namecache[entry_index].nc_ip = &cached_inodes[entry_index];
        namecache[entry_index].nc_used = 1;
    }
    entry = nchrecycle_test();
    if (entry != &namecache[0])
        return 18;
    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++)
        if (namecache[entry_index].nc_used != 0)
            return 19;

    entry = nchrecycle_test();
    if (entry != &namecache[1])
        return 20;
    namecache[2].nc_ip = 0;
    namecache[2].nc_used = 1;
    entry = nchrecycle_test();
    if (entry != &namecache[2])
        return 21;
    return 0;
}

static int
check_linear_device_invalidation(void)
{
    struct inode cached_inodes[NNAMECACHE] = {{0}};
    unsigned entry_index;

    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++) {
        namecache[entry_index].nc_ip = &cached_inodes[entry_index];
        namecache[entry_index].nc_dev = (dev_t)(entry_index + 3);
        namecache[entry_index].nc_idev = (dev_t)(entry_index + 4);
        namecache[entry_index].nc_id = (u_short)(entry_index + 1);
        namecache[entry_index].nc_ino = (ino_t)(entry_index + 1);
        namecache[entry_index].nc_used = 1;
    }
    namecache[1].nc_idev = 3;
    nchinval(3);
    for (entry_index = 0; entry_index < 2; entry_index++) {
        if (namecache[entry_index].nc_ip != 0 ||
            namecache[entry_index].nc_dev != NODEV ||
            namecache[entry_index].nc_idev != NODEV ||
            namecache[entry_index].nc_id != 0 ||
            namecache[entry_index].nc_ino != 0 ||
            namecache[entry_index].nc_used != 0)
            return 22;
    }
    if (namecache[2].nc_ip != &cached_inodes[2] ||
        namecache[3].nc_ip != &cached_inodes[3])
        return 23;
    return 0;
}
#else
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
#endif

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

static int
check_generation_wrap(void)
{
    struct inode target_inode = {0};
    unsigned entry_index;

    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++)
        namecache[entry_index].nc_id = 17;
    namecache[0].nc_ip = &target_inode;
    nextinodeid = 0xffff;
    cacheinval(&target_inode);
    if (target_inode.i_id == 0)
        return 15;
    for (entry_index = 0; entry_index < NNAMECACHE; entry_index++)
        if (namecache[entry_index].nc_id != 0)
            return 16;
    if (NCH_CACHE_HIT_VALID(&namecache[0], &target_inode, 0))
        return 17;
    return 0;
}

static int
check_compact_flags(void)
{
#ifdef COMPACT_INODE_FIELDS
    if (!INODE_PERSISTENT_FLAGS_SUPPORTED(0))
        return 24;
    if (!INODE_PERSISTENT_FLAGS_SUPPORTED(UF_SETTABLE | SF_SETTABLE))
        return 25;
    if (INODE_PERSISTENT_FLAGS_SUPPORTED(0x10000U))
        return 26;
#endif
    return 0;
}

int
main(void)
{
    int status;

    nchinit();
#ifdef LINEAR_NAME_CACHE
    status = check_linear_cache_init();
    if (status != 0)
        return status;
    status = check_linear_replacement();
    if (status != 0)
        return status;
    status = check_linear_device_invalidation();
#else
    status = check_lru_chain();
    if (status != 0)
        return status;
    status = check_hash_chains();
    if (status != 0)
        return status;
    status = check_hash_indices();
#endif
    if (status != 0)
        return status;
    status = check_cache_hit_validation();
    if (status != 0)
        return status;
    status = check_generation_wrap();
    if (status != 0)
        return status;
    return check_compact_flags();
}
