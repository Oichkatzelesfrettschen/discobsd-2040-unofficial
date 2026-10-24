#!/usr/bin/env python3
"""Generate calibrated descriptor-conditioned rooted hard-link mutations."""

from __future__ import annotations

import argparse
from pathlib import Path

from linkat_beneath_extract import FUNCTIONS
from unlinkat_beneath_extract import INCLUDES, extract_function


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{mutation}: expected one source match, found {count}")
    return source.replace(old, new, 1)


def mutate(function: str, mutation: str) -> str:
    if mutation == "skip-source-identity":
        return replace_once(
            function,
            """    if (source != expected) {
        u.u_error = ESTALE;
        goto release_source;
    }
""",
            "",
            mutation,
        )
    if mutation == "skip-link-count-limit":
        return replace_once(
            function,
            """    if (source->i_nlink == USHRT_MAX) {
        u.u_error = EMLINK;
        goto release_source;
    }
""",
            "",
            mutation,
        )
    if mutation == "use-at-fdcwd":
        return function.replace("uap->rootfd", "AT_FDCWD")
    if mutation == "follow-source-symlink":
        return replace_once(
            function,
            "LOOKUP, NOFOLLOW | NOCACHE,",
            "LOOKUP, FOLLOW | NOCACHE,",
            mutation,
        )
    if mutation == "release-before-publication":
        return replace_once(
            function,
            """    error = direnter(expected, target_ndp);
""",
            """    ufs_namespace_exit();
    error = direnter(expected, target_ndp);
""",
            mutation,
        )
    if mutation == "discard-reserve-write-error":
        return replace_once(
            function,
            """    if (error != 0) {
        source->i_nlink--;
        source->i_flag |= ICHG;
        u.u_error = error;
        goto release_source;
    }
""",
            """    (void)error;
""",
            mutation,
        )
    if mutation == "skip-rollback-decrement":
        return replace_once(
            function,
            """        expected->i_nlink--;
        expected->i_flag |= ICHG;
""",
            """        expected->i_flag |= ICHG;
""",
            mutation,
        )
    if mutation == "skip-target-exists":
        return replace_once(
            function,
            """    if (target != NULL) {
        u.u_error = EEXIST;
        goto release_target;
    }
""",
            "",
            mutation,
        )
    if mutation == "reject-append-parent":
        return replace_once(
            function,
            """    error = direnter(expected, target_ndp);
""",
            """    if (parent->i_flags & APPEND) {
        u.u_error = EPERM;
        goto release_target;
    }
    error = direnter(expected, target_ndp);
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
    extracted = []
    for name, signature in FUNCTIONS:
        function = extract_function(source, signature, name)
        if name == "linkat_beneath":
            function = mutate(function, arguments.mutation)
        extracted.append(function)
    arguments.output.write_text(
        INCLUDES + "\n" + "\n\n".join(extracted) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
