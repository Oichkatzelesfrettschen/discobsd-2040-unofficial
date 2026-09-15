/*
 * Host stand-in for sys/map.h. The kernel names its map allocator malloc,
 * which libc owns on the host, so that one name is rewritten for the
 * kernel sources compiled here; the structures and the other two entry
 * points are the shipped ones.
 */
#ifndef _SHIM_SYS_MAP_H_
#define _SHIM_SYS_MAP_H_
#define malloc rmap_malloc
struct map {
    struct mapent   *m_map;     /* start of the map */
    struct mapent   *m_limit;   /* address of last slot in map */
    char            *m_name;    /* name of resource */
};
struct mapent {
    size_t  m_size;             /* size of this segment of the map */
    size_t  m_addr;             /* resource-space addr of start of segment */
};
extern struct map swapmap[];
size_t rmap_malloc (struct map *mp, size_t nbytes);
void mfree (struct map *mp, size_t nbytes, size_t addr);
size_t malloc3 (struct map *mp, size_t d_size, size_t s_size, size_t u_size, size_t a[3]);
size_t malloc3_contiguous (struct map *mp, size_t d_size, size_t s_size,
    size_t u_size, size_t align, size_t a[3]);
size_t malloc3_contiguous_next (struct map *mp, size_t d_size,
    size_t s_size, size_t u_size, size_t align, size_t *nextp,
    size_t a[3]);
#endif
