"""Calibration for tools/available_cpus.py and tools/online-cpus.sh.

Both counters read fixture hierarchies: a /proc/self stand-in (status
with Cpus_allowed_list, cgroup) and a cgroup root. The shell counter takes
the online count from ONLINE_CPUS_ONLINE and the Python counter its
affinity from an argument, both 64, so every verdict holds on a host of
any size. A quota on the process's own cgroup, a tighter one on an
ancestor, one on an ancestor only, a fractional quota (rounded up),
cgroup v1 under each mount name and beside an empty sibling, v1 ancestors,
a hybrid host's v1 line beside a v2 line under unified/, a v2-only host
whose root holds a sibling cgroup named unified with a tighter quota,
"max", malformed files and an affinity list with ranges must each give the
expected count. On the host, both counters must agree, and pinned by
`taskset` (where present) to the lowest CPU in the process's affinity
mask, which a cpuset that excludes CPU 0 still allows, both must print 1.

Mutants that must fail: in each language a walk that skips ancestors, a
quota rounded down and the first quota taken in place of the tightest;
in Python an affinity ignored (under taskset); in the shell one v1 mount
name dropped, the hybrid unified/ mount dropped, unified/ walked on a
v2-only host and a lax cpu.max pattern; in Python unified/ walked on a
v2-only host and never walked. mutant_pool.workers() must equal available_cpus() when
MUTANT_JOBS is unset.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import available_cpus as ac  # noqa: E402
import mutant_pool  # noqa: E402

SCRIPT = HERE / "online-cpus.sh"
ONLINE = 64
failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def clean_env():
    return {k: v for k, v in os.environ.items() if not k.startswith("ONLINE_CPUS_")}


def build(base, cgroup_line, files, allowed="0-63"):
    """Write a fixture: base/self/{status,cgroup} and base/cg/<files>."""
    self_dir = base / "self"
    self_dir.mkdir(parents=True)
    (self_dir / "status").write_text(f"Name:\tsh\nCpus_allowed_list:\t{allowed}\n")
    (self_dir / "cgroup").write_text(cgroup_line + "\n")
    root = base / "cg"
    root.mkdir()
    for rel, text in files.items():
        path = root / rel
        if text is None:
            path.mkdir(parents=True, exist_ok=True)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return self_dir, root


def shell_count(self_dir, root, script=SCRIPT):
    env = dict(
        clean_env(),
        ONLINE_CPUS_PROC_SELF=str(self_dir),
        ONLINE_CPUS_CGROUP_ROOT=str(root),
        ONLINE_CPUS_ONLINE=str(ONLINE),
    )
    out = subprocess.run(["sh", str(script)], env=env, capture_output=True, text=True, check=True)
    return int(out.stdout.strip())


def python_count(self_dir, root):
    return ac.available_cpus(self_dir / "cgroup", root, affinity=ONLINE)


def v1(quota, period="100000\n"):
    return quota, period


# name, cgroup line, files, affinity list, expected quota, expected count
NESTED = (
    "0::/a/b",
    {"cpu.max": "max 100000\n", "a/cpu.max": "400000 100000\n", "a/b/cpu.max": "150000 100000\n"},
)
TIGHT_ANCESTOR = ("0::/a/b", {"a/cpu.max": "100000 100000\n", "a/b/cpu.max": "300000 100000\n"})
ANCESTOR_ONLY = ("0::/a/b", {"a/cpu.max": "300000 100000\n", "a/b/cpu.max": "max 100000\n"})
V1_CPU_MOUNT = (
    "4:cpu,cpuacct:/x/y",
    {
        "cpu/x/cpu.cfs_quota_us": "200000\n",
        "cpu/x/cpu.cfs_period_us": "100000\n",
        "cpu/x/y/cpu.cfs_quota_us": "-1\n",
        "cpu/x/y/cpu.cfs_period_us": "100000\n",
    },
)
V1_SWAPPED_BESIDE_EMPTY = (
    "4:cpuacct,cpu:/x",
    {
        "cpu": None,
        "cpuacct,cpu/x/cpu.cfs_quota_us": "300000\n",
        "cpuacct,cpu/x/cpu.cfs_period_us": "100000\n",
    },
)
CASES = [
    ("own 1.5 CPUs under an ancestor's 4", *NESTED, "0-63", 1.5, 2),
    ("ancestor's 1 over own 3", *TIGHT_ANCESTOR, "0-63", 1.0, 1),
    ("ancestor-only quota of 3", *ANCESTOR_ONLY, "0-63", 3.0, 3),
    ("no quota anywhere", "0::/a", {"a/cpu.max": "max 100000\n"}, "0-63", None, ONLINE),
    (
        "cgroup v1 under cpu,cpuacct",
        "4:cpu,cpuacct:/x",
        {
            "cpu,cpuacct/x/cpu.cfs_quota_us": "200000\n",
            "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n",
        },
        "0-63",
        2.0,
        2,
    ),
    ("cgroup v1 ancestor under cpu", *V1_CPU_MOUNT, "0-63", 2.0, 2),
    ("cgroup v1 under cpuacct,cpu beside an empty cpu", *V1_SWAPPED_BESIDE_EMPTY, "0-63", 3.0, 3),
    (
        "cgroup v1 quota -1",
        "4:cpu,cpuacct:/x",
        {"cpu,cpuacct/x/cpu.cfs_quota_us": "-1\n", "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n"},
        "0-63",
        None,
        ONLINE,
    ),
    (
        "cgroup v1 quota with a unit",
        "4:cpu,cpuacct:/x",
        {
            "cpu,cpuacct/x/cpu.cfs_quota_us": "200abc\n",
            "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n",
        },
        "0-63",
        None,
        ONLINE,
    ),
    ("malformed cpu.max", "0::/a", {"a/cpu.max": "garbage\n"}, "0-63", None, ONLINE),
    (
        "cpu.max period with a unit",
        "0::/a",
        {"a/cpu.max": "200000 100000abc\n"},
        "0-63",
        None,
        ONLINE,
    ),
    (
        "cpu.max with a third field",
        "0::/a",
        {"a/cpu.max": "200000 100000 9\n"},
        "0-63",
        None,
        ONLINE,
    ),
    (
        "hybrid v1 line beside a v2 line under unified/",
        "4:cpu,cpuacct:/x\n0::/x",
        {
            "cpu,cpuacct/x/cpu.cfs_quota_us": "300000\n",
            "cpu,cpuacct/x/cpu.cfs_period_us": "100000\n",
            "unified/x/cpu.max": "200000 100000\n",
        },
        "0-63",
        2.0,
        2,
    ),
    (
        "v2-only host with a tighter sibling cgroup named unified",
        "0::/a",
        {"a/cpu.max": "400000 100000\n", "unified/cpu.max": "100000 100000\n"},
        "0-63",
        4.0,
        4,
    ),
    ("affinity 0-3,8,10-11 counts 7", "0::/", {"cpu.max": "max 100000\n"}, "0-3,8,10-11", None, 7),
]

with tempfile.TemporaryDirectory() as tmp:
    fixtures = {}
    for i, (name, line, files, allowed, quota, count) in enumerate(CASES):
        self_dir, root = build(Path(tmp) / str(i), line, files, allowed)
        fixtures[name] = (self_dir, root)
        got_quota = ac.cgroup_cpu_quota(self_dir / "cgroup", root)
        check(got_quota == quota, f"python quota, {name}: {got_quota}")
        if allowed == "0-63":
            got = python_count(self_dir, root)
            check(got == count, f"python count, {name}: {got}")
        got = shell_count(self_dir, root)
        check(got == count, f"shell count, {name}: {got}")

    # Shell mutants: each must change at least one shell verdict above.
    text = SCRIPT.read_text()
    shell_mutants = {
        "walk without ancestors": text.replace("node=${node%/*}", "break"),
        "quota rounded down": text.replace(
            "q = $1 / $2; c = int(q); if (c < q) c++;", "q = $1 / $2; c = int(q);"
        ),
        "first quota taken": text.replace(
            'for quota in $(cgroup_quotas < "$proc_self/cgroup"); do',
            'for quota in $(cgroup_quotas < "$proc_self/cgroup" | head -n 1); do',
        ),
        "cpuacct,cpu mount dropped": text.replace(' \\\n\t\t    "$cgroup_root/cpuacct,cpu"', ""),
        "hybrid unified mount dropped": text.replace(
            "v2_mount=$cgroup_root/unified", "v2_mount=$cgroup_root"
        ),
        "unified walked on a v2-only host": text.replace(
            "grep -q '^[1-9][0-9]*:' \"$proc_self/cgroup\" 2>/dev/null &&\n\t    [ -d",
            "[ -d",
        ),
        "lax cpu.max period": text.replace("$2 ~ /^[0-9]+$/ && $1 > 0", "$1 > 0"),
    }
    for name, mutant_text in shell_mutants.items():
        if mutant_text == text:
            check(False, f"shell mutant {name}: mutation did not apply")
            continue
        mutant = Path(tmp) / "mutant.sh"
        mutant.write_text(mutant_text)
        caught = any(shell_count(*fixtures[case[0]], script=mutant) != case[5] for case in CASES)
        check(caught, f"shell mutant {name} is rejected")

    # Python mutants, applied by patching the module.
    def python_caught():
        return any(
            ac.cgroup_cpu_quota(fixtures[c[0]][0] / "cgroup", fixtures[c[0]][1]) != c[4]
            or (c[3] == "0-63" and python_count(*fixtures[c[0]]) != c[5])
            for c in CASES
        )

    shipped_ancestors = ac._ancestors
    ac._ancestors = lambda mount, path: shipped_ancestors(mount, path)[:1]
    try:
        check(python_caught(), "python mutant walk without ancestors is rejected")
    finally:
        ac._ancestors = shipped_ancestors
    shipped_ceil = ac.math.ceil
    setattr(ac.math, "ceil", int)  # noqa: B010 (mypy refuses int for an overload)
    try:
        check(python_caught(), "python mutant quota rounded down is rejected")
    finally:
        setattr(ac.math, "ceil", shipped_ceil)  # noqa: B010
    shipped_mount = ac._v2_mount
    for name, mutant_mount in (
        ("unified walked on a v2-only host", lambda root, hybrid: shipped_mount(root, True)),
        ("hybrid unified mount dropped", lambda root, hybrid: root),
    ):
        ac._v2_mount = mutant_mount
        try:
            check(python_caught(), f"python mutant {name} is rejected")
        finally:
            ac._v2_mount = shipped_mount
    shipped_quota = ac.cgroup_cpu_quota

    def first_quota(proc_cgroup=ac.PROC_SELF_CGROUP, cgroup_root=ac.CGROUP_ROOT):
        quotas = []
        for line in proc_cgroup.read_text().splitlines():
            hierarchy, controllers, path = line.split(":", 2)
            if hierarchy == "0" and controllers == "":
                for node in ac._ancestors(cgroup_root, path):
                    q = ac._v2_quota(node / "cpu.max")
                    if q is not None:
                        quotas.append(q)
        return quotas[0] if quotas else shipped_quota(proc_cgroup, cgroup_root)

    ac.cgroup_cpu_quota = first_quota
    try:
        check(python_caught(), "python mutant first quota taken is rejected")
    finally:
        ac.cgroup_cpu_quota = shipped_quota

# The pool's default is the quota-aware count.
saved_jobs = os.environ.pop("MUTANT_JOBS", None)
try:
    check(
        mutant_pool.workers() == ac.available_cpus(),
        "mutant_pool's default equals available_cpus()",
    )
finally:
    if saved_jobs is not None:
        os.environ["MUTANT_JOBS"] = saved_jobs

# On the host, both counters agree, and pinned to one CPU both print 1.
real_shell = int(
    subprocess.run(
        ["sh", str(SCRIPT)], env=clean_env(), capture_output=True, text=True, check=True
    ).stdout
)
check(real_shell == ac.available_cpus(), f"shell and python agree on this host ({real_shell})")
if shutil.which("taskset") and hasattr(os, "sched_getaffinity"):
    pin = str(min(os.sched_getaffinity(0)))
    pinned_shell = subprocess.run(
        ["taskset", "-c", pin, "sh", str(SCRIPT)],
        env=clean_env(),
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    pinned_python = subprocess.run(
        ["taskset", "-c", pin, sys.executable, "-I", str(HERE / "available_cpus.py")],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    check(pinned_shell == "1" and pinned_python == "1", f"both print 1 under taskset -c {pin}")
    host_quota = ac.cgroup_cpu_quota()
    if (os.cpu_count() or 1) > 1 and (host_quota is None or host_quota > 1):
        with tempfile.TemporaryDirectory() as tmp:
            mutant = Path(tmp) / "available_cpus.py"
            source = (HERE / "available_cpus.py").read_text()
            mutant.write_text(
                source.replace("len(os.sched_getaffinity(0)) or 1", "os.cpu_count() or 1")
            )
            pinned_mutant = subprocess.run(
                ["taskset", "-c", pin, sys.executable, "-I", str(mutant)],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
        check(
            pinned_mutant != "1",
            f"python mutant affinity ignored is rejected under taskset -c {pin}",
        )
    else:
        print("SKIP affinity mutant: this process's CPU count or quota is 1, so it cannot differ")
else:
    print("SKIP taskset pin: taskset or sched_getaffinity absent on this host")

print(f"available_cpus: {failures} failed")
sys.exit(1 if failures else 0)
