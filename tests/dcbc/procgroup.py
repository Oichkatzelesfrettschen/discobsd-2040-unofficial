"""Process-group launch and leak scan for the dc and bc gate.

usage: procgroup.py --selftest WORKDIR

bc forks a dc evaluator, so a case that exceeds its time bound leaves the
fork running when only the direct child is killed: the fork is orphaned to
init and keeps spinning after the gate has exited. run_group starts the
command through tools/test_execution.py run_bounded, which makes it the
leader of a new session registered with the enclosing execution ownership
(tools/run_logged_gate.py). A timeout, SIGINT or SIGTERM of the calling
process sends SIGTERM and then SIGKILL to the whole process group before
run_group returns or exits, so neither a grandchild nor the group of a
cancelled gate outlives the case. processes_under reports every process
whose executable lies under a directory, which is how mutate.py proves that
no process of a mutant build survives its run.

--selftest calibrates the scan against both launch methods: a shell that
starts a copy of sleep in a build directory is run past its time bound once
with the single-child kill that subprocess.run applies (the scan must find
the orphan) and once through run_group (the scan must find nothing). It
then terminates a driver that is inside run_group with SIGTERM, once with
the detached launch run_group replaced (the scan must find the orphan) and
once with run_group itself (the scan must find nothing).
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

TOOLS = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "tools",
)
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)
import test_execution  # noqa: E402

# A case without a bound still ends: the longest hard bound the aggregate gate
# allows a whole run.
UNBOUNDED_SECONDS = test_execution.AGGREGATE_TIMEOUT_SECONDS


def kill_group(pid):
    """SIGKILL the process group led by pid; a group already empty is fine."""
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_group(command, cwd=None, env=None, stdin_data=None, timeout=None, executable=None):
    """Run command as an owned session leader.

    Return (returncode, stdout, stderr, timed_out) with bytes output. A
    timeout kills the whole group, so a grandchild cannot outlive the case; a
    SIGINT or SIGTERM of this process stops the group and raises SystemExit.
    """
    with tempfile.TemporaryFile() as out_file, tempfile.TemporaryFile() as err_file:
        with tempfile.TemporaryFile() as in_file:
            if stdin_data is not None:
                in_file.write(stdin_data)
                in_file.seek(0)
            returncode, timed_out = test_execution.run_owned(
                command,
                UNBOUNDED_SECONDS if timeout is None else timeout,
                cwd=cwd,
                env=env,
                stdin=in_file if stdin_data is not None else subprocess.DEVNULL,
                stdout=out_file,
                stderr=err_file,
                executable=executable,
            )
        out_file.seek(0)
        err_file.seek(0)
        return returncode, out_file.read(), err_file.read(), timed_out


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


def _legacy_detached_launch(command, timeout):
    """The launch run_group used before run_bounded: a session outside the gate's ownership.

    A timeout kills the group, but SIGTERM of this process ends it without
    reaching any cleanup, so the detached group keeps running.
    """
    proc = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_group(proc.pid)
        proc.communicate()


def _driver(mode, command):
    """Run command through the named launch method; the selftest terminates this process."""
    if mode == "legacy":
        _legacy_detached_launch(command, 60)
    else:
        run_group(command, timeout=60)
    return 0


def _terminated_driver_leaves(probe, mode, command):
    """SIGTERM a driver that is inside a launch; return processes left under probe."""
    driver = subprocess.Popen(
        [sys.executable, "-I", os.path.abspath(__file__), "--driver", mode, "--", *command],
        stdin=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + 10.0
        while not processes_under(probe):
            if time.monotonic() > deadline or driver.poll() is not None:
                return None
            time.sleep(0.02)
        driver.send_signal(signal.SIGTERM)
        driver.wait(timeout=10.0)
        time.sleep(0.5)
        return processes_under(probe)
    finally:
        if driver.poll() is None:
            driver.kill()
            driver.wait()
        kill_processes_under(probe)
        _wait_gone(probe)


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

    legacy_left = _terminated_driver_leaves(probe, "legacy", command)
    if not legacy_left:
        print("FAIL dcbc process groups: the scan missed the group that outlived a SIGTERM "
              "of the detached launch")
        return 1
    owned_left = _terminated_driver_leaves(probe, "owned", command)
    if owned_left is None:
        print("FAIL dcbc process groups: the run_group driver never started its case")
        return 1
    if owned_left:
        print("FAIL dcbc process groups: SIGTERM of the run_group caller left "
              "%d process(es) running" % len(owned_left))
        return 1
    print("PASS dcbc process groups: single-child kill leaves an orphan, run_group leaves none, "
          "and SIGTERM of the caller leaves none where the detached launch leaves one")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", metavar="WORKDIR")
    parser.add_argument("--driver", choices=("legacy", "owned"))
    parser.add_argument("command", nargs="*")
    args = parser.parse_args()
    if args.driver:
        return _driver(args.driver, args.command)
    if not args.selftest:
        parser.error("--selftest WORKDIR is required")
    return selftest(args.selftest)


if __name__ == "__main__":
    sys.exit(main())
