"""Check the RP2040 root noatime policy from boot mount through remount."""

from __future__ import annotations

import argparse
from collections.abc import Callable
from pathlib import Path


class CheckError(RuntimeError):
    """Report a root noatime contract mismatch."""


CONFIG_OPTION = 'options         "ROOT_MOUNT_FLAGS=MNT_NOATIME"'
MAKEFILE_OPTION = "PARAM += -DROOT_MOUNT_FLAGS=MNT_NOATIME"
READ_GUARD = "if (! (INODE_FILESYSTEM(ip)->fs_flags & MNT_NOATIME))"
EXPLICIT_ATIME = (
    "if (vap->va_atime != (time_t)VNOVAL)\n"
    "            ip->i_flag |= IACC;"
)


def check_tree(root: Path, overrides: dict[str, str] | None = None) -> None:
    overrides = overrides or {}

    def source(relative_path: str) -> str:
        return overrides.get(relative_path, (root / relative_path).read_text())

    for configuration in ("PICO", "PICO_UART"):
        config_path = f"sys/arch/rp2040/compile/{configuration}/Config"
        if source(config_path).count(CONFIG_OPTION) != 1:
            raise CheckError(f"{config_path} must select the root noatime policy")

        makefile_path = f"sys/arch/rp2040/compile/{configuration}/Makefile"
        if source(makefile_path).count(MAKEFILE_OPTION) != 1:
            raise CheckError(f"{makefile_path} must carry the generated policy")

    init_main = source("sys/kern/init_main.c")
    if "#ifndef ROOT_MOUNT_FLAGS\n#define ROOT_MOUNT_FLAGS 0\n#endif" not in init_main:
        raise CheckError("non-RP2040 root mounts must retain zero policy flags")
    mount_call = (
        "fs = mountfs(rootdev, ROOT_MOUNT_FLAGS |\n"
        "\t    ((boothowto & RB_RDONLY) ? MNT_RDONLY : 0), 0);"
    )
    if mount_call not in init_main:
        raise CheckError("the initial root mount must apply ROOT_MOUNT_FLAGS")

    fstab_lines = [
        line.split()
        for line in source("etc/fstab.rp2040").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    expected_fstab = ["/dev/fl0a", "/", "ufs", "rw,async,noaccesstime", "1", "1"]
    if fstab_lines != [expected_fstab]:
        raise CheckError("the root remount must preserve noaccesstime")

    sys_inode = source("sys/kern/sys_inode.c")
    if sys_inode.count(READ_GUARD) != 2:
        raise CheckError("rwip and character reads must suppress automatic atime")

    ufs_fio = source("sys/kern/ufs_fio.c")
    if EXPLICIT_ATIME not in ufs_fio:
        raise CheckError("explicit utimes atime must bypass the mount read policy")


def expect_bad(label: str, action: Callable[[], None]) -> None:
    try:
        action()
    except CheckError:
        return
    raise AssertionError(f"known-bad fixture passed: {label}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    root = arguments.root
    check_tree(root)

    config_path = "sys/arch/rp2040/compile/PICO/Config"
    config = (root / config_path).read_text()
    expect_bad(
        "missing boot policy",
        lambda: check_tree(root, {config_path: config.replace(CONFIG_OPTION, "", 1)}),
    )

    init_path = "sys/kern/init_main.c"
    init_main = (root / init_path).read_text()
    expect_bad(
        "ignored initial policy",
        lambda: check_tree(
            root, {init_path: init_main.replace("ROOT_MOUNT_FLAGS |", "0 |", 1)}
        ),
    )

    fstab_path = "etc/fstab.rp2040"
    fstab = (root / fstab_path).read_text()
    expect_bad(
        "remount drops noatime",
        lambda: check_tree(
            root, {fstab_path: fstab.replace(",noaccesstime", "", 1)}
        ),
    )

    inode_path = "sys/kern/sys_inode.c"
    sys_inode = (root / inode_path).read_text()
    expect_bad(
        "read dirties atime",
        lambda: check_tree(
            root, {inode_path: sys_inode.replace(READ_GUARD, "if (1)", 1)}
        ),
    )

    fio_path = "sys/kern/ufs_fio.c"
    ufs_fio = (root / fio_path).read_text()
    legacy_explicit_atime = (
        "if (vap->va_atime != (time_t)VNOVAL &&\n"
        "            ! (INODE_FILESYSTEM(ip)->fs_flags & MNT_NOATIME))\n"
        "            ip->i_flag |= IACC;"
    )
    expect_bad(
        "explicit atime suppressed",
        lambda: check_tree(
            root, {fio_path: ufs_fio.replace(EXPLICIT_ATIME, legacy_explicit_atime, 1)}
        ),
    )

    print("RP2040 root noatime contracts: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
