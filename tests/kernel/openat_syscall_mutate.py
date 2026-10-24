#!/usr/bin/env python3
"""Generate calibrated rooted-open and rooted-mkdir syscall mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{mutation}: expected one source match, found {count}")
    return source.replace(old, new, 1)


def mutate(source: str, mutation: str) -> str:
    if mutation == "omit-rooted-open-policy":
        return replace_once(
            source,
            "        NI_NOEMPTY | NI_BENEATH);\n}\n\n/*\n * Mknod system call",
            "        NI_NOEMPTY);\n}\n\n/*\n * Mknod system call",
            mutation,
        )
    if mutation == "release-before-open":
        return replace_once(
            source,
            "    error = vn_open_at(ndp, flags, cmode, dirfd, &namespace_owned);\n",
            """    if (namespace_owned) {
        ufs_namespace_exit();
        namespace_owned = 0;
    }
    error = vn_open_at(ndp, flags, cmode, dirfd, &namespace_owned);
""",
            mutation,
        )
    if mutation == "omit-rooted-open-release":
        return replace_once(
            source,
            """out:
    if (namespace_owned)
        ufs_namespace_exit();
    return(error);
}
""",
            """out:
    (void)namespace_owned;
    return(error);
}
""",
            mutation,
        )
    if mutation == "release-competing-open-owner":
        return replace_once(
            source,
            """        error = ufs_namespace_enter();
        if (error != 0)
            return error;
        namespace_owned = 1;
""",
            """        error = ufs_namespace_enter();
        namespace_owned = 1;
        if (error != 0)
            goto out;
""",
            mutation,
        )
    if mutation == "omit-rooted-mkdir-policy":
        return replace_once(
            source,
            """    mkdir_common(uap->rootfd, uap->path, uap->dmode,
        NI_NOEMPTY | NI_BENEATH);
""",
            """    mkdir_common(uap->rootfd, uap->path, uap->dmode,
        NI_NOEMPTY);
""",
            mutation,
        )
    if mutation == "release-before-mkdir-publication":
        return replace_once(
            source,
            """    dp->i_count++;
    u.u_error = direnter(ip, ndp);
""",
            """    if (namei_policy & NI_BENEATH) {
        ufs_namespace_exit();
        namei_policy &= ~NI_BENEATH;
    }
    dp->i_count++;
    u.u_error = direnter(ip, ndp);
""",
            mutation,
        )
    if mutation == "omit-rooted-mkdir-release":
        return replace_once(
            source,
            """out:
    if (namei_policy & NI_BENEATH)
        ufs_namespace_exit();
}

/*
 * Mkdir system call
""",
            """out:
}

/*
 * Mkdir system call
""",
            mutation,
        )
    if mutation == "omit-mkdir-parent-rollback":
        return replace_once(
            source,
            """    if (u.u_error) {
        error = u.u_error;
        ilock(dp);
        dp->i_nlink--;
        dp->i_flag |= ICHG;
        iput(dp);
        u.u_error = error;
    } else {
""",
            """    if (u.u_error) {
        error = u.u_error;
        ilock(dp);
        iput(dp);
        u.u_error = error;
    } else {
""",
            mutation,
        )
    if mutation == "omit-mkdir-parent-iput":
        return replace_once(
            source,
            """    } else {
        irele(dp);
    }
    dp = NULL;
""",
            """    }
    dp = NULL;
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
