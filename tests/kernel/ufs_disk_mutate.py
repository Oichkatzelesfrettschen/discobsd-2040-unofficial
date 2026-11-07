"""Generate calibrated mutations of the disk lifecycle sources.

Each mutation restores one of the defects ufs_disk_test.c guards, or removes
one condition of the fix, by replacing a single anchored fragment of the
production source. An anchor that is missing or not unique stops the run, so
a mutant never silently equals the shipped source.
"""

import argparse
from pathlib import Path

# name -> (source file name, old fragment, new fragment)
MUTATIONS = {
    # sys/kern/ufs_mount.c
    "close-always": (
        "ufs_mount.c",
        "    if (nopen == 0)\n        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);",
        "    (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);",
    ),
    "mount-close-always": (
        "ufs_mount.c",
        "        if (iflush(dev) == 0)\n"
        "            (*bdevsw[major(dev)].d_close)(dev,\n"
        "                ronly? FREAD : FREAD|FWRITE, S_IFBLK);",
        "        (*bdevsw[major(dev)].d_close)(dev,\n"
        "            ronly? FREAD : FREAD|FWRITE, S_IFBLK);",
    ),
    # sys/kern/ufs_fio.c
    # sys/arch/rp2040/rp2040/conf.c
    # sys/arch/rp2040/dev/flash.c
}


def mutate(source: str, name: str, old: str, new: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{name}: expected one anchor, found {count}")
    return source.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True, choices=sorted(MUTATIONS))
    arguments = parser.parse_args()

    target, old, new = MUTATIONS[arguments.mutation]
    if arguments.source.name != target:
        raise SystemExit(f"{arguments.mutation} mutates {target}, not {arguments.source.name}")
    text = arguments.source.read_text()
    arguments.output.write_text(mutate(text, arguments.mutation, old, new))


if __name__ == "__main__":
    main()
