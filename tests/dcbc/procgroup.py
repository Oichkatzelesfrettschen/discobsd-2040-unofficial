"""Process-group launch and leak scan for the dc and bc gate.

usage: procgroup.py --selftest WORKDIR

bc forks a dc evaluator, so a case that exceeds its time bound leaves the
fork running when only the direct child is killed: the fork is orphaned to
init and keeps spinning after the gate has exited. run_group starts the
command as the leader of a new session; on a timeout it sends SIGKILL to the
whole process group before the leader is reaped, so the group id still names
that group, then reaps the leader. processes_under reports every process whose
executable lies under a directory, which is how mutate.py proves that no
process of a mutant build survives its run.

--selftest calibrates the scan against both launch methods: a shell that
starts a copy of sleep in a build directory is run past its time bound once
with the single-child kill that subprocess.run applies (the scan must find
the orphan) and once through run_group (the scan must find nothing).
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import time


def kill_group(pid):
    """SIGKILL the process group led by pid; a group already empty is fine."""
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_group(command, cwd=None, env=None, stdin_data=None, timeout=None, executable=None):
    """Run command as a session leader.

    Return (returncode, stdout, stderr, timed_out) with bytes output. A
    timeout kills the whole group, so a grandchild cannot outlive the case.
    """
    proc = subprocess.Popen(
        command,
        executable=executable,
        cwd=cwd,
        env=env,
        stdin=subprocess.PIPE if stdin_data is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    timed_out = False
    try:
        out, err = proc.communicate(stdin_data, timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_group(proc.pid)
        out, err = proc.communicate()
    except BaseException:
        kill_group(proc.pid)
        proc.communicate()
        raise
    return proc.returncode, out, err, timed_out


def processes_under(directory):
    """Return [(pid, exe)] for processes whose executable lies under directory."""
    prefix = os.path.realpath(directory).rstrip(os.sep) + os.sep
    found = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            exe = os.readlink("/proc/%s/exe" % entry)
        except OSError:
            continue
        exe = exe.removesuffix(" (deleted)")
        if exe.startswith(prefix):
            found.append((int(entry), exe))
    return found


def kill_processes_under(directory):
    """SIGKILL every process found by processes_under; return the list."""
    found = processes_under(directory)
    for pid, _ in found:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return found


def _wait_gone(directory, seconds=5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not processes_under(directory):
            return True
        time.sleep(0.05)
    return not processes_under(directory)


def _single_child_launch(command, timeout):
    """The launch the gate used before run_group: subprocess.run kills only the child."""
    try:
        subprocess.run(command, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        pass


def selftest(work):
    probe = os.path.join(os.path.abspath(work), "leakprobe")
    shutil.rmtree(probe, ignore_errors=True)
    os.makedirs(probe)
    sleeper = shutil.which("sleep")
    if sleeper is None:
        print("SKIP dcbc process groups: no sleep program; no scan was calibrated")
        return 0
    bc = os.path.join(probe, "bc")
    shutil.copyfile(sleeper, bc)
    os.chmod(bc, 0o755)
    # The shell is the direct child; bc is its background child.
    command = ["sh", "-c", '"$0" 300 & wait', bc]

    _single_child_launch(command, 1)
    legacy = kill_processes_under(probe)
    if not legacy:
        print("FAIL dcbc process groups: the scan missed the orphan of a single-child kill")
        return 1
    if not _wait_gone(probe):
        print("FAIL dcbc process groups: the calibration orphan did not exit")
        return 1

    _, _, _, timed_out = run_group(command, timeout=1)
    left = processes_under(probe)
    if left:
        kill_processes_under(probe)
        print("FAIL dcbc process groups: run_group left %d process(es) running" % len(left))
        return 1
    if not timed_out:
        print("FAIL dcbc process groups: the probe did not reach its time bound")
        return 1
    print("PASS dcbc process groups: single-child kill leaves an orphan, run_group leaves none")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", metavar="WORKDIR", required=True)
    args = parser.parse_args()
    return selftest(args.selftest)


if __name__ == "__main__":
    sys.exit(main())
