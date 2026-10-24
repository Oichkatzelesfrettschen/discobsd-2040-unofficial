#!/usr/bin/env python3
"""Generate calibrated rooted conditional-unlink mutations."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from unlinkat_beneath_extract import FUNCTIONS, INCLUDES, extract_function


def replace_once(source: str, old: str, new: str, mutation: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"{mutation}: expected one source match, found {count}"
        )
    return source.replace(old, new)


def mutate_function(function: str, mutation: str) -> str:
    if mutation == "omit-namespace-acquisition":
        function = replace_once(function, "    int error;\n", "", mutation)
        function = replace_once(
            function,
            """    error = ufs_namespace_enter();
    if (error != 0) {
        u.u_error = error;
        irele(expected);
        return;
    }

""",
            "",
            mutation,
        )
        return replace_once(
            function,
            """release_expected:
    irele(expected);
    ufs_namespace_exit();
}""",
            """release_expected:
    irele(expected);
}""",
            mutation,
        )
    if mutation == "release-before-removal":
        function = replace_once(
            function,
            "    if (dirremove(ndp)) {\n",
            "    ufs_namespace_exit();\n    if (dirremove(ndp)) {\n",
            mutation,
        )
        return replace_once(
            function,
            """release_expected:
    irele(expected);
    ufs_namespace_exit();
}""",
            """release_expected:
    irele(expected);
}""",
            mutation,
        )
    if mutation == "skip-identity-comparison":
        return replace_once(
            function,
            """    if (ip != expected) {
        u.u_error = ESTALE;
        goto out;
    }
""",
            "",
            mutation,
        )
    if mutation == "drop-expected-reference":
        return replace_once(
            function,
            """    igrab(expected);
    iunlock(expected);
""",
            """    igrab(expected);
    iunlock(expected);
    irele(expected);
""",
            mutation,
        )
    if mutation == "decrement-on-write-error":
        return replace_once(
            function,
            "    if (dirremove(ndp)) {\n",
            "    if (dirremove(ndp) || u.u_error != 0) {\n",
            mutation,
        )
    raise SystemExit(f"unknown mutation: {mutation}")


def mutate(source: str, mutation: str) -> str:
    signature = "void\nunlinkat_beneath(void)"
    start = source.index(signature)
    function = extract_function(source, signature, "unlinkat_beneath")
    end = start + len(function)
    mutated_function = mutate_function(function, mutation)
    return source[:start] + mutated_function + source[end:]


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
