"""The CPUs this process may run on: the smaller of its affinity mask and
the CPU quota its cgroup hierarchy grants.

os.cpu_count() counts the host's online CPUs, so inside a container or a
systemd scope with CPUQuota= it overstates the budget, and a pool sized
from it oversubscribes the quota: every worker then runs in slices and a
timing-sensitive check sees gaps no host load would explain.
sched_getaffinity() honors CPU pinning on Linux; the cgroup walk below
honors a quota set on the process's cgroup or any ancestor.

`tools/online-cpus.sh` applies the same two bounds for make's -j, and
`tools/available_cpus_test.py` holds both to fixture hierarchies.

Usage: $PYTHON tools/available_cpus.py   (prints the count)
"""

import math
import os
import sys
from pathlib import Path
from typing import Optional

PROC_SELF_CGROUP = Path("/proc/self/cgroup")
CGROUP_ROOT = Path("/sys/fs/cgroup")


def cgroup_cpu_quota(
    proc_cgroup: Path = PROC_SELF_CGROUP, cgroup_root: Path = CGROUP_ROOT
) -> Optional[float]:
    """CPUs the process's cgroup hierarchy grants, or None for no quota.

    /proc/self/cgroup names the process's cgroup in each hierarchy. A
    cgroup v2 line "0::/path" places cpu.max ("QUOTA PERIOD" in
    microseconds, "max" for none) under cgroup_root/path and in every
    ancestor up to the root. A cgroup v1 line whose controllers include cpu
    places cpu.cfs_quota_us (-1 for none) and cpu.cfs_period_us under the
    cpu controller mount, cgroup_root/cpu or cgroup_root/cpu,cpuacct. An
    ancestor's quota binds its descendants, so the minimum over the walk is
    the grant. An unreadable or malformed file counts as no quota there.
    """
    try:
        lines = proc_cgroup.read_text().splitlines()
    except OSError:
        return None
    quotas: list[float] = []
    for line in lines:
        fields = line.split(":", 2)
        if len(fields) != 3:
            continue
        hierarchy, controllers, path = fields
        if hierarchy == "0" and controllers == "":
            for node in _ancestors(cgroup_root, path):
                quota = _v2_quota(node / "cpu.max")
                if quota is not None:
                    quotas.append(quota)
        elif "cpu" in controllers.split(","):
            for mount in (cgroup_root / "cpu", cgroup_root / "cpu,cpuacct"):
                if not mount.is_dir():
                    continue
                for node in _ancestors(mount, path):
                    quota = _v1_quota(node / "cpu.cfs_quota_us", node / "cpu.cfs_period_us")
                    if quota is not None:
                        quotas.append(quota)
                break
    return min(quotas) if quotas else None


def _ancestors(mount: Path, path: str) -> list[Path]:
    """The cgroup directory path names under mount, then each ancestor."""
    parts = [part for part in path.strip("/").split("/") if part]
    return [mount.joinpath(*parts[:depth]) for depth in range(len(parts), -1, -1)]


def _v2_quota(cpu_max: Path) -> Optional[float]:
    try:
        quota_text, period_text = cpu_max.read_text().split()
        if quota_text == "max":
            return None
        quota, period = int(quota_text), int(period_text)
    except (OSError, ValueError):
        return None
    return quota / period if quota > 0 and period > 0 else None


def _v1_quota(quota_file: Path, period_file: Path) -> Optional[float]:
    try:
        quota = int(quota_file.read_text().strip())
        period = int(period_file.read_text().strip())
    except (OSError, ValueError):
        return None
    return quota / period if quota > 0 and period > 0 else None


def available_cpus(proc_cgroup: Path = PROC_SELF_CGROUP, cgroup_root: Path = CGROUP_ROOT) -> int:
    """The affinity count bounded by the cgroup quota, rounded up, at least 1."""
    if hasattr(os, "sched_getaffinity"):
        available = len(os.sched_getaffinity(0)) or 1
    else:
        available = os.cpu_count() or 1
    quota = cgroup_cpu_quota(proc_cgroup, cgroup_root)
    if quota is None:
        return max(1, available)
    return max(1, min(available, math.ceil(quota)))


if __name__ == "__main__":
    print(available_cpus())
    sys.exit(0)
