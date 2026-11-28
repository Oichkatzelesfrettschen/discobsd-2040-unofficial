"""Generate one ownership defect in an extracted production definition."""

import argparse
from pathlib import Path

MUTATIONS = {
    "quota-off-by-one": ("a >= CHILD_PROC_LIMIT", "a > CHILD_PROC_LIMIT"),
    "quota-zombie-effective": (
        "for (p1 = zombproc; p1; p1 = p1->p_nxt)\n"
        "            if (p1->p_ruid == u.u_ruid)",
        "for (p1 = zombproc; p1; p1 = p1->p_nxt)\n"
        "            if (p1->p_uid == u.u_uid)",
    ),
    "ignore-reservation-refusal": (
        "        X_OLDSIZE, X_OLDSIZE) != 0)\n        return -1;",
        "        X_OLDSIZE, X_OLDSIZE) != 0)\n        return 0;",
    ),
    "publish-allproc-early": (
        "    if (swap_image_reserve (&reservation, child, parent,",
        "    allproc = child;\n    if (swap_image_reserve (&reservation, child, parent,",
    ),
    "advance-cursor-on-failure": (
        "        SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0)\n        return ENOMEM;",
        "        SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0) {\n"
        "        swapnext += SWAP_IMAGE_ALIGN;\n        return ENOMEM;\n    }",
    ),
    # swapout() given a reservation must copy into it; reserving again takes a
    # second extent, which an exactly full map cannot supply.
    "allocate-again-on-copy": (
        "    if (reservation == NULL || reservation->state != SWAP_IMAGE_RESERVED)\n"
        '        panic ("K66");',
        "    struct swap_image_reservation again;\n"
        "\n"
        "    if (swap_image_reserve (&again, p, p, odata, ostack) != 0)\n"
        '        panic ("out of swap space");\n'
        "    reservation = &again;",
    ),
    "accept-unreserved-state": (
        "    if (reservation == NULL || reservation->state != SWAP_IMAGE_RESERVED)",
        "    if (reservation == NULL)",
    ),
    "child-sidl-during-copy": (
        "    child->p_stat = SRUN;\n"
        "    swapout (child, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);",
        "    swapout (child, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);\n"
        "    child->p_stat = SRUN;",
    ),
    "omit-release-wakeup": (
        "    wakeup ((caddr_t) &runout);\n"
        "    wakeup ((caddr_t) &runin);\n}\n",
        "    (void)runout;\n    (void)runin;\n}\n",
    ),
    "notify-before-release": (
        "swap_image_release (struct proc *p)\n{",
        "swap_image_release (struct proc *p)\n{\n    wakeup ((caddr_t) &runout);",
    ),
    "ignore-copy-owner": (
        "    if (reservation->owner != p ||\n        reservation->state != SWAP_IMAGE_RESERVED)",
        "    if (reservation->state != SWAP_IMAGE_RESERVED)",
    ),
    "ignore-copy-state": (
        "    if (reservation->owner != p ||\n        reservation->state != SWAP_IMAGE_RESERVED)",
        "    if (reservation->owner != p)",
    ),
    "ignore-commit-owner": (
        "    if (reservation->owner != p ||\n        reservation->state != SWAP_IMAGE_COPIED)",
        "    if (reservation->state != SWAP_IMAGE_COPIED)",
    ),
    "ignore-commit-state": (
        "    if (reservation->owner != p ||\n        reservation->state != SWAP_IMAGE_COPIED)",
        "    if (reservation->owner != p)",
    ),
    "ignore-cancel-state": (
        "    if (reservation->state != SWAP_IMAGE_RESERVED)\n"
        '        panic ("K57");',
        '    if (0)\n        panic ("K57");',
    ),
    "omit-text-release": ("    iput(ip);", "    (void)ip;"),
    # The probe labels a pool refusal by total free bytes against the
    # grain-rounded demand and bins the shortfall in KiB steps.
    "swap-pool-reasons": (
        "        need > avail ? STORAGE_SWAPRAM_REFUSED_NOSPACE :\n"
        "        STORAGE_SWAPRAM_REFUSED_FRAGMENT, 0);",
        "        need > avail ? STORAGE_SWAPRAM_REFUSED_FRAGMENT :\n"
        "        STORAGE_SWAPRAM_REFUSED_NOSPACE, 0);",
    ),
    "misbin-shortfall-boundary": (
        "(shortfall - 1) >> 10 : 0;",
        "shortfall >> 10 : 0;",
    ),
    # The exchange completion count belongs after the sector loop: counted
    # at entry, a swap-out that stops at its first media call looks finished.
    "early-exchange-count": (
        "    storage_note (STORAGE_SWAPRAM_ATTEMPTS, 1);\n#endif\n    bp = geteblk ();",
        "    storage_note (STORAGE_SWAPRAM_ATTEMPTS, 1);\n"
        "    storage_note (STORAGE_SWAPRAM_EXCHANGE_SWAPOUTS, 1);\n#endif\n"
        "    bp = geteblk ();",
    ),
    # swapin() restoring a RAM-tier image frees pool capacity and must wake
    # the swapper after the pool holds it.
    "omit-swapin-ram-wakeup": (
        "            (caddr_t) uaddr);\n        wakeup ((caddr_t) &runout);\n",
        "            (caddr_t) uaddr);\n",
    ),
    "notify-before-swapin-restore": (
        "        swapram_in (p, (caddr_t) (daddr + tsize), (caddr_t) saddr,\n"
        "            (caddr_t) uaddr);\n"
        "        wakeup ((caddr_t) &runout);\n",
        "        wakeup ((caddr_t) &runout);\n"
        "        swapram_in (p, (caddr_t) (daddr + tsize), (caddr_t) saddr,\n"
        "            (caddr_t) uaddr);\n",
    ),
    # newproc() and fork1() result contracts: parent, resumed child, refusal.
    "child-result-zero": (
        "    if (setjmp (&u.u_ssave)) {\n        return(1);",
        "    if (setjmp (&u.u_ssave)) {\n        return(0);",
    ),
    "parent-result-one": (
        "        parent->p_flag &= ~SVFPRNT;\n    }\n    return(0);",
        "        parent->p_flag &= ~SVFPRNT;\n    }\n    return(1);",
    ),
    "fork1-child-rval-parent": (
        "        /* Child */\n        u.u_rval = 0;",
        "        /* Child */\n        u.u_rval = p2->p_pid;",
    ),
    "refusal-result-child": (
        "        X_OLDSIZE, X_OLDSIZE) != 0)\n        return -1;",
        "        X_OLDSIZE, X_OLDSIZE) != 0)\n        return 1;",
    ),
    # Process 1 reserves a u-area-only image: empty segments write zero bytes.
    "write-empty-data-segment": (
        "    if (odata) {\n#ifdef SWAPRAM",
        "    if (1) {\n#ifdef SWAPRAM",
    ),
    "write-empty-stack-segment": (
        "    if (ostack) {\n#ifdef SWAPRAM",
        "    if (1) {\n#ifdef SWAPRAM",
    ),
    # Three-extent arms of a port without SWAP_IMAGE_ALIGN: one segment free
    # dropped from release or from cancellation leaks that extent.
    "omit-release-segment-free": (
        "        mfree (swapmap, btod (USIZE), p->p_addr);",
        "        (void)p->p_addr;",
    ),
    "omit-cancel-segment-free": (
        "        mfree (swapmap, reservation->stack_blocks, reservation->addr[1]);",
        "        (void)reservation->addr[1];",
    ),
    # sched() refusal path: the selected in-core process stays resident.
    "sched-drop-sload-restore": (
        "            in_core->p_flag |= SLOAD;\n",
        "",
    ),
    "sched-drop-requeue": (
        "                if (in_core->p_stat == SRUN)\n                    setrq (in_core);\n",
        "",
    ),
    "sched-swapin-after-refusal": (
        "            in_core = 0;\n            swapped_out = 0;\n            continue;\n",
        "            in_core = 0;\n            continue;\n",
    ),
    # A refused candidate is passed over until sched() sleeps: sleeping
    # straight after the refusal, or selecting the refused candidate again,
    # leaves a candidate whose exchange fits untried, and marks that outlive
    # the sleep leave the refused candidate out after capacity returns.
    "sched-sleep-after-refusal": (
        "            in_core = 0;\n            swapped_out = 0;\n            continue;\n",
        "            in_core = 0;\n            swapped_out = 0;\n            splhigh ();\n"
        "            ++runin;\n            sleep ((caddr_t) &runin, PSWP);\n"
        "            continue;\n",
    ),
    "sched-reselect-refused": (
        "            if (refusals && refused[rp - proc])\n                continue;\n",
        "",
    ),
    "sched-keep-refused-marks": (
        "            if (refusals) {\n                refusals = 0;\n",
        "            if (refusals) {\n",
    ),
    # The reservation-free swapout of the scheduler before the refusal path.
    "sched-unreserved-swapout": (
        "                swapout (in_core, X_FREECORE, X_OLDSIZE, X_OLDSIZE,\n"
        "                    &reservation);",
        "                swapout (in_core, X_FREECORE, X_OLDSIZE, X_OLDSIZE,\n"
        "                    NULL);",
    ),
    # A swap device that leaves (swap_lost): the fork unwind, the resident
    # that stays, the processes retired and the ones spared.
    "lost-fork-no-unwind": (
        "    u.u_procp = parent;\n#ifdef SDSWAP_ENABLED\n"
        "    if (reservation.state != SWAP_IMAGE_COMMITTED) {",
        "    u.u_procp = parent;\n#ifdef SDSWAP_ENABLED\n    if (0) {",
    ),
    "lost-fork-keep-files": (
        "            if (fp != NULL)\n                fp->f_count--;",
        "            (void)fp;",
    ),
    "lost-fork-keep-directory": ("        u.u_cdir->i_count--;", "        (void)0;"),
    "lost-fork-keep-text": (
        "        if (child->p_tip)\n            child->p_tip->i_count--;",
        "        (void)0;",
    ),
    "lost-fork-keep-pidhash": (
        "        *hash = child->p_hash;\n        }",
        "        (void)hash;\n        }",
    ),
    "lost-swapin-loads": (
        "    if (swap_lost)\n        return;\n    swap_image_release (p);",
        "    swap_image_release (p);",
    ),
    "lost-copy-commits": (
        "        p->p_flag &= ~SLOCK;\n"
        "        swap_image_cancel (reservation);\n"
        "        return;",
        "        (void)0;",
    ),
    "lost-copy-keeps-lock": (
        "        p->p_flag &= ~SLOCK;\n        swap_image_cancel (reservation);",
        "        swap_image_cancel (reservation);",
    ),
    "lost-sched-swapin-over-resident": (
        "                if (reservation.state != SWAP_IMAGE_COMMITTED) {\n"
        "                    swapped_out = 0;\n"
        "                    continue;\n"
        "                }",
        "",
    ),
    "lost-sched-no-sweep": ("            swap_lost_sweep ();\n", ""),
    "lost-sched-drop-resident": (
        "        if (exchange_refusal || swap_lost == SWAP_LOST_NEW) {",
        "        if (exchange_refusal) {",
    ),
    "lost-sweep-kills-pool": (
        "        if (swapram_present (p))\n"
        "            continue;\n"
        "#endif\n"
        "        swap_lost_retire (p);",
        "#endif\n        swap_lost_retire (p);",
    ),
    "lost-sweep-kills-system": (
        "        if (p->p_flag & (SLOAD | SSYS))",
        "        if (p->p_flag & SLOAD)",
    ),
    "lost-reserve-after-loss": ("    if (swap_lost)\n        return ENOMEM;\n", ""),
    "lost-exchange-on-removable": (
        "    if (swap_removable || ! swap_exchange_tail_reserve (incoming->p_daddr,",
        "    if (! swap_exchange_tail_reserve (incoming->p_daddr,",
    ),
    "lost-init-retired": (
        '    if (p->p_pid == 1)\n        panic ("init lost with the swap device");',
        "",
    ),
    # A vfork parent whose lent image went exits in newproc(), never
    # behind a signal: returning instead, missing the nested case, or
    # leaving the exiting child waiting for SVFDONE must fail.
    "lost-vfork-parent-spared": (
        "            parent->p_flag &= ~SVFPRNT;\n            exit (SIGKILL);",
        "            parent->p_flag &= ~SVFPRNT;\n            return(0);",
    ),
    "lost-vfork-takes-zombie-sizes": (
        "        if (child->p_stat == SZOMB || child->p_ssize == 0) {",
        "        if (0) {",
    ),
    "lost-vfork-nested-spared": (
        "        if (child->p_stat == SZOMB || child->p_ssize == 0) {",
        "        if (child->p_stat == SZOMB) {",
    ),
    "lost-vfork-zombie-after-fallback": (
        "        if (child->p_stat == SZOMB || child->p_ssize == 0) {",
        "        if (swap_lost && child->p_stat == SZOMB) {",
    ),
    "lost-vfork-child-unreleased": (
        "            child->p_flag |= SVFDONE;\n            wakeup ((caddr_t) parent);\n"
        "            parent->p_flag &= ~SVFPRNT;\n            exit (SIGKILL);",
        "            parent->p_flag &= ~SVFPRNT;\n            exit (SIGKILL);",
    ),
    # A retired vfork parent releases the child that holds its image.
    "lost-vfork-parent-child-held": (
        "                q->p_flag = (q->p_flag & ~SVFORK) | SVFDONE;",
        "                (void)q;",
    ),
    "lost-vfork-parent-child-unwoken": (
        "                q->p_flag = (q->p_flag & ~SVFORK) | SVFDONE;\n"
        "        wakeup ((caddr_t) p);",
        "                q->p_flag = (q->p_flag & ~SVFORK) | SVFDONE;",
    ),
    "lost-vfork-child-kept-waiting": (
        "        p->p_flag &= ~SVFORK;\n        wakeup ((caddr_t) p);",
        "        wakeup ((caddr_t) p);",
    ),
    "lost-sleeper-queued": ("    unsleep (p);", "    (void)0;"),
    # The zombie conversion and reparenting are exit()'s, in proc_zombie()
    # (kern_exit.c), which swap_lost_retire() calls.
    "lost-children-kept": ("            q->p_ppid = 1;", "            (void)0;"),
    "lost-parent-untold": (
        "    psignal(p->p_pptr, SIGCHLD);\n    wakeup((caddr_t) p->p_pptr);",
        "    wakeup((caddr_t) p->p_pptr);",
    ),
    "lost-status-clean": ("    proc_zombie (p, SIGKILL);", "    proc_zombie (p, 0);"),
    "lost-zombie-hashed": (
        "            *pp = p->p_hash;\n            goto done;",
        "            goto done;",
    ),
    "lost-timeout-kept": ("    untimeout (endtsleep, (caddr_t) p);", "    (void)0;"),
    "lost-keyevs-kept": ("    lcdcons_procexit (p);", "    (void)p;"),
    "lost-traced-child-kept": (
        "            if (q->p_flag& P_TRACED) {\n"
        "                q->p_flag &= ~P_TRACED;\n"
        "                psignal(q, SIGKILL);\n"
        "            } else if",
        "            if (0) {\n            } else if",
    ),
    "lost-stopped-child-kept": (
        "                psignal(q, SIGHUP);\n                psignal(q, SIGCONT);",
        "                (void)0;",
    ),
    # After the sweep swap returns to the fixed device once the lost one's
    # map is empty: sched() must ask, a retired image must give its blocks
    # back, and a block still held must keep the lost device's map in place.
    "lost-no-fallback": (
        "        if (swap_lost == SWAP_LOST_SWEPT)\n            swap_lost_fallback ();\n",
        "",
    ),
    "lost-retire-keeps-blocks": (
        "    mfree (swapmap, swap_image_span (p), p->p_daddr);\n    /*",
        "    /*",
    ),
    "lost-fallback-ignores-holder": (
        "    if (! swap_map_empty ())\n        return;\n    blocks",
        "    blocks",
    ),
}


def apply(source: str, name: str) -> str:
    """Return source with every replacement of one mutation applied once."""
    replacements = MUTATIONS[name]
    if isinstance(replacements[0], str):
        replacements = (replacements,)
    for before, after in replacements:
        if source.count(before) != 1:
            raise ValueError("mutation anchor must occur exactly once: " + name)
        source = source.replace(before, after, 1)
    return source


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    args = parser.parse_args()
    args.output.write_text(apply(args.source.read_text(), args.mutation))


if __name__ == "__main__":
    main()
