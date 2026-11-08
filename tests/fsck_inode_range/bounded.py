"""Run a command under a time bound as an owned process group.

usage: PYTHON bounded.py SECONDS COMMAND [ARG...]

The command runs through run_owned of tools/test_execution.py, which starts
it as the leader of its own session and stops the whole group before
returning, so neither a timeout nor a SIGINT or SIGTERM of this process
leaves a child of the command running. The exit status is the command's own;
128 plus the signal number when the command died of a signal; 124 when the
bound ended it, as timeout(1) reports; 127 and 126 when the command is
missing or cannot be executed, as sh(1) reports; and the signal status of
this process when it was itself interrupted.
"""

import os
import sys

TOOLS = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "tools",
)
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import test_execution  # noqa: E402

TIMEOUT_STATUS = 124
NOT_EXECUTABLE_STATUS = 126
NOT_FOUND_STATUS = 127


def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: bounded.py SECONDS COMMAND [ARG...]\n")
        return 2
    seconds = float(argv[1])
    try:
        returncode, timed_out = test_execution.run_owned(argv[2:], seconds)
    except FileNotFoundError as error:
        sys.stderr.write("bounded.py: %s\n" % error)
        return NOT_FOUND_STATUS
    except OSError as error:
        sys.stderr.write("bounded.py: %s\n" % error)
        return NOT_EXECUTABLE_STATUS
    if timed_out:
        return TIMEOUT_STATUS
    if returncode < 0:
        return 128 - returncode
    return returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
