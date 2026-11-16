"""Record and verify the actual inputs of a compiler-matrix reconstruction."""

import argparse
import hashlib
import json
import os
import stat
import subprocess
import sys
from pathlib import Path


def git(root, *arguments):
    return subprocess.run(
        ["git", "-c", "core.fsmonitor=false", *arguments], cwd=root,
        capture_output=True, check=True,
    ).stdout


def relative_input(root, name):
    path = Path(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"input must be a relative path: {name}")
    parent = (root / path).parent.resolve(strict=True)
    if not parent.is_relative_to(root):
        raise ValueError(f"input escapes reconstruction: {name}")
    return parent / path.name


def file_identity(path, object_format=None):
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode):
        payload = os.fsencode(os.readlink(path))
        kind = "symlink"
    elif stat.S_ISREG(metadata.st_mode):
        payload = path.read_bytes()
        kind = "file"
    else:
        raise ValueError(f"input is not a regular file or symlink: {path}")
    identity = {"kind": kind, "mode": stat.S_IMODE(metadata.st_mode),
                "sha256": hashlib.sha256(payload).hexdigest()}
    if object_format is not None:
        object_hash = hashlib.new(object_format)
        object_hash.update(f"blob {len(payload)}\0".encode("ascii"))
        object_hash.update(payload)
        identity["git_blob"] = object_hash.hexdigest()
    return identity


def artifact_hashes(directory):
    """Hash the complete regular-file contents of one captured matrix row."""
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"matrix artifact is a symlink: {path}")
        if path.is_file():
            result[path.relative_to(directory).as_posix()] = hashlib.sha256(
                path.read_bytes()
            ).hexdigest()
        elif not path.is_dir():
            raise ValueError(f"matrix artifact has an unsupported type: {path}")
    return result


def verify_artifacts(directory, expected):
    if not isinstance(expected, dict) or artifact_hashes(directory) != expected:
        raise ValueError(f"matrix row artifacts differ: {directory}")


def snapshot(root, generated_inputs, host_tools):
    root = root.resolve(strict=True)
    if Path(os.fsdecode(git(root, "rev-parse", "--show-toplevel").strip())).resolve() != root:
        raise ValueError("root must be the top of its Git working tree")
    index = git(root, "ls-files", "--stage", "-z")
    object_format = os.fsdecode(git(root, "rev-parse", "--show-object-format").strip())
    tracked = {}
    for entry in index.split(b"\0"):
        if not entry:
            continue
        header, name = entry.split(b"\t", 1)
        mode, object_id, stage = header.split(b" ")
        decoded = os.fsdecode(name)
        if stage != b"0" or mode not in (b"100644", b"100755", b"120000"):
            raise ValueError(f"unsupported or unmerged index entry: {decoded}")
        identity = file_identity(relative_input(root, decoded), object_format)
        expected_kind = "symlink" if mode == b"120000" else "file"
        executable = bool(identity["mode"] & 0o111)
        if (identity["git_blob"] != object_id.decode("ascii") or
                identity["kind"] != expected_kind or
                (expected_kind == "file" and executable != (mode == b"100755"))):
            raise ValueError(f"tracked file differs from index: {decoded}")
        tracked[decoded] = identity
    generated = {name: file_identity(relative_input(root, name))
                 for name in generated_inputs}
    tools = {}
    for name in host_tools:
        path = relative_input(root, name)
        identity = file_identity(path)
        if identity["kind"] != "file" or not os.access(path, os.X_OK):
            raise ValueError(f"host tool is not an executable regular file: {name}")
        tools[name] = identity
    return {"schema": 1,
            "head": os.fsdecode(git(root, "rev-parse", "HEAD").strip()),
            "index_sha256": hashlib.sha256(index).hexdigest(),
            "tracked": tracked, "generated": generated, "host_tools": tools}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--generated-input", action="append", default=[])
    parser.add_argument("--host-tool", action="append", default=[])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--record-only", action="store_true")
    action.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        current = snapshot(args.root, args.generated_input, args.host_tool)
        if args.record_only:
            with args.manifest.open("x", encoding="utf-8") as stream:
                json.dump(current, stream, sort_keys=True, indent=2)
                stream.write("\n")
        else:
            expected = json.loads(args.manifest.read_text(encoding="utf-8"))
            if current != expected:
                raise ValueError("compiler-matrix inputs differ from the manifest")
    except (OSError, ValueError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        print(f"compiler-matrix identity: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
