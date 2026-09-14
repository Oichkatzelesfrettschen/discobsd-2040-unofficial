#ifndef _SYS_EXEC_HSAOUT_H_
#define _SYS_EXEC_HSAOUT_H_

#include "exec_aout.h"

/*
 * A packed a.out: struct exec with EX_HSPACK in its flag bits, then struct
 * hsx, then the text and the initialized data as two separate heatshrink
 * streams, and nothing after them. a_text, a_data, a_bss and a_entry keep
 * the raw image's values, so the loader lays the process out from struct
 * exec alone and struct hsx says only how the bytes are reproduced;
 * a_reltext, a_reldata and a_syms are zero because the container carries
 * neither relocation nor symbols. Text and data are separate streams so
 * the clean-text restoration at swapin expands the text alone.
 *
 * Every field is a little-endian 32-bit unsigned. x_hdrcrc is the CRC-32
 * of struct exec and struct hsx with x_hdrcrc itself zero; x_textcrc and
 * x_datacrc are the CRC-32 of the expanded bytes, and every expansion,
 * exec's two passes and swapin's restoration, recomputes and compares
 * them. x_window and x_lookahead name the heatshrink parameters the
 * streams were encoded with, and the loader accepts only the pair its
 * decoder is compiled for. x_reserved is zero. A version other than
 * HSX_VERSION is rejected rather than interpreted.
 */
#define EX_HSPACK       0x01        /* a.out flag: text and data are packed */

#define HSX_SIG         0x31585348  /* "HSX1" */
#define HSX_VERSION     1
#define HSX_CODEC_HEATSHRINK 1
#define HSX_WINDOW      9           /* heatshrink window bits, 512-byte state */
#define HSX_LOOKAHEAD   3           /* heatshrink lookahead bits */

struct hsx {
    unsigned x_sig;
    unsigned x_version;
    unsigned x_hdrlen;      /* sizeof (struct hsx) */
    unsigned x_codec;
    unsigned x_window;
    unsigned x_lookahead;
    unsigned x_ctext;       /* packed text length */
    unsigned x_cdata;       /* packed data length */
    unsigned x_textcrc;     /* CRC-32 of the expanded text */
    unsigned x_datacrc;     /* CRC-32 of the expanded data */
    unsigned x_reserved;
    unsigned x_hdrcrc;      /* CRC-32 of struct exec and struct hsx */
};

#define HSX_HDRSIZE     (sizeof (struct exec) + sizeof (struct hsx))

/*
 * CRC-32 (IEEE 802.3, reflected polynomial 0xedb88320), computed one bit
 * at a time from subr_crc32.c. Start from CRC32_INIT, feed every byte,
 * and finish with CRC32_FIN; the result matches zlib's crc32().
 */
#define CRC32_INIT      0xffffffffU
#define CRC32_FIN(crc)  ((crc) ^ 0xffffffffU)
unsigned crc32_update (unsigned crc, const void *buf, unsigned len);

/*
 * What the two headers must satisfy before either stream is touched. The
 * result names the first check that failed. The size check demands that
 * the file end exactly after the data stream, so a truncated or extended
 * container is refused before the decoder runs.
 */
#define HSX_OK          0
#define HSX_BADAOUT     1   /* magic, machine id, flag, or relocation and symbol sizes */
#define HSX_BADSIG      2   /* signature, version, length, codec, parameters, reserved, header CRC */
#define HSX_SIZEWRAP    3   /* the stream lengths and the headers wrap 32 bits */
#define HSX_BADSIZE     4   /* the file is not exactly the headers plus both streams */

static inline int
hsx_check (const struct exec *e, const struct hsx *x, unsigned long filesize)
{
    struct hsx h;
    unsigned crc, streams;

    if (N_GETMAGIC (*e) != OMAGIC || N_GETMID (*e) != MID_ZERO ||
        N_GETFLAG (*e) != EX_HSPACK)
        return HSX_BADAOUT;
    if (e->a_reltext != 0 || e->a_reldata != 0 || e->a_syms != 0)
        return HSX_BADAOUT;
    if (x->x_sig != HSX_SIG || x->x_version != HSX_VERSION ||
        x->x_hdrlen != sizeof (struct hsx) ||
        x->x_codec != HSX_CODEC_HEATSHRINK ||
        x->x_window != HSX_WINDOW || x->x_lookahead != HSX_LOOKAHEAD ||
        x->x_reserved != 0)
        return HSX_BADSIG;
    h = *x;
    h.x_hdrcrc = 0;
    crc = crc32_update (CRC32_INIT, e, sizeof (struct exec));
    crc = CRC32_FIN (crc32_update (crc, &h, sizeof h));
    if (crc != x->x_hdrcrc)
        return HSX_BADSIG;
    if (x->x_ctext > 0xffffffffU - x->x_cdata)
        return HSX_SIZEWRAP;
    streams = x->x_ctext + x->x_cdata;
    if (streams > 0xffffffffU - (unsigned) HSX_HDRSIZE)
        return HSX_SIZEWRAP;
    if (filesize != (unsigned long) HSX_HDRSIZE + streams)
        return HSX_BADSIZE;
    return HSX_OK;
}

#ifdef KERNEL
struct exec_params;
struct inode;
struct proc;
int exec_hsaout_check (struct exec_params *epp);
int exec_hsaout_text (struct inode *ip, const struct exec *e, char *dst,
    unsigned len);
#endif
#endif
