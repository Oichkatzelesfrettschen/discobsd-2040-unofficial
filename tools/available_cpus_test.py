"""Calibration for tools/available_cpus.py and tools/online-cpus.sh.

Both read fixture hierarchies: a /proc/self stand-in (status with
Cpus_allowed_list, cgroup) and a cgroup root. A quota on the process's own
cgroup, one on an ancestor only, a fractional quota (rounded up), cgroup
v1, "max", a malformed cpu.max and an affinity list with ranges must each
give the expected count, and the two implementations must agree on this
host. Four mutants must fail: a walk that skips ancestors and a quota
rounded down, in each language.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import available_cpus as ac  # noqa: E402

SCRIPT = HERE / "online-cpus.sh"
failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def build(base, cgroup_line, files, allowed="0-7"):
    """Write a fixture: base/self/{status,cgroup} and base/cg/<files>."""
    self_dir = base / "self"
    self_dir.mkdir(parents=True)
    (self_dir / "status").write_text(f"Name:\tsh\nCpus_allowed_list:\t{allowed}\n")
    (self_dir / "cgroup").write_text(cgroup_line + "\n")
    root = base / "cg"
    root.mkdir()
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return self_dir, root


def shell_count(self_dir, root, script=SCRIPT):
    env = dict(os.environ, ONLINE_CPUS_PROC_SELF=str(self_dir), ONLINE_CPUS_CGROUP_ROOT=str(root))
    out = subprocess.run(["sh", str(script)], env=env, capture_output=True, text=True, check=True)
    return int(out.stdout.strip())


V2_NESTED = (
    "0::/a/b",
    {"cpu.max": "max 100000\n", "a/cpu.max": "400000 100000\n", "a/b/cpu.max": "150000 100000\n"},
)
V2_ANCESTOR = ("0::/a/b", {"a/cpu.max": "300000 100000\n", "a/b/cpu.max": "max 100000\n"})
CASES = [
    # name, cgroup line, files, affinity list, expected quota, expected shell count
    ("own cgroup 1.5 CPUs under an ancestor's 4", *V2_NESTED, "0-7", 1.5, 2),
    ("ancestor-only quota of 3", *V2_ANCESTOR, "0-7", 3.0, 3),
    ("no quota anywhere", "0::/a", {"a/cpu.max": "max 100000\n"}, "0-7", None, 8),
    (
        "cgroup v1 quota of 2",
        "4:cpu,cpuacct:/x",
        {
            "cpu,cpuacct/x/cpu.cfs_quota_us": "200000\n",
            "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n",
        },
        "0-7",
        2.0,
        2,
    ),
    (
        "cgroup v1 quota -1",
        "4:cpu,cpuacct:/x",
        {"cpu,cpuacct/x/cpu.cfs_quota_us": "-1\n", "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n"},
        "0-7",
        None,
        8,
    ),
    ("malformed cpu.max", "0::/a", {"a/cpu.max": "garbage\n"}, "0-7", None, 8),
    ("affinity 0-3,8,10-11 counts 7", "0::/", {"cpu.max": "max 100000\n"}, "0-3,8,10-11", None, 7),
]

online = int(
    subprocess.run(["getconf", "_NPROCESSORS_ONLN"], capture_output=True, text=True).stdout
)

with tempfile.TemporaryDirectory() as tmp:
    for i, (name, line, files, allowed, quota, count) in enumerate(CASES):
        self_dir, root = build(Path(tmp) / str(i), line, files, allowed)
        got_quota = ac.cgroup_cpu_quota(self_dir / "cgroup", root)
        check(got_quota == quota, f"python quota, {name}: {got_quota}")
        expected = min(count, online)
        got = shell_count(self_dir, root)
        check(got == expected, f"shell count, {name}: {got}")

    # Mutants: each must change a verdict above.
    nested = build(Path(tmp) / "m1", *V2_NESTED)
    ancestor = build(Path(tmp) / "m2", *V2_ANCESTOR)
    text = SCRIPT.read_text()
    shell_mutants = {
        "shell walk without ancestors": (text.replace("node=${node%/*}", "break"), ancestor, 3),
        "shell quota rounded down": (text.replace("if (c < q) c++; ", ""), nested, 2),
    }
    for name, (mutant_text, fixture, expected) in shell_mutants.items():
        if mutant_text == text:
            check(False, f"{name}: mutation did not apply")
            continue
        mutant = Path(tmp) / "mutant.sh"
        mutant.write_text(mutant_text)
        check(shell_count(*fixture, script=mutant) != min(expected, online), f"{name} is rejected")

    shipped_ancestors = ac._ancestors
    ac._ancestors = lambda mount, path: shipped_ancestors(mount, path)[:1]
    try:
        check(
            ac.cgroup_cpu_quota(ancestor[0] / "cgroup", ancestor[1]) != 3.0,
            "python walk without ancestors is rejected",
        )
    finally:
        ac._ancestors = shipped_ancestors
    shipped_ceil = ac.math.ceil
    ac.math.ceil = int
    try:
        check(
            ac.available_cpus(nested[0] / "cgroup", nested[1]) != min(2, ac.available_cpus()),
            "python quota rounded down is rejected",
        )
    finally:
        ac.math.ceil = shipped_ceil

# The two implementations agree on this host's real affinity and cgroup.
real_shell = int(subprocess.run(["sh", str(SCRIPT)], capture_output=True, text=True).stdout)
check(real_shell == ac.available_cpus(), f"shell and python agree on this host ({real_shell})")

print(f"available_cpus: {failures} failed")
sys.exit(1 if failures else 0)
