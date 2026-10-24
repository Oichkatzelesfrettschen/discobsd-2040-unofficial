#!/usr/bin/env python3
"""Generate calibrated rooted conditional-publication mutations."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from publishat_beneath_extract import FUNCTIONS
from unlinkat_beneath_extract import INCLUDES, extract_function


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"{mutation}: expected one source match, found {count}"
        )
    return source.replace(old, new)


def mutate_function(function: str, mutation: str) -> str:
    if mutation == "discard-reserve-write-error":
        return replace_once(
            function,
            """    error = iupdat(staged_inode, &time, &time, 1);
    if (error != 0) {
        staged_inode->i_nlink--;
        staged_inode->i_flag |= ICHG;
        u.u_error = error;
        goto release_staged_lookup;
    }
""",
            """    (void)iupdat(staged_inode, &time, &time, 1);
""",
            mutation,
        )
    if mutation == "skip-staged-identity":
        return replace_once(
            function,
            """    if (staged_inode != expected_staged) {
        u.u_error = ESTALE;
        goto release_staged_lookup;
    }
""",
            "",
            mutation,
        )
    if mutation == "skip-staged-sticky":
        return replace_once(
            function,
            """    if ((staged_parent->i_mode & ISVTX) && u.u_uid != 0 &&
        u.u_uid != staged_parent->i_uid &&
        staged_inode->i_uid != u.u_uid) {
        u.u_error = EPERM;
        goto release_staged_lookup;
    }
""",
            "",
            mutation,
        )
    if mutation == "skip-target-identity":
        return replace_once(
            function,
            """    } else if (target_inode != expected_target) {
        return ESTALE;
""",
            "",
            mutation,
        )
    if mutation == "use-at-fdcwd":
        count = function.count("uap->rootfd")
        if count != 4:
            raise SystemExit(
                f"{mutation}: expected four rooted lookups, found {count}"
            )
        return function.replace("uap->rootfd", "AT_FDCWD")
    if mutation == "release-before-publication":
        return replace_once(
            function,
            """    if (target_inode == NULL) {
        error = direnter(expected_staged, target_ndp);
""",
            """    ufs_namespace_exit();
    if (target_inode == NULL) {
        error = direnter(expected_staged, target_ndp);
""",
            mutation,
        )
    if mutation == "skip-same-parent":
        return replace_once(
            function,
            """    if (target_parent != staged_parent ||
        INODE_DEVICE(target_parent) != INODE_DEVICE(expected_staged))
        return EXDEV;
""",
            """    (void)staged_parent;
    if (INODE_DEVICE(target_parent) !=
        INODE_DEVICE(expected_staged))
        return EXDEV;
""",
            mutation,
        )
    if mutation == "publish-after-remove-error":
        return replace_once(
            function,
            """    if (!dirremove(staged_ndp))
        goto release_removal_lookup;
""",
            """    if (!dirremove(staged_ndp))
        u.u_error = 0;
""",
            mutation,
        )
    raise SystemExit(f"unknown mutation: {mutation}")


def mutate(source: str, mutation: str) -> str:
    functions = [
        extract_function(source, signature, name)
        for name, signature in FUNCTIONS
    ]
    return mutate_function("\n\n".join(functions), mutation)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    mutated = mutate(source_bytes.decode("utf-8"), arguments.mutation)
    arguments.output.write_text(
        "/* Mutated from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + INCLUDES
        + "#include <sys/fcntl.h>\n\n"
        + mutated
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
