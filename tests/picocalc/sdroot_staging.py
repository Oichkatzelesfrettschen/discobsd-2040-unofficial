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

import os
import re
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


def check_links(fsutil, work):
    tree = work / "members"
    tree.mkdir()
    (tree / "first").write_bytes(b"linked payload")
    os.link(tree / "first", tree / "second")
    os.link(tree / "first", tree / "explicit")
    (tree / "copy").write_bytes(b"linked payload")
    (tree / "symbolic").symlink_to("first")
    base = work / "base"
    base.write_text("file /explicit\nmode 0600\n")
    manifest = work / "composed"
    generate = [sys.executable, "distrib/rp2040/mksdmanifest.py", base, tree, manifest]
    subprocess.run(generate, check=True, capture_output=True)
    composed = manifest.read_bytes()
    subprocess.run(generate, check=True, capture_output=True)
    if manifest.read_bytes() != composed:
        raise ValueError("the same install tree produces a different manifest")
    image = work / "members.img"
    subprocess.run(
        [fsutil, "--new", "--size=1024", f"--manifest={manifest}", image, tree],
        check=True,
        capture_output=True,
    )
    listing = subprocess.run(
        [fsutil, "-vvv", image], check=True, text=True, capture_output=True
    ).stdout
    numbers = {}
    for name in ("first", "second", "copy", "explicit", "symbolic"):
        match = re.search(rf"^/{name} - [^\n]*\n[^\n]*\n +I-node: (\d+)$", listing, re.M)
        if not match:
            raise ValueError(f"missing image inode for {name}")
        numbers[name] = int(match[1])
    if numbers["first"] != numbers["second"] or len(set(numbers.values())) != 4:
        raise ValueError(f"installed inode identity differs: {numbers}")
    subprocess.run([fsutil, "--check", image], check=True, capture_output=True)


def check_overlay(work):
    """stage-sdroot.sh adds overlay files and refuses to replace or link."""
    tree = work / "install"
    (tree / "etc").mkdir(parents=True)
    (tree / "etc" / "motd").write_text("install tree\n")
    good = work / "good"
    (good / "usr" / "share").mkdir(parents=True)
    (good / "usr" / "share" / "note").write_text("overlay\n")
    (good / "etc").mkdir()
    (good / "etc" / "extra").write_text("beside motd\n")
    stage = work / "stage"
    command = ["sh", "tools/picocalc/stage-sdroot.sh", tree, stage]
    subprocess.run([*command, good], check=True, capture_output=True)
    staged = {
        str(path.relative_to(stage)): path.read_text()
        for path in sorted(stage.rglob("*"))
        if path.is_file()
    }
    expected = {
        "etc/extra": "beside motd\n",
        "etc/motd": "install tree\n",
        "usr/share/note": "overlay\n",
    }
    if staged != expected:
        raise ValueError(f"staged tree differs: {staged}")
    if sorted(p.name for p in (tree / "etc").iterdir()) != ["motd"]:
        raise ValueError("the overlay reached the install tree")
    replacing = work / "replacing"
    (replacing / "etc").mkdir(parents=True)
    (replacing / "etc" / "motd").write_text("replaced\n")
    linking = work / "linking"
    linking.mkdir()
    (linking / "escape").symlink_to("/etc/passwd")
    blocking = work / "blocking"
    blocking.mkdir()
    (blocking / "etc").write_text("a file where the root has a directory\n")
    cases = (
        (replacing, "would replace a file of the root"),
        (linking, "is not a regular file or a directory"),
        (blocking, "would replace a file of the root"),
    )
    for overlay, reason in cases:
        result = subprocess.run([*command, good, overlay], capture_output=True, text=True)
        if result.returncode == 0 or reason not in result.stderr:
            raise ValueError(f"{overlay.name}: accepted or wrong reason: {result.stderr}")
        if (stage / "etc" / "motd").read_text() != "install tree\n":
            raise ValueError(f"{overlay.name}: a refused overlay changed the stage")
        if list(work.glob("stage.new.*")):
            raise ValueError(f"{overlay.name}: a refused overlay left its scratch copy")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        check(sys.argv[1], Path(directory))
        check_links(sys.argv[1], Path(directory))
        check_overlay(Path(directory))
    print("PASS SD root staging and image preservation")
