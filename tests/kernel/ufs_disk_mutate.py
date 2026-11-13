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
    "cooked-device-flush-omitted": (
        "ufs_subr.c",
        "        error = bdevsync(ip->i_rdev);",
        "        error = 0;",
    ),
    "cooked-device-error-hidden": (
        "ufs_subr.c",
        "        error = bdevsync(ip->i_rdev);\n        if (error)\n            return (error);",
        "        error = bdevsync(ip->i_rdev);\n        error = 0;",
    ),
    "cooked-device-latch-ignored": (
        "ufs_bio.c",
        "    return error != 0 ? error : first_error;",
        "    return first_error;",
    ),
    "device-error-replica-omitted": (
        "ufs_bio.c", "                alias->i_bdev_error = write_error;",
        "                alias->i_bdev_error = 0;",
    ),
    "device-first-error-overwritten": (
        "ufs_bio.c",
        "alias->i_rdev == bp->b_dev && alias->i_bdev_error == 0)",
        "alias->i_rdev == bp->b_dev)",
    ),
    "device-busy-drain-omitted": (
        "ufs_bio.c",
        "        if (pending->b_dev != dev || (pending->b_flags & B_BUSY) == 0)",
        "        if (pending->b_dev != dev || (pending->b_flags & B_BUSY) == 0 || 1)",
    ),
    "device-lock-ignored": (
        "ufs_bio.c", "            (alias->i_bdev_state & IBDEV_LOCKED) == 0)",
        "            (alias->i_bdev_state & IBDEV_LOCKED) == 0 || 1)",
    ),
    "device-inherit-omitted": (
        "sys_inode.c", "        ip->i_bdev_error = bdev_write_error(dev);",
        "        ip->i_bdev_error = 0;",
    ),
    "device-inheritance-unmasked": (
        "sys_inode.c",
        "        priority = splbio();\n        ip->i_bdev_error = bdev_write_error(dev);",
        "        priority = 0;\n        ip->i_bdev_error = bdev_write_error(dev);",
    ),
    "device-open-count-on-failure": (
        "sys_inode.c", "        if (error == 0)\n            ip->i_bdev_state++;",
        "        ip->i_bdev_state++;",
    ),
    "device-error-lifetime-reset-omitted": (
        "sys_inode.c", "        bdev_clear_write_error(dev);",
        "        (void)dev;",
    ),
    "device-close-error-hidden": (
        "sys_inode.c", "    return flush_error != 0 ? flush_error : error;",
        "    (void)flush_error;\n    return error;",
    ),
    "device-driver-checkpoint-lost": (
        "ufs_bio.c", "    u.u_qsave = saved_checkpoint;",
        "    (void)saved_checkpoint;",
    ),
    "device-unlock-wakeup-omitted": (
        "ufs_bio.c",
        "bdevunlock(struct inode *holder)\n{\n"
        "    holder->i_bdev_state &= ~IBDEV_LOCKED;\n    wakeup((caddr_t)holder);\n}",
        "bdevunlock(struct inode *holder)\n{\n"
        "    holder->i_bdev_state &= ~IBDEV_LOCKED;\n}",
    ),
    "device-mount-guard-invisible": (
        "ufs_bio.c",
        "!blkdev_same_unit(device_mount->m_dev, device))",
        "!blkdev_same_unit(device_mount->m_dev, device) || 1)",
    ),
    "device-block-admission-before-lock": (
        "sys_inode.c",
        "        if (error == 0 && securelevel > 0)\n"
        "            error = ufs_mountedon(dev);",
        "        if (error == 0 && securelevel > 0)\n"
        "            error = 0;",
    ),
    "device-raw-admission-before-lock": (
        "sys_inode.c",
        "        if (error == 0 && securelevel >= 1 && (mode & FWRITE))\n"
        "            error = ufs_mountedon(bdev);",
        "        if (error == 0 && securelevel >= 1 && (mode & FWRITE))\n"
        "            error = 0;",
    ),
    "device-block-policy-raise-ignored": (
        "sys_inode.c",
        "        error = securelevel >= 2 && (mode & FWRITE) && isdisk(dev, IFBLK) ? EPERM : 0;",
        "        error = 0;",
    ),
    "device-raw-policy-raise-ignored": (
        "sys_inode.c",
        "        error = securelevel >= 2 && (mode & FWRITE) && isdisk(dev, IFCHR) ? EPERM : 0;",
        "        error = 0;",
    ),
    "device-reuse-state-kept": (
        "ufs_inode.c",
        "            if (ip->i_count == 0) {     /* ino on free list */\n"
        "                register struct inode *iq;\n\n"
        "                ip->i_wsel = NULL;\n"
        "                iq = ip->i_freef;",
        "            if (ip->i_count == 0) {     /* ino on free list */\n"
        "                register struct inode *iq;\n\n"
        "                iq = ip->i_freef;",
    ),
    "device-stm32-unit-guard-exact-only": (
        "conf.c",
        "\t\treturn ((minor(a) ^ minor(b)) & 8) == 0;",
        "\t\treturn a == b;",
    ),
    "device-unit-guard-exact-only": (
        "conf.c",
        "blkdev_same_unit(dev_t a, dev_t b)\n{",
        "blkdev_same_unit(dev_t a, dev_t b)\n{\n\treturn a == b;",
    ),
    "device-inode-released-early": (
        "vfs_vnops.c",
        "    error = closei(ip, fp->f_flag);\n    irele(ip);",
        "    irele(ip);\n    error = closei(ip, fp->f_flag);",
    ),
    "device-mount-lock-omitted": (
        "ufs_mount.c",
        "    bdevlock(device_inode);\n    NAMEI_INIT_USER",
        "    NAMEI_INIT_USER",
    ),
    "device-mount-inheritance-omitted": (
        "ufs_mount.c", "    mp->m_write_error = bdev_write_error(dev);",
        "    mp->m_write_error = 0;",
    ),
    "device-mount-guard-transfer-omitted": (
        "ufs_mount.c", "    bdev_mount_take_lock(mp, device_inode);", "",
    ),
    "device-node-reference-leaked": (
        "ufs_mount.c", "    bdev_mount_take_lock(mp, device_inode);\n    irele(device_inode);",
        "    bdev_mount_take_lock(mp, device_inode);\n"
        "    device_inode->i_count++;\n    irele(device_inode);",
    ),
    "device-unmount-close-error-hidden": (
        "ufs_mount.c", "    error = close_error;",
        "    (void)close_error;\n    error = 0;",
    ),
    "scheduler-budget-failure-hidden": (
        "ufs_disk_schedule.c", "    return budget_exhausted;", "    return 0;",
    ),
    # sys/kern/ufs_mount.c
    "close-always": (
        "ufs_mount.c",
        "    if (nopen == 0)\n        close_error = dev_driver_close(dev, 0, S_IFBLK);",
        "    close_error = dev_driver_close(dev, 0, S_IFBLK);",
    ),
    "mount-close-always": (
        "ufs_mount.c",
        "        if (iflush(dev) == 0)\n"
        "            dev_driver_close(dev,\n"
        "                ronly? FREAD : FREAD|FWRITE, S_IFBLK);",
        "        dev_driver_close(dev,\n"
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
        "    if (nopen == 0)\n        close_error = dev_driver_close(dev, 0, S_IFBLK);",
        "    if (nopen == 0 && error == 0)\n"
        "        close_error = dev_driver_close(dev, 0, S_IFBLK);",
    ),
    "gone-no-invalidate": (
        "ufs_mount.c",
        "        close_error = dev_driver_close(dev, 0, S_IFBLK);\n"
        "    binval(dev);\n    error = close_error;",
        "        close_error = dev_driver_close(dev, 0, S_IFBLK);\n    error = close_error;",
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
