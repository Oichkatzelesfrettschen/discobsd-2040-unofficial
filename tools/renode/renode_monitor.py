"""Drive one Renode process through its console monitor on stdin.

`renode --console --plain --port -1` reads monitor commands from stdin and
writes their output, and the emulator's log, to stdout. No socket listens
anywhere, so a host that refuses listening sockets runs the gates that use
this module. Each command is followed by an `echo` of a serial-numbered
marker, and the command's output is everything the log gained before the
marker came back.

Exit-status convention shared by the drivers: 0 pass, 1 an assertion
failed, 2 the emulator did not answer.

`python3 renode_monitor.py --self-test` drives a stand-in that has already
exited and checks that every call reports RenodeError.
"""

import os
import re
import subprocess
import sys
import tempfile
import time

# One line of Renode's log: a bracketed timestamp, a bracketed level, the
# message and its newline. It can begin anywhere in a monitor line.
LOG_LINE = re.compile(r"\[\d\d:\d\d:\d\d\.\d+\] \[[A-Z]+\][^\n]*\n")


class RenodeError(Exception):
    """The emulator stopped answering."""


class Renode:
    """One Renode process driven through its console on stdin."""

    def __init__(self, renode: str, log_path: str, wall_seconds: float) -> None:
        self.log_path = log_path
        self.log = open(log_path, "wb")
        self.proc = subprocess.Popen(
            [renode, "--disable-gui", "--console", "--plain", "--port", "-1"],
            stdin=subprocess.PIPE, stdout=self.log, stderr=subprocess.STDOUT)
        self.serial = 0
        self.deadline = time.monotonic() + wall_seconds

    def _text(self) -> str:
        with open(self.log_path, encoding="utf-8", errors="replace") as handle:
            return handle.read()

    def command(self, line: str) -> str:
        """Send one monitor line and return the output it produced."""
        self.serial += 1
        mark = f"@@done-{self.serial}"
        before = len(self._text())
        if self.proc.stdin is None:
            raise RenodeError("Renode has no stdin")
        try:
            self.proc.stdin.write(f'{line}\necho "{mark}"\n'.encode())
            self.proc.stdin.flush()
        except OSError as error:
            # The pipe is closed once Renode has exited; the exit status is
            # the cause worth reporting, so it is reaped for a moment first.
            try:
                status = self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                status = None
            raise RenodeError(
                f"Renode stopped reading its monitor (exit status {status}, {error}) "
                f"during: {line}") from error
        pattern = re.compile(r"^\r*" + re.escape(mark) + r"\s*$", re.M)
        while True:
            # Renode writes its log to the same stdout from another thread,
            # so a log line can land inside the echoed marker ("@@do[...]
            # Machine paused.\nne-61"); the log lines are removed before
            # the marker is matched, and from the output returned.
            text = LOG_LINE.sub("", self._text()[before:])
            found = pattern.search(text)
            if found:
                return text[:found.start()]
            if self.proc.poll() is not None:
                raise RenodeError(f"Renode exited with {self.proc.returncode} during: {line}")
            if time.monotonic() > self.deadline:
                raise RenodeError(f"wall-clock budget spent during: {line}")
            time.sleep(0.2)

    def close(self) -> None:
        try:
            self.command("quit")
        except RenodeError:
            pass
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        if self.proc.stdin is not None:
            try:
                self.proc.stdin.close()
            except OSError:
                # A buffered command that never reached an exited Renode.
                pass
        self.log.close()


def monitor_int(renode: Renode, line: str) -> int:
    """Read an integer property; the monitor prints it as 0x-prefixed hex."""
    out = renode.command(line).strip().splitlines()
    for text in reversed(out):
        try:
            return int(text.strip(), 0)
        except ValueError:
            continue
    return -1


def self_test() -> int:
    """Calibrate the dead-subprocess path: RenodeError, never BrokenPipeError.

    The stand-in "renode" exits with status 3 before the first command, so the
    monitor pipe has no reader. command() and close() must both turn that into
    RenodeError (the drivers answer it with status 2) and close() must leave
    the process reaped.
    """
    with tempfile.TemporaryDirectory() as scratch:
        fake = os.path.join(scratch, "renode")
        with open(fake, "w", encoding="ascii") as handle:
            handle.write("#!/bin/sh\nexit 3\n")
        os.chmod(fake, 0o755)
        renode = Renode(fake, os.path.join(scratch, "renode.log"), 10.0)
        renode.proc.wait(timeout=10)
        try:
            renode.command("version")
        except RenodeError as error:
            if "3" not in str(error):
                print(f"self-test: the error omits the exit status: {error}", file=sys.stderr)
                return 1
        except Exception as error:  # the old behavior was BrokenPipeError
            print(f"self-test: command() raised {type(error).__name__}, not RenodeError",
                  file=sys.stderr)
            return 1
        else:
            print("self-test: command() succeeded against an exited Renode", file=sys.stderr)
            return 1
        try:
            renode.close()
        except Exception as error:
            print(f"self-test: close() raised {type(error).__name__}: {error}", file=sys.stderr)
            return 1
    print("renode_monitor self-test: ok (command() and close() against an exited Renode)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    print(__doc__, file=sys.stderr)
    sys.exit(2)
