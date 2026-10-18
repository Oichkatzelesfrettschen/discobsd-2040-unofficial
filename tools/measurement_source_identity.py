"""Bind retained measurement subsets to Git blobs and report live differences."""

import hashlib
import re
import subprocess
from pathlib import PurePosixPath


class RetainedSourceError(ValueError):
    """A retained source identity is unsafe, unavailable or inconsistent."""


def require(condition, message):
    if not condition:
        raise RetainedSourceError(message)


def validate_retained_sources(root, hashes, revision):
    require(isinstance(revision, str) and re.fullmatch(r"[0-9a-f]{40}", revision),
            "invalid retained source revision")
    changed = []
    for relative_path, expected_hash in sorted(hashes.items()):
        require(isinstance(relative_path, str) and relative_path and "\0" not in relative_path,
                "unsafe measured source path")
        path = PurePosixPath(relative_path)
        require(not path.is_absolute() and ".." not in path.parts,
                "unsafe measured source path")
        require(isinstance(expected_hash, str) and re.fullmatch(r"[0-9a-f]{64}", expected_hash),
                "invalid retained source digest")
        result = subprocess.run(["git", "-C", str(root), "cat-file", "blob",
                                 revision + ":" + relative_path], capture_output=True,
                                check=False)
        require(result.returncode == 0, "retained source object unavailable: " + relative_path)
        require(hashlib.sha256(result.stdout).hexdigest() == expected_hash,
                "retained source digest differs: " + relative_path)
        source_path = root / relative_path
        require(source_path.resolve().is_relative_to(root.resolve()),
                "measured source path escapes checkout")
        observed = (hashlib.sha256(source_path.read_bytes()).hexdigest()
                    if source_path.is_file() else None)
        if observed != expected_hash:
            changed.append(relative_path)
    return changed
