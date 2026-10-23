/*-
 * Copyright (c) 1994 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the NetBSD
 *  Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Christopher G. Demetriou
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/map.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/namei.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/fcntl.h>
#include <sys/signalvar.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <machine/debug.h>

extern char sigcode[], esigcode[];

/* round up and down to page boundaries. */
#define ELF_ROUND(a, b)     (((a) + (b) - 1) & ~((b) - 1))
#define ELF_TRUNC(a, b)     ((a) & ~((b) - 1))

/*
 * elf_check(): Prepare an Elf binary's exec package
 *
 * First, set of the various offsets/lengths in the exec package.
 *
 * Then, mark the text image busy (so it can be demand paged) or error
 * out if this is not possible.  Finally, set up vmcmds for the
 * text, data, bss, and stack segments.
 */
int
exec_elf_check(struct exec_params *epp)
{
    struct elf_phdr *ph;
    unsigned entry;
    int error, i, resid;

    const char elfident[] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
                 ELFCLASS32, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV, 0};

    DEBUG("\texec_elf_check(): start\n");

    /*
     * Check that this is an ELF file that we can handle,
     * and do some sanity checks on the header
     */
    if (epp->hdr_len < (int)sizeof(struct elf_ehdr)) {
        DEBUG("\texec_elf_check(): error: wrong header length\n");
        DEBUG("\texec_elf_check(): end\n");
        return ENOEXEC;
    }
    for (i = 0; i < (int)sizeof elfident; i++) {
        if (epp->hdr.elf.e_ident[i] !=  elfident[i]) {
            DEBUG("\texec_elf_check(): error: not an elf\n");
            DEBUG("\texec_elf_check(): end\n");
            return ENOEXEC;
        }
    }
    if (epp->hdr.elf.e_type != ET_EXEC)
        return ENOEXEC;
    if (epp->hdr.elf.e_machine != ELF32_MACHDEP_ID || epp->hdr.elf.e_version != EV_CURRENT)
        return ENOEXEC;
    if (epp->hdr.elf.e_phentsize != sizeof(struct elf_phdr) ||
        epp->hdr.elf.e_phoff == 0 || epp->hdr.elf.e_phnum != 1)
        return ENOEXEC;
    if (epp->hdr.elf.e_shnum == 0 || epp->hdr.elf.e_shentsize != sizeof(struct elf_shdr))
        return ENOEXEC;

    /*
     * The loader supports one flat segment. Admit the table from the inode
     * size before allocating storage or asking rdwri to traverse it.
     */
    if (epp->ip->i_size < 0 ||
        epp->hdr.elf.e_phoff > UINT_MAX - sizeof(*ph) ||
        (unsigned long)epp->hdr.elf.e_phoff + sizeof(*ph) >
        (unsigned long)epp->ip->i_size)
        return ENOEXEC;
    ph = exec_alloc(sizeof(*ph), NBPW, epp);
    if (ph == NULL) {
        printf("can't alloc ELF program header\n");
        return ENOEXEC;
    }
    resid = 0;
    error = rdwri(UIO_READ, epp->ip, (caddr_t)ph, sizeof(*ph),
        epp->hdr.elf.e_phoff, IO_UNIT, &resid);
    if (error != 0 || resid != 0)
        return ENOEXEC;

    /*
     * Validate every segment-derived address and length before exec_estab
     * can replace the old image. The entry must name file-backed Thumb code;
     * BSS contains no instruction bytes.
     */
    if (ph->p_type != PT_LOAD || ph->p_flags != (PF_R | PF_W | PF_X) ||
        ph->p_vaddr != (unsigned)__user_data_start ||
        ph->p_filesz > ph->p_memsz || ph->p_filesz == 0)
        return ENOEXEC;
    if (ph->p_memsz > (unsigned)USER_WINDOW_MAX)
        return ENOMEM;
    if (ph->p_offset > UINT_MAX - ph->p_filesz ||
        (unsigned long)ph->p_offset + ph->p_filesz >
        (unsigned long)epp->ip->i_size)
        return ENOEXEC;
#if defined(__thumb__) || defined(__thumb2__)
    if ((epp->hdr.elf.e_entry & 1U) == 0)
        return ENOEXEC;
    entry = epp->hdr.elf.e_entry & ~1U;
#else
    entry = epp->hdr.elf.e_entry;
    if ((entry & 1U) != 0)
        return ENOEXEC;
#endif
    if (entry < ph->p_vaddr || entry - ph->p_vaddr >= ph->p_filesz)
        return ENOEXEC;

    if ((error = exec_save_args(epp)) != 0)
        return error;

    epp->text.len = epp->bss.len = epp->stack.len = epp->heap.len = 0;
    epp->text.vaddr = epp->bss.vaddr = epp->stack.vaddr =
        epp->heap.vaddr = NO_ADDR;
    epp->data.vaddr = (caddr_t)ph->p_vaddr;
    epp->data.len = ph->p_filesz;
    epp->bss.vaddr = epp->data.vaddr + ph->p_filesz;
    epp->bss.len = ph->p_memsz - ph->p_filesz;
    epp->heap.vaddr = epp->bss.vaddr + epp->bss.len;
    epp->heap.len = 0;
    epp->stack.len = SSIZE + roundup(epp->argbc + epp->envbc, NBPW) +
        (epp->argc + epp->envc + 4) * NBPW;
    epp->stack.vaddr = (caddr_t)USER_TOP(u.u_procp) - epp->stack.len;

    /*
     * Establish memory
     */
    if ((error = exec_estab(epp)) != 0)
        return error;

    /* The old image is gone. A failed read must leave no runnable image. */
    resid = 0;
    error = rdwri(UIO_READ, epp->ip, (caddr_t)ph->p_vaddr,
        ph->p_filesz, ph->p_offset, IO_UNIT, &resid);
    if (error != 0 || resid != 0) {
        psignal(u.u_procp, SIGKILL);
        return error != 0 ? error : EIO;
    }

    exec_clear(epp);
    exec_setupstack(epp->hdr.elf.e_entry, epp);

    DEBUG("\texec_elf_check(): end\n");

    return 0;
}
