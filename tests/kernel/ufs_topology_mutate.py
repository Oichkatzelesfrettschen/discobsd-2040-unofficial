#!/usr/bin/env python3
"""Generate calibrated UFS topology mutations from production definitions."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from ufs_topology_extract import FUNCTIONS, INCLUDES, extract_function


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"{mutation}: expected one source match, found {count}"
        )
    return source.replace(old, new)


def mutate(source: str, mutation: str) -> str:
    if mutation == "omit-rename-acquisition":
        return replace_once(
            source,
            """    error = ufs_namespace_enter();
    if (error != 0) {
        u.u_error = error;
        return;
    }
""",
            "",
            mutation,
        )
    if mutation == "release-before-source-lookup":
        return replace_once(
            source,
            """    /*
     * 3) Unlink the source.
     */
    NAMEI_INIT_USER (ndp, DELETE, LOCKPARENT, uap->from);
""",
            """    /*
     * 3) Unlink the source.
     */
    ufs_namespace_exit();
    NAMEI_INIT_USER (ndp, DELETE, LOCKPARENT, uap->from);
""",
            mutation,
        )
    if mutation == "release-before-directory-checkpath":
        return replace_once(
            source,
            """    if (doingdirectory && newparent) {
        if (access(ip, IWRITE))
""",
            """    if (doingdirectory && newparent) {
        ufs_namespace_exit();
        if (access(ip, IWRITE))
""",
            mutation,
        )
    if mutation == "allow-directory-link":
        start = source.index("void\nlink(void)")
        end = source.index("void\nsymlink(void)", start)
        function = replace_once(
            source[start:end],
            "    if ((ip->i_mode&IFMT) == IFDIR) {\n",
            "    if ((ip->i_mode&IFMT) == IFDIR && !suser()) {\n",
            mutation,
        )
        return source[:start] + function + source[end:]
    if mutation == "allow-directory-unlink":
        start = source.index("void\nunlink(void)")
        end = source.index("void\nsaccess(void)", start)
        function = replace_once(
            source[start:end],
            "    if ((ip->i_mode&IFMT) == IFDIR) {\n",
            "    if ((ip->i_mode&IFMT) == IFDIR && !suser()) {\n",
            mutation,
        )
        return source[:start] + function + source[end:]
    raise SystemExit(f"unknown mutation: {mutation}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    mutated = mutate(source_bytes.decode("utf-8"), arguments.mutation)
    functions = [
        extract_function(mutated, signature, name)
        for name, signature in FUNCTIONS
    ]
    arguments.output.write_text(
        "/* Mutated from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + INCLUDES
        + "\n"
        + "\n\n".join(functions)
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
