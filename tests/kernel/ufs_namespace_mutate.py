#!/usr/bin/env python3
"""Generate calibrated UFS namespace-gate mutations."""

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
    if mutation == "omit-acquisition":
        return replace_once(
            source,
            "    ufs_namespace_busy = 1;\n",
            "    (void)ufs_namespace_busy;\n",
            mutation,
        )
    if mutation == "omit-wakeup":
        return replace_once(
            source,
            """    if (ufs_namespace_wanted) {
        ufs_namespace_wanted = 0;
        wakeup((caddr_t)&ufs_namespace_busy);
    }
""",
            """    if (ufs_namespace_wanted)
        ufs_namespace_wanted = 0;
""",
            mutation,
        )
    if mutation == "clear-on-interrupt":
        return replace_once(
            source,
            """        if (error != 0)
            return error;
""",
            """        if (error != 0) {
            ufs_namespace_busy = 0;
            return error;
        }
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
