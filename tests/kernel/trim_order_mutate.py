"""Generate calibrated mutations of the block-release order trim_order_test checks.

Each mutation restores one ordering the change replaced, by rewriting one
anchored fragment of sys/kern/ufs_inode.c or sys/kern/ufs_alloc.c. An anchor
that is missing or not unique stops the run, so a mutant never silently
equals the shipped source.

  --output FILE   write the mutated source
  --others OBJS   print the gate objects the mutant does not replace
  --expect        print the failed-check text the mutant must produce
"""

import argparse
from pathlib import Path

INODE_WRITE = "    error = iupdat(oip, &time, &time, 1);\n"

# name -> (source, [(old, new), ...], expected failure text)
MUTATIONS = {
    # The 2.11BSD order: the inode is written after the blocks are freed.
    "inode-after-frees": (
        "ufs_inode.c",
        [
            (INODE_WRITE, "    error = 0;\n"),
            ("#endif\n    return;\n\ndoquotaupd:", "#endif\n\ndoquotaupd:"),
        ],
        "check_every_cut(ino) < 0",
    ),
    # The trim follows the push and the chain write instead of preceding them.
    "trim-after-chain": (
        "ufs_alloc.c",
        [
            ("        return;\n    }\n    free_trim(ip, fs, bno);\n", "        return;\n    }\n"),
            (
                "    fs->fs_free[fs->fs_nfree++] = bno;\n    fs->fs_tfree++;\n"
                "    fs->fs_fmod = 1;\n}",
                "    fs->fs_free[fs->fs_nfree++] = bno;\n    fs->fs_tfree++;\n"
                "    fs->fs_fmod = 1;\n    free_trim(ip, fs, bno);\n}",
            ),
        ],
        "!is_erased(block)",
    ),
    # No trim: freed blocks stay mapped.
    "trim-skipped": (
        "ufs_alloc.c",
        [("    bdev_trim(INODE_DEVICE(ip), FSBTODB(fs, bno));\n", "    (void)bno;\n")],
        "live_end == live_start",
    ),
    # The inode write is delayed on an async mount, as iupdat(..., 0) does.
    "inode-delayed-async": (
        "ufs_inode.c",
        [
            (
                INODE_WRITE,
                "    error = iupdat(oip, &time, &time,\n"
                "        !(INODE_FILESYSTEM(oip)->fs_flags & MNT_ASYNC));\n",
            )
        ],
        "check_every_cut(ino) < 0",
    ),
}


def mutate(text: str, name: str, edits: list) -> str:
    for old, new in edits:
        count = text.count(old)
        if count != 1:
            raise SystemExit(f"{name}: expected one anchor, found {count}: {old!r}")
        text = text.replace(old, new, 1)
    return text


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutation", required=True, choices=sorted(MUTATIONS))
    parser.add_argument("--root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--others", nargs="*")
    parser.add_argument("--expect", action="store_true")
    arguments = parser.parse_args()
    source, edits, expect = MUTATIONS[arguments.mutation]
    if arguments.expect:
        print(expect)
        return
    if arguments.others is not None:
        replaced = "trim_order_" + source.removesuffix(".c") + ".o"
        print(" ".join(o for o in arguments.others if o != replaced))
        return
    if arguments.root is None or arguments.output is None:
        raise SystemExit("--root and --output are required to mutate")
    text = (arguments.root / "sys" / "kern" / source).read_text()
    arguments.output.write_text(mutate(text, arguments.mutation, edits))


if __name__ == "__main__":
    main()
