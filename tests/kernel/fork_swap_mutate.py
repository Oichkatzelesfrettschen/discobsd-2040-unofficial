"""Generate one ownership defect in an extracted production definition."""

import argparse
from pathlib import Path

MUTATIONS = {
    "ignore-reservation-refusal": ("        return -1;", "        return 0;"),
    "publish-allproc-early": (
        "    if (swap_image_reserve (&reservation, child, parent,",
        "    allproc = child;\n    if (swap_image_reserve (&reservation, child, parent,",
    ),
    "advance-cursor-on-failure": (
        "            &next, reservation->addr) == 0)\n            return ENOMEM;",
        "            &next, reservation->addr) == 0) {\n"
        "            swapnext += SWAP_IMAGE_ALIGN;\n            return ENOMEM;\n        }",
    ),
    "allocate-again-on-copy": (
        "    if (reservation == NULL) {",
        "    if (1) {",
    ),
    "child-sidl-during-copy": (
        "    child->p_stat = SRUN;\n"
        "    swapout (child, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);",
        "    swapout (child, X_DONTFREE, X_OLDSIZE, X_OLDSIZE, &reservation);\n"
        "    child->p_stat = SRUN;",
    ),
    "omit-release-wakeup": (
        "    wakeup ((caddr_t) &runout);\n}\n\n/*\n * Swap a process in.",
        "    (void)runout;\n}\n\n/*\n * Swap a process in.",
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
        '        panic ("swap image: cancel state");',
        '    if (0)\n        panic ("swap image: cancel state");',
    ),
    "omit-text-release": ("    iput(ip);", "    (void)ip;"),
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
    "refusal-result-child": ("        return -1;", "        return 1;"),
    # Process 1 reserves a u-area-only image: empty segments write nothing.
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
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    before, after = MUTATIONS[args.mutation]
    if source.count(before) != 1:
        raise ValueError("mutation anchor must occur exactly once: " + args.mutation)
    args.output.write_text(source.replace(before, after, 1))


if __name__ == "__main__":
    main()
