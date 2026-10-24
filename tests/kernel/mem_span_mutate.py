"""Create calibrated mutations of /dev/mem complete-span admission."""

import argparse
from pathlib import Path

MUTATIONS = (
    "endpoint-admission",
    "user-rejected",
    "kernel-rejected",
    "kernel-first-byte",
)


def replace_unique(source: str, original: str, replacement: str, name: str) -> str:
    if source.count(original) != 1:
        raise ValueError(name + " mutation site is not unique")
    return source.replace(original, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    complete = (
        "if (badkaddr_span(addr, iov->iov_len) &&\n"
        "\t\t\t    baduaddr_span(addr, iov->iov_len)) {"
    )
    if arguments.mutation == "endpoint-admission":
        replacement = (
            "if ((badkaddr(addr) && baduaddr(addr)) ||\n"
            "\t\t\t    (badkaddr(addr + iov->iov_len - 1) &&\n"
            "\t\t\t     baduaddr(addr + iov->iov_len - 1))) {"
        )
        source = replace_unique(source, complete, replacement, arguments.mutation)
    elif arguments.mutation == "user-rejected":
        source = replace_unique(
            source,
            complete,
            "if (badkaddr_span(addr, iov->iov_len)) {",
            arguments.mutation,
        )
    elif arguments.mutation == "kernel-rejected":
        source = replace_unique(
            source,
            complete,
            "if (baduaddr_span(addr, iov->iov_len)) {",
            arguments.mutation,
        )
    else:
        original = "(u_int)addr, nbytes)"
        if source.count(original) != 2:
            raise ValueError(arguments.mutation + " mutation sites are not unique")
        source = source.replace(
            original, "(u_int)addr, ((void)nbytes, 1))", 1
        ).replace(original, "(u_int)addr, 1)", 1)

    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
