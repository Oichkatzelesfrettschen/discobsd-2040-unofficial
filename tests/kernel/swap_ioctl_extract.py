"""Extract the production swap-device provider for host gates: the
temporary-swap ioctl, the live-device write rule, open and strategy."""

import argparse
import hashlib
from pathlib import Path

STATE_START = "#define TD_APPEND_DISABLED 0xffffU\n"
STATE_END = "static struct swtemp td[NTMP];"
SWSIZE_SIGNATURE = "daddr_t\nswsize(dev_t dev)"
SWCIOCTL_SIGNATURE = (
    "int\nswcioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)"
)
WRITE_FLAGS_SIGNATURE = (
    "static int\nswtemp_write_flags(struct swtemp *temp, u_int block, u_int blocks)"
)
REFUSED_SIGNATURE = "static int\nswap_live_write_refused(int unit, int writing)"
SWOPEN_SIGNATURE = "int\nswopen(dev_t dev, int mode, int flag)"
SWSTRATEGY_SIGNATURE = "void\nswstrategy(struct buf *bp)"
INCLUDES = """#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/disk.h>

#ifndef NTMP
#define NTMP 3
#endif
"""
DEVICE_DECLARATIONS = """extern int securelevel;
extern void swap(size_t blkno, size_t coreaddr, int count, int rdflg);

"""
TEST_HOOKS = """
void
swap_ioctl_test_reset(void)
{
    int unit;

    for (unit = 0; unit < NTMP; unit++) {
        td[unit].t_start = 0;
        td[unit].t_size = 0;
        td[unit].t_next = TD_APPEND_DISABLED;
    }
}

u_int
swap_ioctl_test_start(int unit)
{
    return td[unit].t_start;
}

u_int
swap_ioctl_test_size(int unit)
{
    return td[unit].t_size;
}

u_int
swap_ioctl_test_next(int unit)
{
    return td[unit].t_next;
}
"""


def extract_function(source: str, signature: str, label: str) -> str:
    """Return one unique C function definition, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError(label + " signature is not unique")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for offset in range(opening, len(source)):
        character = source[offset]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise ValueError("unterminated " + label + " function")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    if source.count(STATE_START) != 1 or source.count(STATE_END) != 1:
        raise ValueError("temporary-swap state block is not unique")
    state_start = source.index(STATE_START)
    state_end = source.index(STATE_END, state_start) + len(STATE_END)
    state = source[state_start:state_end]
    swsize = extract_function(source, SWSIZE_SIGNATURE, "swsize")
    swcioctl = extract_function(source, SWCIOCTL_SIGNATURE, "swcioctl")
    device = "\n\n".join(
        extract_function(source, signature, label)
        for signature, label in (
            (WRITE_FLAGS_SIGNATURE, "swtemp_write_flags"),
            (REFUSED_SIGNATURE, "swap_live_write_refused"),
            (SWOPEN_SIGNATURE, "swopen"),
            (SWSTRATEGY_SIGNATURE, "swstrategy"),
        )
    )
    source_hash = hashlib.sha256(source_bytes).hexdigest()

    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + INCLUDES
        + state
        + "\n\n"
        + swsize
        + "\n\n"
        + swcioctl
        + "\n\n"
        + DEVICE_DECLARATIONS
        + device
        + "\n"
        + TEST_HOOKS
    )


if __name__ == "__main__":
    main()
