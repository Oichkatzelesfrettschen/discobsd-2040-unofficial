# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Check the release specification, Python geometry, and an AND-to-OR mutant."""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lean", default="lean")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "distrib/rp2040/host/src"))
    from discobsd_host.release import HEADER, RECORD

    if (HEADER.size, RECORD.size, HEADER.size + 5 * RECORD.size) != (74, 77, 459):
        raise SystemExit("Python geometry no longer matches the proved specification")
    source = Path(__file__).with_name("ReleasePolicy.lean")
    subprocess.run([args.lean, str(source)], check=True, timeout=60)
    text = source.read_text()
    if text.count("classical && pq") != 1:
        raise SystemExit("mutation anchor changed")
    with tempfile.TemporaryDirectory(prefix="discobsd-proof-") as folder:
        mutant = Path(folder) / "Mutant.lean"
        mutant.write_text(text.replace("classical && pq", "classical || pq"))
        result = subprocess.run(
            [args.lean, str(mutant)], capture_output=True, text=True, timeout=60
        )
        if (
            result.returncode == 0
            or "unsolved goals" not in result.stdout
            or "⊢ b = true" not in result.stdout
        ):
            raise SystemExit("AND-to-OR mutant did not fail the required theorem")
    print("Lean policy accepted; Python geometry matches; OR-policy mutant rejected")


if __name__ == "__main__":
    main()
