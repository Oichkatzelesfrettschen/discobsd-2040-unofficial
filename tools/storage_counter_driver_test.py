"""Verify the storage-counter driver owns its detached make environment."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "tools/verify_storage_counters.sh"


class StorageCounterDriverTest(unittest.TestCase):
    def write_tool(self, directory, name, source):
        path = directory / name
        path.write_text(source, encoding="utf-8")
        path.chmod(0o755)
        return path

    def test_detached_make_does_not_inherit_parent_jobserver_flags(self):
        with tempfile.TemporaryDirectory(prefix="discobsd-storage-driver-") as name:
            directory = Path(name)
            invocation_log = directory / "make-environment.log"
            make = self.write_tool(
                directory,
                "make",
                """#!/bin/sh
set -eu
printf 'MAKEFLAGS=%s MFLAGS=%s\\n' "${MAKEFLAGS-}" "${MFLAGS-}" >> "$STORAGE_LOG"
build=
statistics=
while [ "$#" -gt 0 ]; do
    case "$1" in
    -C)
        build=$2
        shift 2
        ;;
    STORAGE_STATS=*)
        statistics=${1#STORAGE_STATS=}
        shift
        ;;
    *)
        shift
        ;;
    esac
done
test -n "$build"
mkdir -p "$build"
printf '%s\\n' "$statistics" > "$build/unix.elf"
""",
            )
            nm = self.write_tool(
                directory,
                "nm",
                """#!/bin/sh
set -eu
if grep -qx yes "$1"; then
    printf '0 T storage_note\\n0 T storage_snapshot\\n0 T storage_dirty_sample\\n'
fi
""",
            )
            size = self.write_tool(directory, "size", "#!/bin/sh\nexit 0\n")
            environment = os.environ.copy()
            environment.update(
                MAKE=str(make),
                NM=str(nm),
                SIZE=str(size),
                PYTHON=sys.executable,
                STORAGE_LOG=str(invocation_log),
                MAKEFLAGS="-j 12 -J 98,99",
                MFLAGS="-j 12 -J 98,99",
            )

            result = subprocess.run(
                ["sh", str(DRIVER)],
                cwd=directory,
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            invocations = invocation_log.read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(invocations), 6)
            self.assertEqual(set(invocations), {"MAKEFLAGS= MFLAGS="})


if __name__ == "__main__":
    unittest.main()
