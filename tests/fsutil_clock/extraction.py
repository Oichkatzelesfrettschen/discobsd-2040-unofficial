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
"""Check the host extractor with existing files, fresh directories and sparse data."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def check(tool, work):
    source = work / "source"
    source.mkdir()
    payload = bytes(6 * 1024) + b"end"
    (source / "body").write_bytes(payload)
    manifest = work / "manifest"
    manifest.write_text("file /body\nmode 0444\ndir /child\nsymlink /child/name\ntarget ../body\n")
    image = work / "filesystem"
    subprocess.run(
        [tool, "--new", "--size=1024", f"--manifest={manifest}", image, source],
        check=True,
        capture_output=True,
    )
    dest = work / "output"
    dest.mkdir()
    (dest / "child").mkdir()
    extract = [tool, "--extract", image]
    subprocess.run(extract, cwd=dest, check=True)
    if (dest / "body").read_bytes() != payload or os.readlink(dest / "child/name") != "../body":
        raise ValueError("fresh extraction changes file bytes or symlink text")
    (dest / "body").write_bytes(b"keep")
    os.link(dest / "body", dest / "peer")
    result = subprocess.run(extract, cwd=dest, capture_output=True)
    if result.returncode == 0 or b"body" not in result.stderr:
        raise ValueError("existing regular file must refuse extraction")
    if (dest / "peer").read_bytes() != b"keep" or not (dest / "body").samefile(dest / "peer"):
        raise ValueError("collision changes an existing host file")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        check(str(Path(sys.argv[1]).resolve()), Path(directory))
    print("PASS fsutil extraction, sparse reads and collision preservation")
