"""Create calibrated mutations for temporary-swap ioctl contracts."""

import argparse
from pathlib import Path

RELEASE_SUCCESS = """\t\tif (*offtval == 0)
\t\t\treturn 0;
\t\tbreak;"""
RELEASE_FALLTHROUGH = """\t\tbreak;"""
MEDIA_SUCCESS = """\tcase DIOCGETMEDIASIZE:
\t\t*uival = swsize(dev);
\t\treturn 0;"""
MEDIA_FALLTHROUGH = """\tcase DIOCGETMEDIASIZE:
\t\t*uival = swsize(dev);
\t\tbreak;"""
SHORTAGE_OUTPUT = """\t\t\t*offtval = 0;
\t\t\tprintf("temp%d: failed to allocate %lu blocks\\n","""
SHORTAGE_SUPPRESSED = """\t\t\tprintf("temp%d: failed to allocate %lu blocks\\n","""
ROUNDED_FREE = """\t\t\tmfree(swapmap,
\t\t\t    (td[unit].t_size + SWAP_IMAGE_ALIGN - 1) &
\t\t\t    ~(SWAP_IMAGE_ALIGN - 1), td[unit].t_start);"""
UNROUNDED_FREE = """\t\t\tmfree(swapmap, td[unit].t_size,
\t\t\t    td[unit].t_start);"""
CLEARED_SIZE = """\t\t\ttd[unit].t_start = 0;
\t\t\ttd[unit].t_size = 0;
\t\t\ttd[unit].t_next = TD_APPEND_DISABLED;"""
STALE_SIZE = """\t\t\ttd[unit].t_start = 0;
\t\t\ttd[unit].t_next = TD_APPEND_DISABLED;"""


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise ValueError(label + " mutation does not match exactly once")
    return source.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--mutation",
        choices=(
            "release-fallthrough",
            "media-size-fallthrough",
            "shortage-output-suppression",
            "unrounded-free",
            "stale-released-size",
        ),
        required=True,
    )
    arguments = parser.parse_args()
    source = arguments.source.read_text()

    if arguments.mutation == "release-fallthrough":
        source = replace_once(
            source,
            RELEASE_SUCCESS,
            RELEASE_FALLTHROUGH,
            arguments.mutation,
        )
    elif arguments.mutation == "media-size-fallthrough":
        source = replace_once(
            source,
            MEDIA_SUCCESS,
            MEDIA_FALLTHROUGH,
            arguments.mutation,
        )
    elif arguments.mutation == "shortage-output-suppression":
        source = replace_once(
            source,
            SHORTAGE_OUTPUT,
            SHORTAGE_SUPPRESSED,
            arguments.mutation,
        )
    elif arguments.mutation == "unrounded-free":
        source = replace_once(
            source,
            ROUNDED_FREE,
            UNROUNDED_FREE,
            arguments.mutation,
        )
    else:
        source = replace_once(
            source,
            CLEARED_SIZE,
            STALE_SIZE,
            arguments.mutation,
        )
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
