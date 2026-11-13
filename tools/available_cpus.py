"""The CPUs this process may run on: the smaller of its affinity mask and
the CPU quota its cgroup hierarchy grants.

os.cpu_count() counts the host's online CPUs, so inside a container or a
systemd scope with CPUQuota= it overstates the budget, and a pool sized
from it oversubscribes the quota: every worker then runs in slices and a
timing-sensitive check sees gaps no host load would explain.
sched_getaffinity() honors CPU pinning on Linux; the cgroup walk below
honors the tightest readable quota on the process's cgroup or any
ancestor, under the standard mounts: the v2 root at /sys/fs/cgroup or, on
a hybrid host (one whose /proc/self/cgroup also carries a v1 line),
/sys/fs/cgroup/unified, and the v1 cpu controller as cpu, cpu,cpuacct or
cpuacct,cpu. A quota file the process cannot read, or a
controller mounted elsewhere, counts as no quota.

`tools/online-cpus.sh` applies the same two bounds for make's -j, and
`tools/available_cpus_test.py` holds both to fixture hierarchies.

Usage: $PYTHON tools/available_cpus.py   (prints the count)
"""

import math
import os
import re
import sys
from pathlib import Path
from typing import Optional

PROC_SELF_CGROUP = Path("/proc/self/cgroup")
CGROUP_ROOT = Path("/sys/fs/cgroup")
V1_CPU_MOUNTS = ("cpu", "cpu,cpuacct", "cpuacct,cpu")


def cgroup_cpu_quota(
    proc_cgroup: Path = PROC_SELF_CGROUP, cgroup_root: Path = CGROUP_ROOT
) -> Optional[float]:
    """CPUs the process's cgroup hierarchy grants, or None for no quota.

    /proc/self/cgroup names the process's cgroup in each hierarchy. A
    cgroup v2 line "0::/path" places cpu.max ("QUOTA PERIOD" in
    microseconds, "max" for none) under the v2 mount and in every ancestor
    up to the root. The v2 mount is cgroup_root/unified on a hybrid host,
    which a v1 line (a nonzero hierarchy number) in the same file marks, and
    cgroup_root otherwise: on a v2-only host a directory named unified is a
    sibling cgroup whose quota does not bind this process. A cgroup v1 line
    whose controllers include cpu
    places cpu.cfs_quota_us (-1 for none) and cpu.cfs_period_us under the
    cpu controller mount; every name in V1_CPU_MOUNTS that exists under
    cgroup_root is walked, so an empty sibling directory hides nothing. An
    ancestor's quota binds its descendants, so the minimum over the walk is
    the grant. An unreadable or malformed file counts as no quota there. A
    v1 controller mounted outside cgroup_root under another name is not
    found (/proc/self/mountinfo would name it).
    """
    try:
        lines = proc_cgroup.read_text().splitlines()
    except OSError:
        return None
    entries = [fields for fields in (line.split(":", 2) for line in lines) if len(fields) == 3]
    v2_mount = _v2_mount(cgroup_root, hybrid=any(fields[0] != "0" for fields in entries))
    quotas: list[float] = []
    for hierarchy, controllers, path in entries:
        if hierarchy == "0" and controllers == "":
            for node in _ancestors(v2_mount, path):
                quota = _v2_quota(node / "cpu.max")
                if quota is not None:
                    quotas.append(quota)
        elif "cpu" in controllers.split(","):
            for name in V1_CPU_MOUNTS:
                mount = cgroup_root / name
                if not mount.is_dir():
                    continue
                for node in _ancestors(mount, path):
                    quota = _v1_quota(node / "cpu.cfs_quota_us", node / "cpu.cfs_period_us")
                    if quota is not None:
                        quotas.append(quota)
    return min(quotas) if quotas else None


def _v2_mount(cgroup_root: Path, hybrid: bool) -> Path:
    """The directory the cgroup v2 hierarchy is mounted on."""
    unified = cgroup_root / "unified"
    return unified if hybrid and unified.is_dir() else cgroup_root


def _ancestors(mount: Path, path: str) -> list[Path]:
    """The cgroup directory path names under mount, then each ancestor."""
    parts = [part for part in path.strip("/").split("/") if part]
    return [mount.joinpath(*parts[:depth]) for depth in range(len(parts), -1, -1)]


# The kernel writes each file as one line of decimal digits; the shell
# counter's awk reads the same first line with the same patterns, so a
# malformed file (a sign, a unit, an underscore, a third field) counts as
# no quota in both.
DIGITS = re.compile(r"[0-9]+")
SIGNED_DIGITS = re.compile(r"-?[0-9]+")


def _first_line(path: Path) -> Optional[str]:
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return None
    return lines[0] if lines else None


def _v2_quota(cpu_max: Path) -> Optional[float]:
    line = _first_line(cpu_max)
    fields = line.split() if line is not None else []
    if len(fields) != 2 or not all(DIGITS.fullmatch(f) for f in fields):
        return None
    quota, period = int(fields[0]), int(fields[1])
    return quota / period if quota > 0 and period > 0 else None


def _v1_quota(quota_file: Path, period_file: Path) -> Optional[float]:
    quota_text, period_text = _first_line(quota_file), _first_line(period_file)
    if quota_text is None or period_text is None:
        return None
    if not SIGNED_DIGITS.fullmatch(quota_text) or not DIGITS.fullmatch(period_text):
        return None
    quota, period = int(quota_text), int(period_text)
    return quota / period if quota > 0 and period > 0 else None


def available_cpus(
    proc_cgroup: Path = PROC_SELF_CGROUP,
    cgroup_root: Path = CGROUP_ROOT,
    affinity: Optional[int] = None,
) -> int:
    """The affinity count bounded by the cgroup quota, rounded up, at least
    1. affinity replaces the process's own count; the calibration passes it
    so its fixtures do not depend on the host's CPUs."""
    if affinity is not None:
        available = affinity
    elif hasattr(os, "sched_getaffinity"):
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
