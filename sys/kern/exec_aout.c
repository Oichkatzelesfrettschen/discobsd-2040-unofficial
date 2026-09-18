#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/exec.h>
#include <sys/exec_aout.h>
#include <sys/dir.h>
#include <sys/uio.h>
#include <sys/signal.h>
#ifdef EXEC_HSAOUT
#include <sys/exec_hsaout.h>
#endif
#include <machine/debug.h>

/*
 * Executable-write exclusion. An image is read from its file at exec and
 * again at every swapin, so the file must not change while any process
 * runs it: exec refuses a file someone holds open for writing, marks it
 * ITEXT, which access() turns into ETXTBSY for every later writer, and
 * the mark stays until the last process whose p_tip names the inode lets
 * go. The exec dispatcher holds the inode locked from namei to iput, so
 * two execs of one file serialize and the scan below is exact.
 */
int
exec_text_hold(struct inode *ip)
{
    register struct file *fp;

    for (fp = file; fp < file + NFILE; fp++) {
        if (fp->f_count == 0 || fp->f_type != DTYPE_INODE)
            continue;
        if ((struct inode *)fp->f_data == ip && (fp->f_flag & FWRITE))
            return ETXTBSY;
    }
    ip->i_flag |= ITEXT;
    return 0;
}

/* Clear ITEXT on a held inode no process runs; the caller holds it locked. */
void
exec_text_unhold(struct inode *ip)
{
    register struct proc *q;

    for (q = allproc; q != NULL; q = q->p_nxt)
        if (q->p_tip == ip)
            return;
    ip->i_flag &= ~ITEXT;
}

/*
 * Drop p's text reference. next is the inode of the image replacing it,
 * which the caller holds locked and which keeps ITEXT through the new
 * p_tip; a process that exits passes NULL. The other case locks the old
 * inode so an exec of the same file elsewhere has either committed its
 * p_tip, which the scan then finds, or not yet marked it.
 */
void
exec_text_release(struct proc *p, struct inode *next)
{
    struct inode *ip = p->p_tip;

    if (ip == NULL)
        return;
    p->p_tip = NULL;
    if (ip == next) {
        irele(ip);
        return;
    }
    ilock(ip);
    exec_text_unhold(ip);
    iput(ip);
}

/*
 * The clean text for swapin, from whichever format the executable behind
 * p_tip is in: a raw image is read from behind its header, a packed one
 * is expanded and checked. Returns 0, or -1 when the file no longer
 * yields the text the process was loaded with.
 */
int
exec_text_restore(struct proc *p, caddr_t dst, size_t len)
{
    struct exec ex;
    int error, resid = 0;

    ILOCK(p->p_tip);
    error = rdwri(UIO_READ, p->p_tip, (caddr_t)&ex, sizeof ex, (off_t)0,
        IO_UNIT, &resid);
    if (error == 0 && resid == 0) {
#ifdef EXEC_HSAOUT
        if (N_GETFLAG(ex) == EX_HSPACK)
            error = exec_hsaout_text(p->p_tip, &ex, dst, len);
        else
#endif
        {
            error = rdwri(UIO_READ, p->p_tip, dst, len,
                (off_t)sizeof ex, IO_UNIT, &resid);
            if (resid != 0)
                error = -1;
        }
    } else
        error = -1;
    IUNLOCK(p->p_tip);
    return error ? -1 : 0;
}

