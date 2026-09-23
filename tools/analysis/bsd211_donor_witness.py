"""Retain and verify Git-object provenance for selected donor changes."""

import argparse
import base64
import binascii
import gzip
import hashlib
import io
import json
import re
import struct
import subprocess
import zlib
from collections import deque
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate witness key: {key}")
        result[key] = value
    return result


def canonical_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def git_object_id(kind, contents):
    header = f"{kind} {len(contents)}\0".encode("ascii")
    return hashlib.sha1(header + contents, usedforsecurity=False).hexdigest()


def git_object(repository, identifier):
    kind_result = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "-t", identifier],
        capture_output=True, check=True,
    )
    kind = kind_result.stdout.decode("ascii").strip()
    require(kind in {"commit", "tree", "blob"}, "unsupported donor Git object")
    contents = subprocess.run(
        ["git", "-C", str(repository), "cat-file", kind, identifier],
        capture_output=True, check=True,
    ).stdout
    require(git_object_id(kind, contents) == identifier,
            f"donor Git object mismatch: {identifier}")
    return kind, contents


def commit_headers(contents):
    header = contents.split(b"\n\n", 1)[0]
    tree_match = re.search(rb"(?m)^tree ([0-9a-f]{40})$", header)
    require(tree_match is not None, "donor commit lacks tree")
    parents = re.findall(rb"(?m)^parent ([0-9a-f]{40})$", header)
    return tree_match[1].decode("ascii"), [parent.decode("ascii") for parent in parents]


def tree_entries(contents):
    position = 0
    entries = {}
    while position < len(contents):
        separator = contents.find(b" ", position)
        terminator = contents.find(b"\0", separator + 1)
        require(separator > position and terminator > separator and
                terminator + 21 <= len(contents), "malformed donor tree")
        mode = contents[position:separator]
        name = contents[separator + 1:terminator]
        require(name and name not in entries, "duplicate donor tree entry")
        entries[name] = (mode, contents[terminator + 1:terminator + 21].hex())
        position = terminator + 21
    return entries


def original_paths(data):
    paths = set()
    for row in data["fixes"]:
        for number in row["patches"]:
            for source in row["original"]:
                paths.add((data["patches"][number]["commit"], source["path"]))
    return paths


def make_witness(data, repository):
    tip = data["donor"]["commit"]
    targets = {patch["commit"] for patch in data["patches"].values()}
    predecessors = {tip: None}
    queue = deque([tip])
    commits = {}
    while queue and targets - commits.keys():
        identifier = queue.popleft()
        kind, contents = git_object(repository, identifier)
        require(kind == "commit", "donor ancestry contains a non-commit")
        commits[identifier] = contents
        for parent in commit_headers(contents)[1]:
            if parent not in predecessors:
                predecessors[parent] = identifier
                queue.append(parent)
        require(len(predecessors) <= 2000, "donor ancestry search exceeded bound")
    require(not targets - commits.keys(), "selected patch is outside donor ancestry")
    selected = {tip}
    for target in targets:
        node = target
        while node is not None:
            selected.add(node)
            node = predecessors[node]
    selected.update(patch["parent"] for patch in data["patches"].values())
    objects = {}

    def retain(identifier, expected_kind):
        kind, contents = git_object(repository, identifier)
        require(kind == expected_kind, f"donor object type mismatch: {identifier}")
        objects[identifier] = {"type": kind,
                               "data": base64.b64encode(contents).decode("ascii")}
        return contents

    for identifier in selected:
        retain(identifier, "commit")

    def retain_path(commit, path):
        commit_contents = retain(commit, "commit")
        tree_identifier = commit_headers(commit_contents)[0]
        components = path.split("/")
        for index, component in enumerate(components):
            entries = tree_entries(retain(tree_identifier, "tree"))
            require(component.encode() in entries,
                    f"donor tree lacks {path}")
            mode, child_identifier = entries[component.encode()]
            if index == len(components) - 1:
                require(mode in {b"100644", b"100755"},
                        f"donor source is not a regular file: {path}")
                retain(child_identifier, "blob")
            else:
                require(mode == b"40000", f"donor path component is not a tree: {path}")
                tree_identifier = child_identifier

    retain_path(tip, data["donor"]["ledger_path"])
    for commit, path in original_paths(data):
        retain_path(commit, path)
    return {"schema_version": 1, "donor_commit": tip, "objects": objects}


