"""Calibrate the documented board workload without executing board commands."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("shells", nargs="+", type=Path)
args = parser.parse_args()
text = (args.root / "docs/research/board-capacity-acceptance.md").read_text()
workload = text.split("shell workload for the stdio portion is:\n\n```sh\n")[1].split("```", 1)[0]
expected = [
    "sysctl kern.capacity",
    "sysctl machdep.storage_stats",
    "stdiocap trap",
    "stdiocap leaf",
]
expected += [
    f"stdiocap {operation} {length}"
    for operation in ("fprintf", "vfprintf", "fputs")
    for length in (0, 1, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025, 2049)
]
expected += ["sysctl kern.capacity", "sync ", "sysctl machdep.storage_stats"]
with tempfile.TemporaryDirectory(prefix="capacity-workload-") as temporary:
    directory = Path(temporary)
    for command in ("sysctl", "stdiocap", "sync", "printf", "echo"):
        executable = directory / command
        executable.write_text("""#!/bin/sh
set -eu
label="${0##*/} $*"
echo "$label" >> "$TRACE"
call_index=0
while IFS= read -r recorded_call; do
    call_index=$((call_index + 1))
done < "$TRACE"
if [ "$call_index" -eq "$FAIL_INDEX" ]; then
    exit 5
fi
exit 0
""")
        executable.chmod(0o755)

    def verify(shell, script):
        # Each invocation is a failure control, including repeated snapshots.
        for failure in range(len(expected) + 1):
            trace = directory / "trace"
            trace.write_text("")
            result = subprocess.run(
                [str(shell.resolve()), "-c", script],
                text=True,
                capture_output=True,
                env=dict(
                    os.environ, PATH=str(directory), TRACE=str(trace), FAIL_INDEX=str(failure)
                ),
            )
            calls = trace.read_text().splitlines()
            limit = failure if failure else len(expected)
            assert calls == expected[:limit], (shell, failure, calls)
            assert result.returncode == (1 if failure else 0), (shell, failure, result.returncode)
            assert result.stdout.count("status=") == limit, (shell, failure, result.stdout)
            if failure:
                assert "status=5 command=" in result.stdout, (shell, failure, result.stdout)

    for shell in args.shells:
        verify(shell, workload)
        mutants = [
            workload.replace(" || exit 1", ""),
            workload.replace(
                'echo "status=$command_status command= $*"',
                '"$PATH/printf" "status=%s command= %s\\n" "$command_status" "$*"',
            ),
        ]
        for mutant in mutants:
            assert mutant != workload
            try:
                verify(shell, mutant)
            except AssertionError:
                continue
            raise AssertionError((shell, "workload mutant escapes"))
print(
    f"workload: {len(args.shells)} shells pass all command failures; "
    "continuation/external-output mutants fail"
)
