#!/usr/bin/env python3
"""Generate calibrated directory-relative lookup and open mutations."""

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
    if mutation == "cwd-instead-of-dirfd":
        return replace_once(
            source,
            "        dp = (struct inode *)fp->f_data;\n",
            "        dp = u.u_cdir;\n",
            mutation,
        )
    if mutation == "absolute-consults-dirfd":
        return replace_once(
            source,
            "    cp = path;\n    if (*cp == '/') {\n",
            "    cp = path;\n    if (*cp == '/' && dirfd == AT_FDCWD) {\n",
            mutation,
        )
    if mutation == "accept-empty-beneath":
        return replace_once(
            source,
            """    if (*cursor == '\\0')
        return ENOENT;
""",
            """    if (*cursor == '\\0')
        return 0;
""",
            mutation,
        )
    if mutation == "follow-beneath-symlink":
        return replace_once(
            source,
            """    if ((target->i_mode & IFMT) == IFLNK)
        return ELOOP;
""",
            "",
            mutation,
        )
    if mutation == "cross-beneath-mount":
        return replace_once(
            source,
            """    if (INODE_DEVICE(target) != INODE_DEVICE(parent))
        return EXDEV;
""",
            "    (void)parent;\n",
            mutation,
        )
    if mutation == "follow-final-symlink":
        return replace_once(
            source,
            """    if ((fmode & O_NOFOLLOW) && (ip->i_mode & IFMT) == IFLNK) {
        error = ELOOP;
        goto bad;
    }
""",
            "",
            mutation,
        )
    if mutation == "hold-namespace-during-open-provider":
        return replace_once(
            source,
            """    if (namespace_owned != NULL && *namespace_owned) {
        ufs_namespace_exit();
        *namespace_owned = 0;
    }
""",
            "    (void)namespace_owned;\n",
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
