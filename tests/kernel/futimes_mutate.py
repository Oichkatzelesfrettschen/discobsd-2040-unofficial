#!/usr/bin/env python3
"""Generate calibrated futimes(2) contract mutations."""

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
    if mutation == "short-copy":
        return replace_once(
            source,
            "            sizeof(staged_times));",
            "            sizeof(staged_times[0]));",
            mutation,
        )
    if mutation == "descriptor-before-copy":
        return replace_once(
            source,
            """    u.u_error = prepare_utimes_attributes(uap->tptr, &vattr);
    if (u.u_error != 0)
        return;
    if ((ip = getinode(uap->fd)) == NULL)
        return;
""",
            """    if ((ip = getinode(uap->fd)) == NULL)
        return;
    u.u_error = prepare_utimes_attributes(uap->tptr, &vattr);
    if (u.u_error != 0)
        return;
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
