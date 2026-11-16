"""Verify the routeless-resident exchange connections in production source."""

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Contract:
    name: str
    path: str
    anchor: str


CONTRACTS = (
    Contract(
        "fork-child-reservation",
        "sys/kern/kern_fork.c",
        "swap_image_reserve (&reservation, child, parent,",
    ),
    Contract(
        "scheduler-prewrite-switch",
        "sys/kern/vm_sched.c",
        "swap_exchange_switch (&reservation, in_core, swapped_out,",
    ),
    Contract(
        "scheduler-inplace-exchange",
        "sys/kern/vm_sched.c",
        "swap_exchange_inplace (&reservation, in_core, swapped_out);",
    ),
    Contract(
        "exchange-tail-reservation",
        "sys/kern/vm_swap.c",
        "swap_exchange_tail_reserve (incoming->p_daddr,",
    ),
    Contract(
        "pool-incoming-prewrite-reservation",
        "sys/kern/vm_swap.c",
        "if (pool_backed)\n        return swap_image_reserve",
    ),
    Contract(
        "swapram-evacuation-reservation",
        "sys/arch/rp2040/rp2040/swapram.c",
        "malloc3_contiguous_next (swapmap, btod (e->e_rlen[SR_DATA]),",
    ),
    Contract(
        "exec-spool-independent-reservation",
        "sys/kern/exec_subr.c",
        "malloc3_contiguous_next (swapmap, blocks, 0, 0,",
    ),
    Contract(
        "temporary-swap-independent-reservation",
        "sys/dev/swap.c",
        "malloc3_contiguous_next(swapmap,",
    ),
    Contract(
        "flash-release-wakes-scheduler",
        "sys/kern/vm_swap.c",
        "wakeup ((caddr_t) &runout);\n    wakeup ((caddr_t) &runin);",
    ),
)

FORBIDDEN = (
    ("resident-route-state", "sys/sys/vmsystm.h", "struct swap_exchange_state"),
    ("resident-route-resize", "sys/sys/vmsystm.h", "swap_exchange_resize"),
    ("swapram-route-state", "sys/arch/rp2040/include/swapram.h", "swapram_route"),
)


def load_sources(root: Path) -> dict[str, str]:
    """Read every distinct source named by the contract."""
    paths = {contract.path for contract in CONTRACTS}
    paths.update(path for _, path, _ in FORBIDDEN)
    return {path: (root / path).read_text() for path in paths}


def validate(sources: dict[str, str]) -> None:
    """Require each connection once and each deleted route surface absent."""
    for contract in CONTRACTS:
        count = sources[contract.path].count(contract.anchor)
        if count != 1:
            raise ValueError(f"{contract.name}: expected one contract anchor, found {count}")
    for name, path, anchor in FORBIDDEN:
        count = sources[path].count(anchor)
        if count != 0:
            raise ValueError(f"{name}: expected deleted surface, found {count}")


def self_test(sources: dict[str, str]) -> None:
    """Require removal and route-reintroduction mutants to fail validation."""
    for contract in CONTRACTS:
        mutant = dict(sources)
        mutant[contract.path] = mutant[contract.path].replace(contract.anchor, "", 1)
        try:
            validate(mutant)
        except ValueError as error:
            if not str(error).startswith(contract.name + ":"):
                raise
        else:
            raise ValueError(f"{contract.name}: removal mutant passed")
        print(f"PASS {contract.name} removal mutant rejected")
    for name, path, anchor in FORBIDDEN:
        mutant = dict(sources)
        mutant[path] += "\n" + anchor + "\n"
        try:
            validate(mutant)
        except ValueError as error:
            if not str(error).startswith(name + ":"):
                raise
        else:
            raise ValueError(f"{name}: reintroduction mutant passed")
        print(f"PASS {name} reintroduction mutant rejected")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sources = load_sources(args.root)
    validate(sources)
    if args.self_test:
        self_test(sources)
    print(f"routeless exchange contract: {len(CONTRACTS)} connections")


if __name__ == "__main__":
    main()
