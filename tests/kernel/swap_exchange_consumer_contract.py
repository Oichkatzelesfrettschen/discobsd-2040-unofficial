"""Verify every flash consumer participates in exchange-headroom ownership."""

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Consumer:
    name: str
    path: str
    anchor: str


CONSUMERS = (
    Consumer(
        "fork",
        "sys/kern/kern_fork.c",
        "swap_image_reserve (&reservation, child, parent,",
    ),
    Consumer(
        "exec-spool",
        "sys/kern/exec_subr.c",
        "malloc3_contiguous_next (swapmap, blocks, 0, 0,",
    ),
    Consumer(
        "temporary-swap",
        "sys/dev/swap.c",
        "malloc3_contiguous_next(swapmap,\n\t\t\t    (size_t)requested, 0, 0,",
    ),
    Consumer(
        "swapram-evacuation",
        "sys/arch/rp2040/rp2040/swapram.c",
        "        base = e->e_route_base;",
    ),
    Consumer(
        "heap-growth",
        "sys/kern/kern_mman.c",
        "swap_exchange_resize (u.u_procp, newsize, u.u_procp->p_ssize,",
    ),
    Consumer(
        "syscall-stack-growth",
        "sys/arch/rp2040/rp2040/syscall.c",
        "swap_exchange_resize(u.u_procp, u.u_procp->p_dsize,",
    ),
    Consumer(
        "signal-stack-growth",
        "sys/arch/rp2040/rp2040/sig_machdep.c",
        "swap_exchange_resize(u.u_procp,",
    ),
    Consumer(
        "exec-geometry",
        "sys/kern/exec_subr.c",
        "swap_exchange_resize (u.u_procp,\n        epp->data.len + epp->bss.len,",
    ),
    Consumer(
        "process-one-geometry",
        "sys/kern/init_main.c",
        "swap_exchange_resize(p, icodeend - icode, 1024, 0)",
    ),
    Consumer(
        "exit-release",
        "sys/kern/kern_exit.c",
        "swap_exchange_release(p);",
    ),
)


def validate(sources: dict[str, str]) -> None:
    """Require every production consumer anchor exactly once."""
    for consumer in CONSUMERS:
        count = sources[consumer.path].count(consumer.anchor)
        if count != 1:
            raise ValueError(f"{consumer.name}: expected one ownership anchor, found {count}")


def load_sources(root: Path) -> dict[str, str]:
    """Read each distinct production source used by the contract."""
    return {consumer.path: (root / consumer.path).read_text() for consumer in CONSUMERS}


def self_test(sources: dict[str, str]) -> None:
    """Remove each consumer anchor and require validation to reject it."""
    for consumer in CONSUMERS:
        mutant = dict(sources)
        mutant[consumer.path] = mutant[consumer.path].replace(consumer.anchor, "", 1)
        try:
            validate(mutant)
        except ValueError as error:
            if not str(error).startswith(consumer.name + ":"):
                raise
        else:
            raise ValueError(f"{consumer.name}: removal mutant passed")
        print(f"PASS swap exchange {consumer.name} removal mutant rejected")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sources = load_sources(args.root)
    validate(sources)
    if args.self_test:
        self_test(sources)
    print(f"swap exchange consumer contract: {len(CONSUMERS)} consumers")


if __name__ == "__main__":
    main()
