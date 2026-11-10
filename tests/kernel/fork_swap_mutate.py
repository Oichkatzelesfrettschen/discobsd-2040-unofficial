"""Generate one ownership defect in an extracted production definition."""

import argparse
from pathlib import Path

MUTATIONS = {
    "count-free-not-largest-run": (
        "        span = malloc3_contiguous_next (swapmap, sr_flash_span (e), 0, 0,\n"
        "            SWAP_IMAGE_ALIGN, &next, addresses);",
        "        {\n"
        "        struct mapent *entry;\n\n"
        "        span = 0;\n"
        "        for (entry = swapmap->m_map; entry->m_size != 0; entry++)\n"
        "            span += entry->m_size;\n"
        "        if (span >= sr_flash_span (e)) {\n"
        "            span = sr_flash_span (e);\n"
        "            addresses[0] = SWAP_IMAGE_ALIGN;\n"
        "        } else\n"
        "            span = 0;\n"
        "    }",
    ),
    "release-headroom-early": (
        "        swap_exchange.owner = p;\n"
        "        swap_exchange.base = p->p_daddr;\n"
        "        swap_exchange.span = swap_image_span (p);",
        "        mfree (swapmap, swap_image_span (p), p->p_daddr);",
    ),
    "pool-swap-in": (
        "        if (! swapram_route_take (incoming, &incoming_base,\n"
        "            &incoming_span)) {\n"
        "            splx (spl);\n"
        "            return ENOMEM;\n"
        "        }",
        "        incoming_base = SWAP_IMAGE_ALIGN;\n"
        "        incoming_span = swap_image_span (incoming);",
    ),
    "scheduler-pool-bypass": (
        "        if (swapram_reserve_route (outgoing,",
        "        if (0 && swapram_reserve_route (outgoing,",
    ),
    "fork-consumer-skip-rule": (
        "    swap_image_reservation_init (reservation, owner, source, odata, ostack);\n"
        "#ifdef SWAPRAM",
        "    swap_image_reservation_init (reservation, owner, source, odata, ostack);\n"
        "#ifdef SWAP_IMAGE_ALIGN\n"
        "    if (swap_exchange.owner == source)\n"
        "        mfree (swapmap, swap_exchange.span, swap_exchange.base);\n"
        "#endif\n"
        "#ifdef SWAPRAM",
    ),
    "ignore-reservation-refusal": ("        return -1;", "        return 0;"),
    "publish-allproc-early": (
        "    if (swap_image_reserve (&reservation, child, parent,",
        "    allproc = child;\n    if (swap_image_reserve (&reservation, child, parent,",
    ),
    "advance-cursor-on-failure": (
        "            SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0)\n            return ENOMEM;",
        "            SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0) {\n"
        "            swapnext += SWAP_IMAGE_ALIGN;\n            return ENOMEM;\n        }",
    ),
    # swapout() given a reservation must copy into it; reserving again takes a
    # second extent, which an exactly full map cannot supply.
    "allocate-again-on-copy": (
        "    if (reservation == NULL || reservation->state != SWAP_IMAGE_RESERVED)\n"
        '        panic ("swapout: no reservation");',
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
        "            in_core = 0;\n            swapped_out = 0;\n            splhigh ();\n",
        "            in_core = 0;\n            splhigh ();\n",
    ),
    # The reservation-free swapout of the scheduler before the refusal path.
    "sched-unreserved-swapout": (
        "            swapout (in_core, X_FREECORE, X_OLDSIZE, X_OLDSIZE,\n"
        "                &reservation);",
        "            swapout (in_core, X_FREECORE, X_OLDSIZE, X_OLDSIZE, NULL);",
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
