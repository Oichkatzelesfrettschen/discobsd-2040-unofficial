"""Resolve shipped artifacts through executed tools and selected archive members."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from record_tool import dependency_paths, digest, named_path

ARCHIVE_MEMBER = re.compile(r"([^\s()]+\.a)\(([^()]+)\)")


class ClosureError(ValueError):
    """Shipped dependency evidence is missing, stale or inconsistent."""


def require(condition, message):
    if not condition:
        raise ClosureError(message)


def load_json(path):
    return json.loads(path.read_text())


def source_snapshot(root):
    result = subprocess.run(["git", "-c", "core.fsmonitor=false", "-C", str(root),
                             "ls-files", "--stage", "-z"], capture_output=True, check=True)
    files = {}
    index = {}
    for entry in result.stdout.split(b"\0"):
        if not entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, object_id, stage = metadata.decode().split()
        require(stage == "0", "source snapshot contains an unresolved index entry")
        name = os.fsdecode(raw_path)
        path = root / name
        index[name] = {"mode": mode, "object": object_id}
        if mode == "120000":
            files[name] = hashlib.sha256(os.fsencode(os.readlink(path))).hexdigest()
        else:
            require(mode in {"100644", "100755"} and not path.is_symlink(),
                    "source snapshot requires regular tracked files or declared symlinks")
            files[name] = digest(path)
    commit = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                            capture_output=True, text=True, check=True).stdout.strip()
    return {"schema_version": 1, "commit": commit, "tracked_files": files,
            "index_entries": index, "created_epoch_seconds": int(time.time())}


def verify_snapshot(root, path):
    recorded = load_json(path)
    observed = source_snapshot(root)
    for field in ["schema_version", "commit", "tracked_files", "index_entries"]:
        require(recorded[field] == observed[field], "source snapshot changed: " + field)
    return recorded


def execution_receipt(root, path, expected_targets):
    receipt = load_json(path)
    require(receipt["state"] == "completed" and receipt["exit_code"] == 0,
            "build execution is incomplete or failed")
    require(receipt["terminal_queue_dropped_bytes"] == 0, "build terminal capture lost bytes")
    command = receipt["command"]
    require(Path(command[0]).name == "bmake" and
            command[1:] == ["MACHINE=rp2040", *expected_targets],
            "build receipt command differs from the required execution")
    status = (root / receipt["status_path"]).resolve()
    status.relative_to(root)
    log = (root / receipt["log_path"]).resolve()
    log.relative_to(root)
    require(load_json(status) == receipt, "immutable execution receipt differs")
    return {"receipt_sha256": digest(status), "log_sha256": digest(log),
            "started_at": receipt["started_at"], "updated_at": receipt["updated_at"],
            "invocation_id": receipt["invocation_id"]}


def manifest_artifacts(root, manifest):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from check_multicall_composition import parse_manifest

    packs, _ = parse_manifest(manifest)
    destination = "distrib/obj/destdir.rp2040"
    selections = {}
    for name in packs:
        selections["root." + name.lstrip("/").replace("/", ".")] = destination + name
    for line in manifest.read_text().splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] == "file" and fields[1].endswith((".a", ".o")):
            name = fields[1]
            key = "native." + name.lstrip("/").replace("/", ".")
            require(key not in selections, "duplicate manifest artifact owner")
            selections[key] = destination + name
    for path in selections.values():
        require((root / path).is_file(), "shipped native object or executable is absent: " + path)
    return selections


def verify_image_hardlinks(listing, links):
    """Check installed alias identity where fsutil creates the hardlinks."""
    inodes = {}
    current = None
    for line in listing.splitlines():
        if line.startswith("/"):
            current = line.split(" - ", 1)[0]
        elif line.startswith("     I-node: ") and current is not None:
            require(current not in inodes, "duplicate image inode metadata")
            inodes[current] = int(line.split(":", 1)[1].strip())
    for name, target in links.items():
        require(name in inodes and target in inodes and
                inodes[name] == inodes[target], "manifest hard-link inode differs: " + name)


class CaptureIndex:
    def __init__(self, root, records, denominator, generated,
                 expected_invocation=None, expected_snapshot=None):
        self.root = root.resolve()
        self.records = records.resolve()
        self.rows = {row["path"]: row for row in denominator["rows"]}
        require(len(self.rows) == len(denominator["rows"]), "duplicate denominator row")
        result = subprocess.run(["git", "-C", str(self.root), "ls-files", "-z"],
                                capture_output=True, check=True)
        self.tracked = set(result.stdout.decode().split("\0")) - {""}
        self.generated = generated
        self.expected_invocation = expected_invocation
        self.expected_snapshot = expected_snapshot
        self.receipts = {}
        self.by_path = {}
        self.by_hash = {}
        self.archive_cache = {}
        for path in sorted(records.glob("*.json")):
            row = load_json(path)
            require(row.get("schema_version") == 1, "unsupported capture schema")
            identity = row["id"]
            require(identity not in self.receipts, "duplicate capture identity")
            require(row.get("capture_error") is None, "capture error in receipt")
            self.receipts[identity] = row
            for output in row["outputs"]:
                self.retained(output)
                self.by_path.setdefault(output["path"], []).append((identity, output))
                self.by_hash.setdefault(output["sha256"], []).append((identity, output))
            if row.get("link_map"):
                self.retained(row["link_map"])
        require(self.receipts, "capture directory contains no receipts")

    def retained(self, record):
        name = record["retained"]
        path = (self.records / name).resolve()
        path.relative_to(self.records)
        require(path.is_file() and digest(path) == record["sha256"],
                "retained artifact is absent or changed")
        return path

    def current_path(self, name):
        if name.startswith("external:"):
            return Path(name[len("external:"):])
        path = (self.root / name).resolve()
        path.relative_to(self.root)
        return path

    def current(self, record):
        path = self.current_path(record["path"])
        require(path.is_file() and digest(path) == record["sha256"],
                "input changed since capture: " + record["path"])
        return path

    def producers(self, record, before=None):
        candidates = [pair for pair in self.by_path.get(record["path"], [])
                      if pair[1]["sha256"] == record["sha256"] and
                      (before is None or pair[0] < before)]
        if candidates:
            return [max(candidates, key=lambda pair: pair[0])]
        candidates = [pair for pair in self.by_hash.get(record["sha256"], [])
                      if before is None or pair[0] < before]
        require(candidates, "missing producer for " + record["path"])
        # Equal bytes under different names retain every possible source owner.
        return candidates

    def bound_invocation(self, row):
        if self.expected_invocation is not None:
            require(row.get("execution_invocation") == self.expected_invocation,
                    "producer belongs to another execution invocation")
        if self.expected_snapshot is not None:
            require(row.get("source_snapshot_sha256") == self.expected_snapshot,
                    "producer belongs to another source snapshot")

    def archive(self, record, before):
        label = record["path"]
        retained = self.retained(record)
        if label.startswith("external:"):
            return label, retained, None, record["sha256"]
        try:
            candidates = self.producers(record, before)
        except ClosureError as error:
            raise ClosureError("missing archive producer for " + label) from error
        identity, _ = max(candidates, key=lambda pair: pair[0])
        self.bound_invocation(self.receipts[identity])
        return label, retained, identity, record["sha256"]

    def members(self, archive_path):
        key = digest(archive_path)
        if key in self.archive_cache:
            return self.archive_cache[key]
        # The board archive uses the BSD a.out ranlib index. GNU ar treats
        # that index as its incompatible host-BSD index format.
        with archive_path.open("rb") as stream:
            header = stream.read(24)
        native_index = header == b"!<arch>\n__.SYMDEF       "
        tool = (str(self.root / "tools/bin/rp2040/ar") if native_index
                else shutil.which("arm-none-eabi-ar"))
        require(tool and Path(tool).is_file(), "archive format reader is required")
        result = subprocess.run([tool, "t", str(archive_path)], capture_output=True, check=True)
        names = [name for name in result.stdout.decode().splitlines()
                 if name != "__.SYMDEF"]
        require(names and len(names) == len(set(names)), "empty or ambiguous archive members")
        members = {}
        for name in names:
            require("/" not in name and name not in {".", ".."}, "unsafe archive member")
            result = subprocess.run([tool, "p", str(archive_path), name],
                                    capture_output=True, check=True)
            members[name] = hashlib.sha256(result.stdout).hexdigest()
        self.archive_cache[key] = members
        return members

    def configuration(self, artifacts):
        state = {"artifacts": [], "tracked_sources": set(), "tracked_inputs": set(),
                 "generated_inputs": {}, "external_inputs": {}, "commands": set(),
                 "archive_members": {}, "visited": set()}

        def input_file(record):
            label = record["path"]
            if label in self.tracked or label.startswith("external:"):
                path = self.current(record)
            else:
                path = self.current_path(label)
                if path.is_file():
                    self.current(record)
                else:
                    self.retained(record)
            if label.startswith("external:"):
                state["external_inputs"][label] = record["sha256"]
                return
            if label in self.tracked:
                state["tracked_inputs"].add(label)
                if path.suffix in {".c", ".h"}:
                    require(label in self.rows, "tracked source missing from denominator: " + label)
                    state["tracked_sources"].add(label)
                return
            if label in self.generated:
                providers = self.generated[label]
                require(providers, "generated input has no providers: " + label)
                state["generated_inputs"][label] = {"sha256": record["sha256"],
                                                   "providers": list(providers)}
                for provider in providers:
                    provider_path = self.root / provider
                    require(provider in self.tracked, "untracked generated-input provider")
                    input_file({"path": provider, "sha256": digest(provider_path)})
                return
            walk_record(record)

        def archive_member(label, archive_path, archive_identity, archive_hash, member, before):
            members = self.members(archive_path)
            require(member in members, "link map names an absent archive member")
            state["archive_members"].setdefault(label, {})[member] = members[member]
            if archive_identity is None:
                state["external_inputs"][label] = archive_hash
                return
            walk_record({"path": member, "sha256": members[member]}, before)

        def walk(identity, output):
            if identity in state["visited"]:
                return
            state["visited"].add(identity)
            row = self.receipts[identity]
            self.bound_invocation(row)
            require(row["exit_code"] == 0, "failed command reached shipped closure")
            state["commands"].add(identity)
            if row.get("dependency_file"):
                dependency_file = self.retained(row["dependency_file"])
                expected = {named_path(path, self.root) for path in dependency_paths(
                    dependency_file, self.root / row["directory"], require_exists=False)}
                actual = {record["path"] for record in row["dependencies"]}
                require(expected == actual, "dependency receipt disagrees with compiler output")
            for record in row["dependencies"]:
                input_file(record)
            for record in row["sources"]:
                if record["path"] not in {item["path"] for item in row["dependencies"]}:
                    input_file(record)
            if row.get("stdin_retained"):
                data = (self.records / row["stdin_retained"]).read_bytes()
                require(hashlib.sha256(data).hexdigest() == row["stdin_sha256"],
                        "generated stdin changed")
            if row["kind"] in {"archive", "archive-index"}:
                archive_path = self.retained(output)
                for member in self.members(archive_path):
                    archive_member(output["path"], archive_path, identity,
                                   output["sha256"], member, identity)
                return
            for record in row["inputs"]:
                if row["kind"] in {"link", "compile-link"} and record["path"].endswith(".a"):
                    continue
                if record["path"].startswith("external:"):
                    input_file(record)
                elif record["path"] in self.tracked or record["path"] in self.generated:
                    input_file(record)
                else:
                    walk_record(record, identity)
            if row.get("link_map"):
                map_path = self.retained(row["link_map"])
                members = set(ARCHIVE_MEMBER.findall(map_path.read_text(errors="replace")))
                bindings = {record["path"]: record for record in row.get("linked_archives", [])}
                for archive_name, member in sorted(members):
                    label = named_path(self.root / row["directory"] / archive_name, self.root)
                    require(label in bindings, "link archive byte identity is absent: " + label)
                    archive_info = self.archive(bindings[label], identity)
                    archive_member(*archive_info, member, identity)

        def walk_record(record, before=None):
            for identity, output in self.producers(record, before):
                walk(identity, output)

        for artifact in artifacts:
            path = self.root / artifact
            require(path.is_file(), "shipped artifact is absent: " + artifact)
            record = {"path": artifact, "sha256": digest(path)}
            state["artifacts"].append(record)
            walk_record(record)
        del state["visited"]
        for name in ["tracked_sources", "tracked_inputs", "commands"]:
            state[name] = sorted(state[name])
        state["language_open"] = [name for name in state["tracked_sources"]
                                  if self.rows[name]["language_state"] != "accepted"]
        state["generated_language_open"] = sorted(
            name for name in state["generated_inputs"] if Path(name).suffix in {".c", ".h"})
        state["source_owners"] = {name: {"owner": self.rows[name]["owner"],
                                       "risk_tier": self.rows[name]["classification"]["risk_tier"]}
                                  for name in state["tracked_sources"]}
        return state


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--records", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--source-snapshot", type=Path)
    parser.add_argument("--expected-invocation")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--clean-receipt", type=Path)
    parser.add_argument("--distribution-receipt", type=Path)
    parser.add_argument("--denominator", default="tools/c17/source-denominator.json")
    parser.add_argument("--generated-inputs", type=Path)
    parser.add_argument("--artifact", action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-language-acceptance", action="store_true")
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        if args.snapshot:
            args.snapshot.write_text(json.dumps(source_snapshot(root), indent=2) + "\n")
            print("PASS tracked source snapshot")
            return 0
        require(args.records is not None and args.output is not None,
                "closure requires records and output paths")
        snapshot = verify_snapshot(root, args.source_snapshot) if args.source_snapshot else None
        provenance = {}
        selections = list(args.artifact)
        if args.manifest:
            require(snapshot is not None and args.clean_receipt and args.distribution_receipt,
                    "shipped accounting requires source, clean and distribution receipts")
            provenance["clean"] = execution_receipt(root, args.clean_receipt,
                                                    ["clean", "cleankernel", "cleanfs"])
            provenance["distribution"] = execution_receipt(root, args.distribution_receipt,
                                                           ["distribution"])
            clean_started = datetime.fromisoformat(provenance["clean"]["started_at"].replace(
                "Z", "+00:00")).timestamp()
            clean_ended = datetime.fromisoformat(provenance["clean"]["updated_at"].replace(
                "Z", "+00:00")).timestamp()
            build_started = datetime.fromisoformat(
                provenance["distribution"]["started_at"].replace("Z", "+00:00")).timestamp()
            require(
                snapshot["created_epoch_seconds"] <= clean_started <= clean_ended <= build_started,
                "source snapshot, clean and distribution execution order differs")
            provenance["source_snapshot_sha256"] = digest(args.source_snapshot)
            provenance["source_commit"] = snapshot["commit"]
            provenance["tracked_files_sha256"] = hashlib.sha256(json.dumps(
                snapshot["tracked_files"], sort_keys=True).encode()).hexdigest()
            provenance["manifest_sha256"] = digest(args.manifest)
            sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
            from check_multicall_composition import parse_manifest
            _, links = parse_manifest(args.manifest)
            selections.extend(name + ":" + path for name, path in
                              manifest_artifacts(root, args.manifest).items())
            for configuration in ["PICO", "PICO_UART"]:
                selections.append(configuration + ":sys/arch/rp2040/compile/" +
                                  configuration + "/unix")
            image = root / "distrib/rp2040/sdcard.img"
            listing = subprocess.run([str(root / "tools/bin/rp2040/fsutil"), "-vvv",
                                      "--partition=1", str(image)], check=True,
                                     capture_output=True, text=True).stdout
            verify_image_hardlinks(listing, links)
            provenance["image_sha256"] = digest(image)
        require(selections, "select artifacts or a complete shipped manifest")
        generated = load_json(args.generated_inputs) if args.generated_inputs else {}
        expected_invocation = (provenance["distribution"]["invocation_id"]
                               if args.manifest else args.expected_invocation)
        expected_snapshot = provenance.get("source_snapshot_sha256")
        index = CaptureIndex(root, args.records, load_json(root / args.denominator), generated,
                             expected_invocation, expected_snapshot)
        grouped = {}
        for selection in selections:
            name, artifact = selection.split(":", 1)
            require(re.fullmatch(r"[A-Za-z0-9_.-]+", name), "invalid configuration name")
            path = (root / artifact).resolve()
            path.relative_to(root)
            grouped.setdefault(name, []).append(path.relative_to(root).as_posix())
        configurations = {name: index.configuration(paths)
                          for name, paths in sorted(grouped.items())}
        language_open = any(group["language_open"] or group["generated_language_open"]
                            for group in configurations.values())
        require(not args.require_language_acceptance or not language_open,
                "shipped language acceptance remains open")
        result = {"schema_version": 1, "configurations": configurations,
                  "provenance": provenance,
                  "language_acceptance": "open" if language_open else "accepted"}
        if args.source_snapshot:
            verify_snapshot(root, args.source_snapshot)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print("PASS shipped dependency accounting: " + ", ".join(
            f"{name}={len(group['tracked_sources'])} C/header paths"
            for name, group in configurations.items()))
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"FAIL shipped closure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