def archive_bytes(witness):
    contents = canonical_bytes(witness)
    stream = io.BytesIO()
    stream.write(b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff")
    for offset in range(0, len(contents), 65535):
        block = contents[offset:offset + 65535]
        stream.write(bytes((int(offset + len(block) == len(contents)),)))
        stream.write(struct.pack("<HH", len(block), len(block) ^ 0xffff))
        stream.write(block)
    stream.write(struct.pack("<II", zlib.crc32(contents), len(contents) & 0xffffffff))
    return stream.getvalue()


def verify_witness(data, archive):
    try:
        contents = gzip.decompress(archive)
        witness = json.loads(contents, object_pairs_hook=unique_object)
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError("invalid donor witness archive") from error
    require(canonical_bytes(witness) == contents, "noncanonical donor witness")
    require(archive_bytes(witness) == archive, "nondeterministic donor witness archive")
    require(set(witness) == {"schema_version", "donor_commit", "objects"},
            "unexpected donor witness fields")
    require(witness["schema_version"] == 1 and
            witness["donor_commit"] == data["donor"]["commit"],
            "donor witness identity mismatch")
    objects = witness["objects"]
    used = set()

    def object_contents(identifier, expected_kind):
        require(identifier in objects, f"missing donor object: {identifier}")
        record = objects[identifier]
        require(set(record) == {"type", "data"},
                f"unexpected donor object fields: {identifier}")
        require(record["type"] == expected_kind, f"donor object type mismatch: {identifier}")
        try:
            raw = base64.b64decode(record["data"], validate=True)
        except (binascii.Error, ValueError) as error:
            raise ValueError(f"invalid donor object encoding: {identifier}") from error
        require(git_object_id(expected_kind, raw) == identifier,
                f"donor object ID mismatch: {identifier}")
        used.add(identifier)
        return raw

    tip = data["donor"]["commit"]
    reachable = set()
    pending = [tip]
    while pending:
        identifier = pending.pop()
        if identifier in reachable:
            continue
        commit = object_contents(identifier, "commit")
        reachable.add(identifier)
        pending.extend(parent for parent in commit_headers(commit)[1]
                       if parent in objects)
    for number, patch in data["patches"].items():
        identifier = patch["commit"]
        require(identifier in reachable, f"patch {number} is outside donor ancestry")
        _, parents = commit_headers(object_contents(identifier, "commit"))
        require(parents and parents[0] == patch["parent"],
                f"patch {number} parent mismatch")
        object_contents(patch["parent"], "commit")

    def path_blob(commit_identifier, path):
        commit = object_contents(commit_identifier, "commit")
        tree_identifier = commit_headers(commit)[0]
        for index, component in enumerate(path.split("/")):
            entries = tree_entries(object_contents(tree_identifier, "tree"))
            require(component.encode() in entries,
                    f"donor tree lacks {path}")
            mode, child = entries[component.encode()]
            if index == len(path.split("/")) - 1:
                require(mode in {b"100644", b"100755"},
                        f"donor source is not a regular file: {path}")
                return object_contents(child, "blob")
            require(mode == b"40000", f"donor path component is not a tree: {path}")
            tree_identifier = child
        raise ValueError("empty donor path")

    ledger_contents = path_blob(tip, data["donor"]["ledger_path"])
    require(hashlib.sha256(ledger_contents).hexdigest() ==
            data["donor"]["ledger_sha256"], "donor patch ledger hash mismatch")
    for number, patch in data["patches"].items():
        pattern = re.compile(
            rb"(?m)^\| " + number.encode() +
            rb" \| ([0-9a-f]{7}) \| [^|]* \| `([0-9a-f]{64})` \|"
        )
        match = pattern.search(ledger_contents)
        require(match is not None, f"patch {number} is absent from donor ledger")
        require(patch["commit"].startswith(match[1].decode("ascii")) and
                patch["raw_patch_sha256"] == match[2].decode("ascii"),
                f"patch {number} donor ledger attestation mismatch")
    for row in data["fixes"]:
        for number in row["patches"]:
            for source in row["original"]:
                contents = path_blob(data["patches"][number]["commit"], source["path"])
                require(source["symbol"].encode() in contents,
                        f"{row['id']}: donor source symbol is absent")
    require(set(objects) == used, "unused donor witness object")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--ledger", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    data = json.loads(arguments.ledger.read_bytes(), object_pairs_hook=unique_object)
    witness = make_witness(data, arguments.donor)
    archive = archive_bytes(witness)
    verify_witness(data, archive)
    with arguments.output.open("xb") as destination:
        destination.write(archive)
    print(f"donor witness sha256={hashlib.sha256(archive).hexdigest()}")


if __name__ == "__main__":
    main()
