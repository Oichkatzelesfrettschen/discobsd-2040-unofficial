#!/usr/bin/env python3
"""Generate calibrated directory-removal write-error mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"{mutation}: expected one source match, found {count}"
        )
    return source.replace(old, new)


def mutate(source: str, mutation: str) -> str:
    if mutation == "discard-first-write-error":
        return replace_once(
            source,
            """        error = rdwri (UIO_WRITE, dp, (caddr_t) &ndp->ni_dent,
                (int) DIRSIZ(&ndp->ni_dent), ndp->ni_offset,
                IO_UNIT | IO_SYNC, (int*) 0);
""",
            """        (void) rdwri (UIO_WRITE, dp, (caddr_t) &ndp->ni_dent,
                (int) DIRSIZ(&ndp->ni_dent), ndp->ni_offset,
                IO_UNIT | IO_SYNC, (int*) 0);
        error = 0;
""",
            mutation,
        )
    if mutation == "discard-coalesced-write-error":
        return replace_once(
            source,
            """        ep->d_reclen += ndp->ni_dent.d_reclen;
        error = bwrite(bp);
""",
            """        ep->d_reclen += ndp->ni_dent.d_reclen;
        (void) bwrite(bp);
        error = 0;
""",
            mutation,
        )
    if mutation == "discard-enter-write-error":
        return replace_once(
            source,
            """    error = bwrite(bp);
    if (error == 0) {
""",
            """    (void) bwrite(bp);
    error = 0;
    if (error == 0) {
""",
            mutation,
        )
    raise SystemExit(f"unknown mutation: {mutation}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    arguments.output.write_text(
        mutate(source, arguments.mutation), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