int exec_aout_check(struct exec_params *epp)
{
    register struct proc *p;
    size_t tsize = 0;
    int error, thumb;

    DEBUG("\texec_aout_check(): start\n");

    if (epp->hdr_len < (int)sizeof(struct exec)) {
        DEBUG("\texec_aout_check(): error: wrong header length\n");
        DEBUG("\texec_aout_check(): end\n");
        return ENOEXEC;
    }

    /*
     * Refuse a header whose sizes wrap, whose image exceeds the window
     * or the file, or whose entry leaves the text before anything is
     * committed: exec_estab runs later, and the process keeps its old
     * image when this returns.
     */
#if defined(__thumb__) || defined(__thumb2__)
    thumb = 1;
#else
    thumb = 0;
#endif
    error = aout_layout_check(&epp->hdr.aout, (unsigned)__user_data_start,
        (unsigned)USER_WINDOW_MAX,
        (unsigned long)epp->ip->i_size, thumb);
    if (error != AOUT_OK) {
        DEBUG("\texec_aout_check(): error: layout check %d\n", error);
        DEBUG("\texec_aout_check(): end\n");
        return error == AOUT_TOOBIG ? ENOMEM : ENOEXEC;
    }

    switch (N_GETMAGIC(epp->hdr.aout)) {
    case OMAGIC:
        tsize = epp->hdr.aout.a_text;
        epp->hdr.aout.a_data += epp->hdr.aout.a_text;
        epp->hdr.aout.a_text = 0;
        break;
    default:
        printf("Bad a.out magic = %0o\n", N_GETMAGIC(epp->hdr.aout));
        return ENOEXEC;
    }

    /*
     * Save arglist
     */
    if ((error = exec_save_args(epp)) != 0)
        return error;

    DEBUG("\texec_aout_check(): exec file header\n");
    /* magic number */
    DEBUG("\texec_aout_check(): a_midmag  = %#x\n", epp->hdr.aout.a_midmag);
    /* size of text segment */
    DEBUG("\texec_aout_check(): a_text    = %d\n",  epp->hdr.aout.a_text);
    /* size of initialized data */
    DEBUG("\texec_aout_check(): a_data    = %d\n",  epp->hdr.aout.a_data);
    /* size of uninitialized data */
    DEBUG("\texec_aout_check(): a_bss     = %d\n",  epp->hdr.aout.a_bss);
    /* size of text relocation info */
    DEBUG("\texec_aout_check(): a_reltext = %d\n",  epp->hdr.aout.a_reltext);
    /* size of data relocation info */
    DEBUG("\texec_aout_check(): a_reldata = %d\n",  epp->hdr.aout.a_reldata);
    /* size of symbol table */
    DEBUG("\texec_aout_check(): a_syms    = %d\n",  epp->hdr.aout.a_syms);
    /* entry point */
    DEBUG("\texec_aout_check(): a_entry   = %#x\n", epp->hdr.aout.a_entry);

    /*
     * Set up memory allocation
     */
    epp->text.vaddr = epp->heap.vaddr = NO_ADDR;
    epp->text.len = epp->heap.len = 0;

    epp->data.vaddr = (caddr_t)__user_data_start;
    epp->data.len = epp->hdr.aout.a_data;
    epp->bss.vaddr = epp->data.vaddr + epp->data.len;
    epp->bss.len = epp->hdr.aout.a_bss;
    epp->heap.vaddr = epp->bss.vaddr + epp->bss.len;
    epp->heap.len = 0;
    epp->stack.len = SSIZE + roundup(epp->argbc + epp->envbc, NBPW) + (epp->argc + epp->envc+4)*NBPW;
    epp->stack.vaddr = (caddr_t)USER_TOP (u.u_procp) - epp->stack.len;

    /*
     * Nobody writes the file from here on. Then establish memory: the
     * overflow and layout checks run before core is allocated, so a
     * rejected image (one whose text, data, bss, heap and stack exceed
     * MAXMEM) returns here with the old image intact and execve reports
     * the error, matching exec_elf. Only past this point is the process
     * committed to the new image.
     */
    if ((error = exec_text_hold(epp->ip)) != 0)
        return error;
    if ((error = exec_estab(epp)) != 0) {
        exec_text_unhold(epp->ip);
        return error;
    }

    /*
     * The text at the start of the image is never written, so swapin
     * reads it back from the executable instead of swapout writing it
     * to swap; keep the inode for that, one reference per process. It is
     * taken before the read so a failure there leaves the exclusion with
     * an owner for exit to release.
     */
    p = u.u_procp;
    exec_text_release(p, epp->ip);
    epp->ip->i_count++;
    p->p_tip = epp->ip;
    p->p_tsize = tsize;

    /* read in text and data */
    DEBUG("\texec_aout_check(): reading a.out image\n");
    error = rdwri (UIO_READ, epp->ip,
               (caddr_t)epp->data.vaddr, epp->hdr.aout.a_data,
               sizeof(struct exec) + epp->hdr.aout.a_text, IO_UNIT, 0);
    if (error) {
        DEBUG("\texec_aout_check(): error: read image returned: %d\n", error);
        /*
         * All is lost: the old image is overwritten and the new one is
         * incomplete. Its signal handlers still stand until exec_clear,
         * so a catchable signal would run whatever now lies at the old
         * handler's address; SIGKILL runs nothing.
         */
        psignal (u.u_procp, SIGKILL);
        return error;
    }

    exec_clear(epp);
    exec_setupstack(epp->hdr.aout.a_entry, epp);

    DEBUG("\texec_aout_check(): end\n");

    return 0;
}
