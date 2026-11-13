#ifndef _MACHINE_SWAPRAM_H_
#define _MACHINE_SWAPRAM_H_

/*
 * A compressed RAM tier in front of the flash swap unit fl1.
 *
 * dev/flash.c turns each 1 KB swap block into a 4 KB read-modify-write of
 * QSPI NOR -- a 45 ms sector erase plus page programs -- so a 33 KB image
 * costs about half a second and wears the part. This tier compresses the
 * same image with heatshrink into a kernel RAM pool and keeps it there when
 * it fits; kern/vm_swap.c falls through to the flash path unchanged when it
 * does not.
 *
 * The option SWAPRAM selects the tier and pulls swapram.c, swapram_pool.c,
 * and the heatshrink codec into the build (arch/rp2040/conf/files.rp2040).
 * SWAPRAM_KB sizes the pool in kilobytes of bss.
 */

#ifndef SWAPRAM_KB
#define SWAPRAM_KB      64
#endif

#if defined(SWAPRAM) && SWAPRAM_KB < 1
#error "SWAPRAM_KB must be at least 1: a zero-byte pool sends every image to flash while still paying the text and bss of the tier"
#endif

/*
 * Allocations round up to this many bytes. Four keeps every image aligned
 * for the word copies the codec's callers make and wastes at most 3 bytes
 * per image.
 */
#define SWAPRAM_GRAIN   4

/*
 * swapram_pool.c: a first-fit byte allocator over a caller-supplied buffer,
 * with no kernel dependency, so the host test in arch/rp2040/test/swapram
 * exercises the shipped code. Every entry point reports an inconsistency
 * by return value; swapram.c turns that into a panic.
 */
struct swapram_seg {
    unsigned int    s_off;              /* byte offset into the pool */
    unsigned int    s_size;             /* bytes, a multiple of the grain */
};

struct swapram_pool {
    unsigned char       *p_base;
    unsigned int        p_size;         /* bytes in the pool */
    unsigned int        p_nseg;         /* entries in p_seg */
    unsigned int        p_used;         /* entries in use */
    struct swapram_seg  *p_seg;         /* free segments, address order */
};

void swapram_pool_init(struct swapram_pool *, unsigned char *, unsigned int,
    struct swapram_seg *, unsigned int);
int swapram_pool_alloc(struct swapram_pool *, unsigned int, unsigned int *);
int swapram_pool_free(struct swapram_pool *, unsigned int, unsigned int);
int swapram_pool_trim(struct swapram_pool *, unsigned int, unsigned int,
    unsigned int);
unsigned int swapram_pool_avail(struct swapram_pool *);
unsigned int swapram_pool_largest(struct swapram_pool *);

/*
 * Segment indices, in the order swapout writes them and swapin reads them.
 */
#define SWAPRAM_DATA    0
#define SWAPRAM_STACK   1
#define SWAPRAM_U       2
#define SWAPRAM_NSEG    3

/*
 * The swapper runs the SwapRAM encoder, SwapRAM decoder, and packed-text
 * decoder sequentially. One owner-checked workspace therefore replaces the
 * three permanent codec objects. Size assertions beside each user catch any
 * codec configuration that outgrows the shared extent.
 */
#define SWAPRAM_CODEC_WORK_BYTES    1040
#define SWAPRAM_CODEC_ENCODER       1
#define SWAPRAM_CODEC_DECODER       2
#define SWAPRAM_CODEC_PACKED_TEXT   3

/* machdep.swapram_evacuate: written as PENDING, read back as the result. */
#define SWAPRAM_EVAC_IDLE       0
#define SWAPRAM_EVAC_PENDING    1
#define SWAPRAM_EVAC_DONE       2
#define SWAPRAM_EVAC_NOFLASH    3

#ifdef KERNEL
#include <sys/types.h>          /* size_t and caddr_t for the prototypes */

struct proc;

/*
 * swapram_reserve counts the compressed data and stack lengths, owns the
 * whole slot on success and returns 0 when the caller must try flash.
 * swapram_put writes one segment into the slot; swapram_commit trims its
 * unused tail and marks the image present. swapram_cancel returns a slot
 * without reading its bytes. swapram_out wraps reservation with attempt
 * accounting; vm_swap accounts only after complete backing admission so a
 * refused fork preserves diagnostics too. swapin calls swapram_in for any
 * process swapram_present reports on.
 */
int swapram_out(struct proc *, caddr_t, size_t, caddr_t, size_t, size_t);
int swapram_reserve(struct proc *, caddr_t, size_t, caddr_t, size_t, size_t);
void swapram_put(struct proc *, int, caddr_t, size_t);
void swapram_commit(struct proc *);
void swapram_cancel(struct proc *);
int swapram_present(struct proc *);
int swapram_uarea_prefix(struct proc *, void *, unsigned int);
void swapram_in(struct proc *, caddr_t, caddr_t, caddr_t);
void swapram_admit(int);
int swapram_images(void);
int swapram_evacuate(void);
void swapram_service(void);
int swapram_spool_alloc(unsigned int, unsigned int *);
void swapram_spool_write(unsigned int, unsigned int, const void *,
    unsigned int);
void swapram_spool_read(unsigned int, unsigned int, void *, unsigned int);
void swapram_spool_free(unsigned int, unsigned int);
void *swapram_codec_acquire(unsigned int);
void swapram_codec_release(unsigned int);
extern int swapram_evac;
extern unsigned char swapram_pool_mem[];
extern unsigned char swapram_codec_work[];
#endif

#endif /* _MACHINE_SWAPRAM_H_ */
