/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_EXEC_XIP_H_
#define _SYS_EXEC_XIP_H_

#include "exec_aout.h"

#define EX_XIP          0x02
#define XREQ_SIG        0x51455258U     /* "XREQ" in little-endian order */
#define XREQ_VERSION    1U
#define AOUT_BADSTATE   6
#define AOUT_BADLIBC    7

struct xip_requirement {
    unsigned signature;
    unsigned version;
    unsigned length;
    unsigned char identity[32];
    unsigned slots;
    unsigned state_base;
    unsigned state_size;
};

/*
 * The caller supplies the process window after subtracting the actual stack
 * reservation. The raw file size includes the requirement before text.
 */
static inline int
xip_raw_layout(const struct exec *header, const struct xip_requirement *req,
    unsigned base, unsigned window, unsigned long filesize, int thumb)
{
    struct exec raw;
    unsigned end;
    int result;

    if (N_GETMAGIC(*header) != OMAGIC || N_GETMID(*header) != MID_ZERO ||
        N_GETFLAG(*header) != EX_XIP)
        return AOUT_BADMAG;
    if (filesize < sizeof raw + sizeof *req)
        return AOUT_TRUNC;
    if (req->signature != XREQ_SIG || req->version != XREQ_VERSION ||
        req->length != sizeof *req)
        return AOUT_BADMAG;
    raw = *header;
    N_SETMAGIC(raw, OMAGIC, MID_ZERO, 0);
    result = aout_layout_check(&raw, base, window, filesize - sizeof *req,
        thumb);
    if (result != AOUT_OK)
        return result;
    end = raw.a_text + raw.a_data + raw.a_bss;
    if (end > 0xffffffffU - base)
        return AOUT_SIZEWRAP;
    if (req->state_size == 0 || req->state_size > raw.a_bss ||
        (req->state_base & 7) != 0 ||
        req->state_base != base + end - req->state_size)
        return AOUT_BADSTATE;
    return AOUT_OK;
}

static inline int
xip_provider_check(const struct xip_requirement *req,
    const unsigned char identity[32], unsigned slots, unsigned state_size)
{
    unsigned i;

    if (req->slots > slots || req->state_size != state_size)
        return AOUT_BADLIBC;
    for (i = 0; i < sizeof req->identity; ++i)
        if (req->identity[i] != identity[i])
            return AOUT_BADLIBC;
    return AOUT_OK;
}

#endif
