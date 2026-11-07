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
    "gone-ignored": (
        "ufs_mount.c",
        "        if (! media_gone(dev)) {",
        "        if (! media_gone(dev) || 1) {",
    ),
    "gone-assumed": (
        "ufs_mount.c",
        "        if (! media_gone(dev)) {",
        "        if (0 && ! media_gone(dev)) {",
    ),
    "gone-keeps-error": (
        "ufs_mount.c",
        "        mp->m_write_error = 0;\n",
        "",
    ),
    "gone-no-close": (
        "ufs_mount.c",
        "    if (nopen == 0)\n        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);",
        "    if (nopen == 0 && error == 0)\n"
        "        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);",
    ),
    "gone-no-invalidate": (
        "ufs_mount.c",
        "        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);\n"
        "    binval(dev);\n"
        "    return (0);",
        "        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);\n    return (0);",
    ),
    "gone-inverted": (
        "ufs_mount.c",
        "    return (gone != 0);",
        "    return (gone == 0);",
    ),
    # sys/kern/ufs_fio.c
    "exact-compare": (
        "ufs_fio.c",
        "        if (blkoverlap(mp->m_dev, dev))",
        "        if (mp->m_dev == dev)",
    ),
    # sys/arch/rp2040/rp2040/conf.c
    "ignore-unit": (
        "conf.c",
        "	if (maj != major(b) || (minor(a) >> 3) != (minor(b) >> 3))",
        "	if (maj != major(b))",
    ),
    "ignore-major": (
        "conf.c",
        "	if (maj != major(b) || (minor(a) >> 3) != (minor(b) >> 3))",
        "	if ((minor(a) >> 3) != (minor(b) >> 3))",
    ),
    "touching": (
        "conf.c",
        "	    pb.dp_offset - pa.dp_offset < pa.dp_nsectors :",
        "	    pb.dp_offset - pa.dp_offset <= pa.dp_nsectors :",
    ),
    "start-only": (
        "conf.c",
        "	return pa.dp_offset <= pb.dp_offset ?\n"
        "	    pb.dp_offset - pa.dp_offset < pa.dp_nsectors :\n"
        "	    pa.dp_offset - pb.dp_offset < pb.dp_nsectors;",
        "	return pa.dp_offset == pb.dp_offset;",
    ),
    "whole-device-disjoint": (
        "conf.c",
        "	if (pa.dp_nsectors == 0 || pb.dp_nsectors == 0)\n		return 0;",
        "	if (pa.dp_nsectors == 0 || pb.dp_nsectors == 0 ||\n"
        "	    (minor(a) & 7) == 0 || (minor(b) & 7) == 0)\n"
        "		return 0;",
    ),
    "failed-query-disjoint": (
        "conf.c",
        "(caddr_t)&pb, 0) != 0)\n		return 1;",
        "(caddr_t)&pb, 0) != 0)\n		return 0;",
    ),
    # sys/arch/rp2040/dev/flash.c
    "reinit-unguarded": (
        "flash.c",
        "		if ((fldrives[unit].openpart & ~(1U << RAWPART)) != 0)\n"
        "			return EBUSY;\n",
        "",
    ),
    "reinit-counts-whole-device": (
        "flash.c",
        "(fldrives[unit].openpart & ~(1U << RAWPART)) != 0",
        "fldrives[unit].openpart != 0",
    ),
    "reinit-first-partition-only": (
        "flash.c",
        "(fldrives[unit].openpart & ~(1U << RAWPART)) != 0",
        "(fldrives[unit].openpart & 2U) != 0",
    ),
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
