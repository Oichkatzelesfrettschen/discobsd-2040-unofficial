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
"""Check SD root staging and preservation of the last accepted image."""

import subprocess
import sys
import tempfile
from pathlib import Path


def check(fsutil, work):
    root = work / "root"
    etc = root / "etc"
    etc.mkdir(parents=True)
    (etc / "fstab").write_text("initial fstab\n")
    (etc / "ttys").write_text("tty0 getty off secure\n")
    manifest = work / "manifest"
    manifest.write_text("dir /etc\ndir /dev\nfile /etc/fstab\nfile /etc/ttys\n")
    image = work / "sd.img"
    command = ["sh", "tools/picocalc/mksdroot.sh", fsutil, root, manifest, "1024", "a", image]
    subprocess.run(command, check=True, capture_output=True)
    accepted = image.read_bytes()
    if len(accepted) != 1024 * 1024:
        raise ValueError("accepted image has the wrong extent")
    for path in (etc / "fstab", etc / "ttys", etc):
        saved = path.with_suffix(".saved")
        path.rename(saved)
        path.symlink_to(saved.name, target_is_directory=saved.is_dir())
        result = subprocess.run(command, capture_output=True, text=True)
        expected = (
            "staged root and etc must be directories"
            if path == etc
            else f"staged etc/{path.name} must be a regular file"
        )
        if result.returncode == 0 or expected not in result.stderr:
            raise ValueError(f"{path.name}: staging type check failed: {result.stderr}")
        if image.read_bytes() != accepted:
            raise ValueError("rejected input changes the accepted image")
        path.unlink()
        saved.rename(path)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        check(sys.argv[1], Path(directory))
    print("PASS SD root staging and image preservation")
