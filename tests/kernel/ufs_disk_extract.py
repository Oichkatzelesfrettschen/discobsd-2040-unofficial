"""Extract the production disk-routing code that ufs_disk_test.c exercises.

conf.c names every driver of the port, and flash.c needs the Dhara map and the
boot ROM, so neither links into a host binary whole. The gate takes the text
of the routines it tests from the owning file, the way swap_ioctl_extract.py
takes the temporary-swap provider:

  conf   isdisk, chrtoblk, iskmemdev, blkdev_same_unit and blkoverlap from
         sys/arch/rp2040/rp2040/conf.c
  flash  the partition state (NPARTITIONS through fldrives), flopen, flclose
         and flioctl from sys/arch/rp2040/dev/flash.c

A signature that is missing or not unique stops the extraction, so a renamed
routine fails the gate instead of leaving it testing a stale copy.
"""

import argparse
import hashlib
import re
from pathlib import Path

CONF_INCLUDES = """#include <sys/param.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <rp2040/dev/sd.h>
"""

CONF_FUNCTIONS = (
    ("iskmemdev", "int\niskmemdev(dev_t dev)"),
    ("isdisk", "int\nisdisk(dev_t dev, int type)"),
    ("chrtoblk", "int\nchrtoblk(dev_t dev __unused)"),
    ("blkdev_same_unit", "int\nblkdev_same_unit(dev_t a, dev_t b)"),
    ("blkoverlap", "int\nblkoverlap(dev_t a, dev_t b)"),
)

# <sys/buf.h> has no include guard; the gate includes it before the extraction.
FLASH_INCLUDES = """#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/disk.h>
"""

FLASH_STATE_START = "#define\tNPARTITIONS\tFLASH_PARTITION_COUNT\n"
FLASH_STATE_END = "static struct fldisk fldrives[NFL];\n"

FLASH_FUNCTIONS = (
    ("flopen", "int\nflopen(dev_t dev, int flags __unused, int mode __unused)"),
    ("flclose", "int\nflclose(dev_t dev, int mode __unused, int flag __unused)"),
    (
        "flioctl",
        "int\nflioctl(dev_t dev, u_int cmd, caddr_t addr, int flag __unused)",
    ),
)

# flash.h includes <stdint.h>, which a host build cannot take beside the
# kernel's own; the one macro the state block needs comes from the header's
# text.
PARTITION_COUNT = re.compile(r"^#define\tFLASH_PARTITION_COUNT\t(\d+)$", re.M)


def extract_function(source: str, signature: str, label: str) -> str:
    """Return one unique C function definition, including its closing brace."""
    if source.count(signature) != 1:
        raise SystemExit(label + " signature is not unique")
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
    raise SystemExit("unterminated " + label + " function")


def banner(path: Path, data: bytes) -> str:
    return (
        "/* Extracted from "
        + path.as_posix()
        + " SHA-256 "
        + hashlib.sha256(data).hexdigest()
        + ". */\n"
    )


def extract_conf(source_path: Path) -> str:
    data = source_path.read_bytes()
    source = data.decode()
    functions = [extract_function(source, sig, name) for name, sig in CONF_FUNCTIONS]
    return banner(source_path, data) + CONF_INCLUDES + "\n" + "\n\n".join(functions) + "\n"


def extract_flash(source_path: Path, header_path: Path) -> str:
    data = source_path.read_bytes()
    source = data.decode()
    if source.count(FLASH_STATE_START) != 1 or source.count(FLASH_STATE_END) != 1:
        raise SystemExit("partition state block is not unique")
    start = source.index(FLASH_STATE_START)
    end = source.index(FLASH_STATE_END, start) + len(FLASH_STATE_END)
    state = source[start:end]
    count = PARTITION_COUNT.search(header_path.read_text())
    if count is None:
        raise SystemExit("FLASH_PARTITION_COUNT is not defined in flash.h")
    functions = [extract_function(source, sig, name) for name, sig in FLASH_FUNCTIONS]
    return (
        banner(source_path, data)
        + FLASH_INCLUDES
        + "\n#define\tFLASH_PARTITION_COUNT\t"
        + count.group(1)
        + "\n"
        + state
        + "\n"
        + "\n\n".join(functions)
        + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("conf", "flash"))
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    if arguments.kind == "conf":
        text = extract_conf(arguments.source)
    else:
        if arguments.header is None:
            raise SystemExit("flash extraction needs --header")
        text = extract_flash(arguments.source, arguments.header)
    arguments.output.write_text(text)


if __name__ == "__main__":
    main()
