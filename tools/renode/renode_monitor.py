"""Drive one Renode process through its console monitor on stdin.

`renode --console --plain --port -1` reads monitor commands from stdin and
writes their output, and the emulator's log, to stdout. No socket listens
anywhere, so a host that refuses listening sockets runs the gates that use
this module. Each command is followed by an `echo` of a serial-numbered
marker, and the command's output is everything the log gained before the
marker came back.

Exit-status convention shared by the drivers: 0 pass, 1 an assertion
failed, 2 the emulator did not answer.
"""

import re
import subprocess
import time


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
        self.proc.stdin.write(f'{line}\necho "{mark}"\n'.encode())
        self.proc.stdin.flush()
        pattern = re.compile(r"^" + re.escape(mark) + r"\s*$", re.M)
        while True:
            text = self._text()
            found = pattern.search(text, before)
            if found:
                return text[before:found.start()]
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
