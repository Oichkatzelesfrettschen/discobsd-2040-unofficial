"""Verify the finite DPUCP-2024 multicall and packed-root composition."""

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

from measurement_source_identity import RetainedSourceError, validate_retained_sources


class CompositionError(ValueError):
    """The composition ledger disagrees with its source or arithmetic."""


ARCHIVE_MEMBER_PATTERN = re.compile(r"(?:^|[\s/])libc\.a\(([^()\s]+)\)")


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CompositionError(f"cannot read {path}: {error}") from error


def sha256_path(path):
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(65536), b""):
                digest.update(chunk)
    except OSError as error:
        raise CompositionError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def parse_libc_members(path):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise CompositionError(f"cannot read link map {path}: {error}") from error
    members = {
        f"libc.a({match.group(1)})"
        for match in ARCHIVE_MEMBER_PATTERN.finditer(text)
    }
    if not members:
        raise CompositionError(f"link map {path} names no libc archive members")
    return tuple(sorted(members))


def validate_live_archive_members(
    retained_members, live_members, exact, label="live libc members"
):
    if not live_members:
        raise CompositionError("live link map names no libc archive members")
    if exact:
        require_equal(live_members, retained_members, label)
    retained_set = set(retained_members)
    live_set = set(live_members)
    return (
        tuple(sorted(live_set - retained_set)),
        tuple(sorted(retained_set - live_set)),
    )


def parse_inventory(path):
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CompositionError(f"cannot read archive inventory {path}: {error}") from error
    if not lines or lines[0] != "member\tconsumers":
        raise CompositionError("archive inventory header differs")
    inventory = {}
    for line_number, line in enumerate(lines[1:], 2):
        fields = line.split("\t")
        if len(fields) != 2 or not fields[0] or not fields[1]:
            raise CompositionError(f"invalid archive inventory row {line_number}")
        member = fields[0]
        consumers = tuple(fields[1].split(","))
        if consumers != tuple(sorted(set(consumers))):
            raise CompositionError(f"archive consumers differ at row {line_number}")
        if member in inventory:
            raise CompositionError(f"duplicate archive member {member}")
        inventory[member] = consumers
    if list(inventory) != sorted(inventory):
        raise CompositionError("archive inventory order differs")
    return inventory


def parse_size_inventory(path):
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CompositionError(f"cannot read size inventory {path}: {error}") from error
    expected_header = (
        "kind\tbox\tprogram\tpath\traw_bytes\tpacked_bytes\traw_blocks"
        "\tpacked_blocks"
    )
    if not lines or lines[0] != expected_header:
        raise CompositionError("size inventory header differs")
    records = []
    identities = set()
    for line_number, line in enumerate(lines[1:], 2):
        fields = line.split("\t")
        if len(fields) != 8:
            raise CompositionError(f"invalid size inventory row {line_number}")
        kind, box_id, program, path = fields[:4]
        if kind not in {"multicall", "standalone"}:
            raise CompositionError(f"invalid size kind at row {line_number}")
        if not box_id or not program or not path or Path(path).is_absolute():
            raise CompositionError(f"invalid size identity at row {line_number}")
        identity = (kind, box_id, program)
        if identity in identities:
            raise CompositionError(f"duplicate size identity at row {line_number}")
        identities.add(identity)
        values = []
        for field_name, value in zip(
            ("raw_bytes", "packed_bytes", "raw_blocks", "packed_blocks"),
            fields[4:],
        ):
            try:
                parsed = int(value, 10)
            except ValueError as error:
                raise CompositionError(
                    f"invalid {field_name} at size row {line_number}"
                ) from error
            if parsed <= 0:
                raise CompositionError(
                    f"nonpositive {field_name} at size row {line_number}"
                )
            values.append(parsed)
        records.append(
            {
                "kind": kind,
                "box": box_id,
                "program": program,
                "path": path,
                "raw_bytes": values[0],
                "packed_bytes": values[1],
                "raw_blocks": values[2],
                "packed_blocks": values[3],
            }
        )
    return records


def measure_hsaout(root, tool_path, records):
    command = [str(tool_path), "-s"]
    command.extend(str(root / record["path"]) for record in records)
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise CompositionError(f"hsaout measurement failed: {result.stderr.strip()}")
    lines = result.stdout.splitlines()
    if not lines or lines[0].split() != ["file", "raw", "packed", "rblk", "pblk"]:
        raise CompositionError("hsaout measurement header differs")
    if len(lines) != len(records) + 1:
        raise CompositionError("hsaout measurement row count differs")
    measured = []
    for line_number, line in enumerate(lines[1:], 2):
        fields = line.split()
        if len(fields) < 5:
            raise CompositionError(f"malformed hsaout row {line_number}")
        try:
            values = tuple(int(value, 10) for value in fields[-4:])
        except ValueError as error:
            raise CompositionError(f"malformed hsaout row {line_number}") from error
        measured.append(values)
    return measured


def validate_live_measurements(records, boxes, measured, exact):
    if len(measured) != len(records):
        raise CompositionError("live hsaout measurement row count differs")
    live_by_identity = {}
    for record, values in zip(records, measured):
        if (
            not isinstance(values, tuple)
            or len(values) != 4
            or any(
                not isinstance(value, int) or isinstance(value, bool) or value <= 0
                for value in values
            )
        ):
            raise CompositionError(
                f"{record['program']} live hsaout measurement is invalid"
            )
        identity = (record["kind"], record["box"], record["program"])
        live_by_identity[identity] = values
        if exact:
            require_equal(
                values,
                (
                    record["raw_bytes"],
                    record["packed_bytes"],
                    record["raw_blocks"],
                    record["packed_blocks"],
                ),
                f"{record['program']} live hsaout measurement",
            )
    for box in boxes:
        box_id = box["id"]
        multicall_blocks = live_by_identity[("multicall", box_id, box_id)][3]
        standalone_blocks = sum(
            live_by_identity[("standalone", box_id, program)][3]
            for program in box.get("tools", [])
        )
        if multicall_blocks >= standalone_blocks:
            raise CompositionError(
                f"{box_id} live composition saves no packed blocks: "
                f"{multicall_blocks} against {standalone_blocks}"
            )


def command_version(command, label):
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise CompositionError(f"cannot read {label} version: {result.stderr.strip()}")
    lines = result.stdout.splitlines()
    if not lines or not lines[0].strip():
        raise CompositionError(f"cannot read {label} version")
    return lines[0].strip()


def measurement_environment_matches(summary):
    observed_arm_cc = command_version(
        ["arm-none-eabi-gcc", "--version"], "ARM compiler"
    )
    observed_arm_ld = command_version(
        ["arm-none-eabi-ld", "--version"], "ARM linker"
    )
    observed_bmake = command_version(
        ["bmake", "-V", "MAKE_VERSION"], "bmake"
    )
    exact = (
        observed_arm_cc == summary["arm_cc_version"]
        and observed_arm_ld == summary["arm_ld_version"]
        and observed_bmake == summary["bmake_version"]
    )
    return exact, observed_arm_cc, observed_arm_ld, observed_bmake


def capture_inventory(document, map_root):
    consumers = [box["id"] for box in document["boxes"]]
    consumers.extend(program["id"] for program in document["standalone_programs"])
    member_consumers = {}
    for consumer in consumers:
        for member in parse_libc_members(map_root / f"{consumer}.map"):
            member_consumers.setdefault(member, []).append(consumer)
    lines = ["member\tconsumers"]
    for member in sorted(member_consumers):
        lines.append(f"{member}\t{','.join(sorted(member_consumers[member]))}")
    return "\n".join(lines) + "\n"


def parse_manifest(path):
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CompositionError(f"cannot read manifest {path}: {error}") from error
    packs = []
    links = {}
    line_index = 0
    while line_index < len(lines):
        fields = lines[line_index].split()
        if not fields or fields[0].startswith("#"):
            line_index += 1
            continue
        if fields[0] == "pack" and len(fields) == 2:
            packs.append(fields[1])
        elif fields[0] == "link" and len(fields) == 2:
            source = fields[1]
            line_index += 1
            if line_index >= len(lines):
                raise CompositionError(f"manifest link {source} lacks a target")
            target_fields = lines[line_index].split()
            if len(target_fields) != 2 or target_fields[0] != "target":
                raise CompositionError(f"manifest link {source} lacks a target")
            if source in links:
                raise CompositionError(f"duplicate manifest link {source}")
            links[source] = target_fields[1]
        line_index += 1
    if len(packs) != len(set(packs)):
        raise CompositionError("manifest contains duplicate packed executables")
    return tuple(packs), links


def read_make_variable(root, box, variable):
    directory = root / box["makefile"]
    if directory.name != "Makefile":
        raise CompositionError(f"invalid Makefile path for {box['id']}")
    command = [
        "bmake",
        "-C",
        str(directory.parent),
        "MACHINE=rp2040",
        "-V",
        variable,
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        diagnostic = result.stderr.strip()
        raise CompositionError(
            f"cannot evaluate {variable} for {box['id']}: {diagnostic}"
        )
    return result.stdout.strip().split()


def require_equal(actual, expected, label):
    if actual != expected:
        raise CompositionError(f"{label}: expected {expected!r}, got {actual!r}")


def validate_root(document, links, receipts):
    root = document.get("root")
    if not isinstance(root, dict):
        raise CompositionError("missing root accounting")
    partition_blocks = root.get("partition_blocks")
    inode_blocks = root.get("inode_list_blocks")
    for state in ("base", "candidate"):
        record = root.get(state, {})
        if (
            record.get("data_blocks", -1)
            + record.get("free_blocks", -1)
            + inode_blocks
            != partition_blocks
        ):
            raise CompositionError(f"{state} packed-root arithmetic differs")
    base = root["base"]
    candidate = root["candidate"]
    delta = root.get("delta", {})
    for field in ("files", "links", "data_blocks", "free_blocks"):
        require_equal(
            delta.get(field),
            candidate.get(field) - base.get(field),
            f"root delta {field}",
        )
    require_equal(candidate.get("links"), len(links), "candidate manifest links")
    if candidate.get("data_blocks", -1) > base.get("data_blocks", -1):
        raise CompositionError("candidate packed root grew")
    if candidate.get("files", -1) > base.get("files", -1):
        raise CompositionError("candidate inode use grew")
    for state in ("base", "candidate"):
        receipt = receipts[state]
        image = receipt["root_image"]
        manifest = receipt["manifest"]
        require_equal(root[state]["links"], manifest["hard_links"], f"{state} receipt links")
        require_equal(
            root[state]["files"],
            image["inode_capacity"] - image["free_inodes"],
            f"{state} receipt inode use",
        )
        require_equal(
            root[state]["data_blocks"],
            image["data_blocks"],
            f"{state} receipt data blocks",
        )
        require_equal(
            root[state]["free_blocks"],
            image["free_blocks"],
            f"{state} receipt free blocks",
        )
        require_equal(
            partition_blocks,
            image["partition_blocks"],
            f"{state} receipt partition blocks",
        )
        require_equal(
            inode_blocks,
            image["inode_list_blocks"],
            f"{state} receipt inode-list blocks",
        )


def validate_receipt_payload(document, receipt_name, payload):
    expected_fields = {
        "schema_version",
        "evidence_class",
        "source_commit",
        "command",
        "state",
        "exit_code",
        "elapsed_seconds",
        "output_bytes",
        "terminal_queue_dropped_bytes",
        "manifest",
        "root_image",
        "last_output",
    }
    if not isinstance(payload, dict) or set(payload) != expected_fields:
        raise CompositionError(f"malformed retained {receipt_name} receipt")
    if payload["schema_version"] != 1 or payload["evidence_class"] != "cross-build":
        raise CompositionError(f"invalid retained {receipt_name} receipt identity")
    expected_commit = document[f"{receipt_name}_commit"]
    require_equal(payload["source_commit"], expected_commit, f"{receipt_name} receipt commit")
    if (
        not isinstance(payload["command"], list)
        or not payload["command"]
        or payload["command"][0] != "bmake"
    ):
        raise CompositionError(f"invalid retained {receipt_name} command")
    if not isinstance(payload["last_output"], str) or not payload["last_output"]:
        raise CompositionError(f"missing retained {receipt_name} terminal output")
    manifest = payload["manifest"]
    if not isinstance(manifest, dict) or set(manifest) != {"path", "sha256", "hard_links"}:
        raise CompositionError(f"malformed retained {receipt_name} manifest")
    if (
        manifest["path"] != "distrib/rp2040/mi.rp2040"
        or re.fullmatch(r"[0-9a-f]{64}", manifest["sha256"]) is None
        or manifest["sha256"] == "0" * 64
        or not isinstance(manifest["hard_links"], int)
        or isinstance(manifest["hard_links"], bool)
        or manifest["hard_links"] <= 0
    ):
        raise CompositionError(f"invalid retained {receipt_name} manifest")
    image = payload["root_image"]
    image_fields = {
        "path",
        "sha256",
        "partition",
        "partition_blocks",
        "inode_list_blocks",
        "inode_capacity",
        "free_inodes",
        "data_blocks",
        "free_blocks",
    }
    if not isinstance(image, dict) or set(image) != image_fields:
        raise CompositionError(f"malformed retained {receipt_name} root image")
    if (
        image["path"] != "distrib/rp2040/sdcard.img"
        or re.fullmatch(r"[0-9a-f]{64}", image["sha256"]) is None
        or image["sha256"] == "0" * 64
    ):
        raise CompositionError(f"invalid retained {receipt_name} image identity")
    for field in image_fields - {"path", "sha256"}:
        if (
            not isinstance(image[field], int)
            or isinstance(image[field], bool)
            or image[field] < 0
        ):
            raise CompositionError(f"invalid retained {receipt_name} image {field}")
    if image["partition"] != 1:
        raise CompositionError(f"invalid retained {receipt_name} image partition")
    require_equal(
        image["data_blocks"] + image["free_blocks"] + image["inode_list_blocks"],
        image["partition_blocks"],
        f"{receipt_name} retained image arithmetic",
    )


def validate_clean_distribution_receipts(root, document):
    receipts = document.get("clean_distribution_receipts")
    if not isinstance(receipts, dict) or set(receipts) != {"base", "candidate"}:
        raise CompositionError("clean-distribution receipt set differs")
    expected_fields = {
        "state",
        "exit_code",
        "elapsed_seconds",
        "output_bytes",
        "terminal_queue_dropped_bytes",
        "receipt_path",
        "receipt_sha256",
    }
    for receipt_name in ("base", "candidate"):
        receipt = receipts[receipt_name]
        if not isinstance(receipt, dict) or set(receipt) != expected_fields:
            raise CompositionError(
                f"malformed {receipt_name} clean-distribution receipt"
            )
        if receipt["state"] != "completed" or receipt["exit_code"] != 0:
            raise CompositionError(
                f"{receipt_name} clean distribution did not complete"
            )
        if (
            not isinstance(receipt["elapsed_seconds"], (int, float))
            or isinstance(receipt["elapsed_seconds"], bool)
            or receipt["elapsed_seconds"] <= 0
        ):
            raise CompositionError(
                f"invalid {receipt_name} clean-distribution elapsed time"
            )
        if (
            not isinstance(receipt["output_bytes"], int)
            or isinstance(receipt["output_bytes"], bool)
            or receipt["output_bytes"] <= 0
        ):
            raise CompositionError(
                f"invalid {receipt_name} clean-distribution output count"
            )
        if (
            not isinstance(receipt["terminal_queue_dropped_bytes"], int)
            or isinstance(receipt["terminal_queue_dropped_bytes"], bool)
            or receipt["terminal_queue_dropped_bytes"] != 0
        ):
            raise CompositionError(
                f"dropped output in {receipt_name} clean-distribution receipt"
            )
        if not isinstance(receipt["receipt_path"], str):
            raise CompositionError(f"invalid {receipt_name} receipt path")
        receipt_path = (root / receipt["receipt_path"]).resolve()
        if not receipt_path.is_relative_to(root):
            raise CompositionError(f"invalid {receipt_name} receipt path")
        if (
            not isinstance(receipt["receipt_sha256"], str)
            or re.fullmatch(r"[0-9a-f]{64}", receipt["receipt_sha256"]) is None
            or receipt["receipt_sha256"] == "0" * 64
        ):
            raise CompositionError(
                f"invalid {receipt_name} clean-distribution receipt hash"
            )
        require_equal(
            sha256_path(receipt_path),
            receipt["receipt_sha256"],
            f"{receipt_name} retained receipt digest",
        )
        payload = load_json(receipt_path)
        validate_receipt_payload(document, receipt_name, payload)
        for field in (
            "state",
            "exit_code",
            "elapsed_seconds",
            "output_bytes",
            "terminal_queue_dropped_bytes",
        ):
            require_equal(receipt[field], payload[field], f"{receipt_name} receipt {field}")
    return {
        receipt_name: load_json((root / receipts[receipt_name]["receipt_path"]).resolve())
        for receipt_name in ("base", "candidate")
    }


def validate_measurement_inventory(root, document, boxes, live):
    summary = document.get("measurement_inventory", {})
    expected_fields = {
        "path",
        "sha256",
        "hsaout_path",
        "captured_hsaout_sha256",
        "command_pattern",
        "arm_cc_version",
        "arm_ld_version",
        "bmake_version",
    }
    if not isinstance(summary, dict) or set(summary) != expected_fields:
        raise CompositionError("measurement inventory metadata differs")
    inventory_path = root / summary["path"]
    require_equal(sha256_path(inventory_path), summary["sha256"], "size inventory digest")
    records = parse_size_inventory(inventory_path)
    expected_identities = []
    for box in boxes:
        expected_identities.append(("multicall", box["id"], box["id"]))
        expected_identities.extend(
            ("standalone", box["id"], program) for program in box.get("tools", [])
        )
    identities = [(record["kind"], record["box"], record["program"]) for record in records]
    require_equal(identities, expected_identities, "size inventory identities")
    records_by_identity = {
        (record["kind"], record["box"], record["program"]): record
        for record in records
    }
    for box in boxes:
        multicall = records_by_identity[("multicall", box["id"], box["id"])]
        expected_path = str(Path(box["makefile"]).parent / box["id"])
        require_equal(multicall["path"], expected_path, f"{box['id']} measured path")
        for field in ("raw_bytes", "packed_bytes", "raw_blocks", "packed_blocks"):
            require_equal(box.get(field), multicall[field], f"{box['id']} {field}")
        standalone_records = [
            records_by_identity[("standalone", box["id"], program)]
            for program in box.get("tools", [])
        ]
        require_equal(
            box.get("standalone_primary_packed_blocks"),
            sum(record["packed_blocks"] for record in standalone_records),
            f"{box['id']} standalone measured blocks",
        )
    tool_path = root / summary["hsaout_path"]
    if (
        re.fullmatch(r"[0-9a-f]{64}", summary["captured_hsaout_sha256"]) is None
        or summary["captured_hsaout_sha256"] == "0" * 64
    ):
        raise CompositionError("invalid captured hsaout digest")
    if live:
        measured = measure_hsaout(root, tool_path, records)
        exact, observed_arm_cc, observed_arm_ld, observed_bmake = (
            measurement_environment_matches(summary)
        )
        validate_live_measurements(records, boxes, measured, exact)
        return records, exact, observed_arm_cc, observed_arm_ld, observed_bmake
    return (
        records,
        True,
        summary["arm_cc_version"],
        summary["arm_ld_version"],
        summary["bmake_version"],
    )


def measure_root_image(fsutil_path, image_path):
    command = [str(fsutil_path), "--verbose", "--partition=1", str(image_path)]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise CompositionError(f"root image measurement failed: {result.stderr.strip()}")
    patterns = {
        "partition_blocks": r"^\s*Volume size:\s+(\d+) blocks$",
        "inode_list_blocks": r"^\s*Inode list size:\s+(\d+) blocks$",
        "free_blocks": r"^\s*Total free blocks:\s+(\d+) blocks$",
        "free_inodes": r"^\s*Total free inodes:\s+(\d+) inodes$",
    }
    values = {}
    for field, pattern in patterns.items():
        matches = re.findall(pattern, result.stdout, re.MULTILINE)
        if len(matches) != 1:
            raise CompositionError(f"root image {field} measurement differs")
        values[field] = int(matches[0], 10)
    values["data_blocks"] = (
        values["partition_blocks"]
        - values["inode_list_blocks"]
        - values["free_blocks"]
    )
    return values


def validate_live_root_measurement(document, retained, measured, exact):
    for field in (
        "partition_blocks",
        "inode_list_blocks",
        "free_blocks",
        "free_inodes",
        "data_blocks",
    ):
        value = measured.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise CompositionError(f"candidate live root {field} is invalid")
    require_equal(
        measured["partition_blocks"],
        document["root"]["partition_blocks"],
        "candidate live root partition_blocks",
    )
    require_equal(
        measured["inode_list_blocks"],
        document["root"]["inode_list_blocks"],
        "candidate live root inode_list_blocks",
    )
    require_equal(
        measured["data_blocks"] + measured["free_blocks"],
        measured["partition_blocks"] - measured["inode_list_blocks"],
        "candidate live root block arithmetic",
    )
    if measured["free_blocks"] == 0 or measured["free_inodes"] == 0:
        raise CompositionError("candidate live root has no free capacity")
    if exact:
        for field in (
            "partition_blocks",
            "inode_list_blocks",
            "free_blocks",
            "free_inodes",
            "data_blocks",
        ):
            require_equal(measured[field], retained[field], f"candidate live root {field}")


def validate_root_measurement(root, document, candidate_receipt, live, exact):
    summary = document.get("root_measurement", {})
    expected_fields = {
        "fsutil_path",
        "captured_fsutil_sha256",
        "command_pattern",
    }
    if not isinstance(summary, dict) or set(summary) != expected_fields:
        raise CompositionError("root measurement metadata differs")
    fsutil_path = root / summary["fsutil_path"]
    if (
        re.fullmatch(r"[0-9a-f]{64}", summary["captured_fsutil_sha256"]) is None
        or summary["captured_fsutil_sha256"] == "0" * 64
    ):
        raise CompositionError("invalid captured fsutil digest")
    if live:
        image = candidate_receipt["root_image"]
        measured = measure_root_image(fsutil_path, root / image["path"])
        validate_live_root_measurement(document, image, measured, exact)
    manifest = candidate_receipt["manifest"]
    require_equal(
        sha256_path(root / manifest["path"]),
        manifest["sha256"],
        "candidate retained manifest digest",
    )


def validate_archive_summary(root, document, map_root, exact):
    summary = document.get("archive_summary", {})
    inventory_path = root / summary.get("inventory_path", "")
    require_equal(
        sha256_path(inventory_path),
        summary.get("inventory_sha256"),
        "archive inventory digest",
    )
    inventory = parse_inventory(inventory_path)
    member_consumers = Counter()
    consumer_members = {}
    for member, consumers in inventory.items():
        member_consumers[member] = len(consumers)
        for consumer in consumers:
            consumer_members.setdefault(consumer, []).append(member)
    expected_consumers = sorted(
        [box["id"] for box in document["boxes"]]
        + [program["id"] for program in document.get("standalone_programs", [])]
    )
    require_equal(sorted(consumer_members), expected_consumers, "archive consumers")
    live_deltas = []
    for consumer in [*document["boxes"], *document.get("standalone_programs", [])]:
        members = consumer_members[consumer["id"]]
        require_equal(
            consumer.get("libc_member_count"),
            len(members),
            f"{consumer['id']} archive-member count",
        )
        if map_root is not None:
            live_members = parse_libc_members(map_root / consumer["link_map"])
            added_members, removed_members = validate_live_archive_members(
                tuple(members), live_members, exact,
                f"{consumer['id']} live libc members",
            )
            if added_members or removed_members:
                live_deltas.append(
                    (consumer["id"], added_members, removed_members)
                )
    shared = sorted(
        member for member, consumer_count in member_consumers.items()
        if consumer_count > 1
    )
    unique = sorted(
        member for member, consumer_count in member_consumers.items()
        if consumer_count == 1
    )
    require_equal(
        summary.get("consumer_edges"),
        sum(member_consumers.values()),
        "archive consumer edges",
    )
    require_equal(
        summary.get("distinct_members"),
        len(member_consumers),
        "distinct archive members",
    )
    require_equal(summary.get("shared_members"), len(shared), "shared archive members")
    require_equal(summary.get("single_consumer_members"), len(unique), "single-consumer members")
    return live_deltas


def validate_document(root, document, map_root=None):
    if document.get("schema_version") != 1:
        raise CompositionError("unsupported composition schema")
    if document.get("profile_id") != "DPUCP-2024":
        raise CompositionError("composition profile identity differs")
    for commit_field in ("base_commit", "candidate_commit"):
        if re.fullmatch(r"[0-9a-f]{40}", document.get(commit_field, "")) is None:
            raise CompositionError(f"invalid composition {commit_field}")
    claims = document.get("claims", {})
    if claims.get("posix_conformance") is not False:
        raise CompositionError("composition ledger claims POSIX conformance")
    if claims.get("unix_conformance") is not False:
        raise CompositionError("composition ledger claims UNIX conformance")

    source_hashes = document.get("source_sha256")
    if not isinstance(source_hashes, dict) or not source_hashes:
        raise CompositionError("composition source identities are absent")
    try:
        changed_sources = validate_retained_sources(
            root, source_hashes, document.get("retained_source_commit"))
    except RetainedSourceError as error:
        raise CompositionError(str(error)) from error

    boxes = document.get("boxes")
    if not isinstance(boxes, list) or not boxes:
        raise CompositionError("composition boxes are absent")
    box_ids = [box.get("id") for box in boxes]
    installed_paths = [box.get("installed_path") for box in boxes]
    if len(box_ids) != len(set(box_ids)):
        raise CompositionError("duplicate multicall box identity")
    if len(installed_paths) != len(set(installed_paths)):
        raise CompositionError("duplicate multicall installed path")
    _, exact_measurement, observed_arm_cc, observed_arm_ld, observed_bmake = (
        validate_measurement_inventory(root, document, boxes, map_root is not None)
    )

    packs, links = parse_manifest(root / "distrib/rp2040/mi.rp2040")
    shipped_paths = []
    dispatch_count = 0
    primary_executables = 0
    standalone_blocks = 0
    multicall_blocks = 0
    for box in boxes:
        expected_variables = {
            "PROG": [box["id"]],
            "TOOLS": box.get("tools", []),
            "BUILTIN_TOOLS": box.get("builtin_tools", []),
            "ALIASES": box.get("aliases", []),
        }
        for variable, expected in expected_variables.items():
            require_equal(
                read_make_variable(root, box, variable),
                expected,
                f"{box['id']} {variable}",
            )
        makefile_text = (root / box["makefile"]).read_text(encoding="utf-8")
        if "${LINK_MAP_FLAG}" not in makefile_text:
            raise CompositionError(f"{box['id']} link does not consume LINK_MAP_FLAG")
        require_equal(box.get("link_map"), f"{box['id']}.map", f"{box['id']} map name")
        primary_executables += len(box.get("tools", []))
        standalone_blocks += box.get("standalone_primary_packed_blocks", -1)
        multicall_blocks += box.get("packed_blocks", -1)
        require_equal(
            box.get("packed_block_savings"),
            box.get("standalone_primary_packed_blocks", -1)
            - box.get("packed_blocks", -1),
            f"{box['id']} packed-block savings",
        )
        if box["installed_path"] not in packs:
            raise CompositionError(f"{box['id']} is not a packed manifest executable")
        alias_names = [alias.split(":", 1)[0] for alias in box.get("aliases", [])]
        if any(":" not in alias for alias in box.get("aliases", [])):
            raise CompositionError(f"{box['id']} has an invalid alias")
        dispatch_names = (
            box.get("tools", []) + box.get("builtin_tools", []) + alias_names
        )
        if len(dispatch_names) != len(set(dispatch_names)):
            raise CompositionError(f"{box['id']} has duplicate dispatch names")
        dispatch_count += len(dispatch_names)
        shipped = box.get("shipped_names")
        if not isinstance(shipped, list) or shipped != sorted(set(shipped)):
            raise CompositionError(f"{box['id']} shipped names are not sorted and unique")
        shipped_basenames = [Path(path).name for path in shipped]
        if len(shipped_basenames) != len(set(shipped_basenames)):
            raise CompositionError(f"{box['id']} repeats a shipped basename")
        unknown_names = sorted(set(shipped_basenames) - set(dispatch_names))
        if unknown_names:
            raise CompositionError(
                f"{box['id']} ships names outside dispatch: {', '.join(unknown_names)}"
            )
        expected_unshipped = sorted(set(dispatch_names) - set(shipped_basenames))
        require_equal(
            box.get("unshipped_dispatch_names"),
            expected_unshipped,
            f"{box['id']} unshipped dispatch names",
        )
        manifest_shipped = sorted(
            source for source, target in links.items()
            if target == box["installed_path"]
        )
        require_equal(shipped, manifest_shipped, f"{box['id']} manifest links")
        shipped_paths.extend(shipped)

    if len(shipped_paths) != len(set(shipped_paths)):
        raise CompositionError("a shipped name belongs to multiple multicall boxes")
    denominator = document.get("denominator", {})
    require_equal(denominator.get("packed_executables"), len(packs), "packed executables")
    require_equal(denominator.get("manifest_hard_links"), len(links), "manifest hard links")
    require_equal(denominator.get("multicall_executables"), len(boxes), "multicall executables")
    require_equal(
        denominator.get("multicall_hard_links"),
        len(shipped_paths),
        "multicall hard links",
    )
    require_equal(denominator.get("dispatch_names"), dispatch_count, "dispatch names")
    comparison = document.get("standalone_comparison", {})
    require_equal(
        comparison.get("primary_executables"),
        primary_executables,
        "standalone primary executable count",
    )
    require_equal(
        comparison.get("standalone_packed_blocks"),
        standalone_blocks,
        "standalone packed blocks",
    )
    require_equal(
        comparison.get("multicall_packed_blocks"),
        multicall_blocks,
        "multicall packed blocks",
    )
    require_equal(
        comparison.get("saved_packed_blocks"),
        standalone_blocks - multicall_blocks,
        "saved packed blocks",
    )
    shell_makefile = (root / "bin/sh/Makefile").read_text(encoding="utf-8")
    if "${LINK_MAP_FLAG}" not in shell_makefile:
        raise CompositionError("shell link does not consume LINK_MAP_FLAG")
    archive_deltas = validate_archive_summary(
        root, document, map_root, exact_measurement
    )
    receipts = validate_clean_distribution_receipts(root, document)
    validate_root(document, links, receipts)
    validate_root_measurement(
        root,
        document,
        receipts["candidate"],
        map_root is not None,
        exact_measurement,
    )
    if map_root is not None:
        print(f"multicall composition: {len(changed_sources)} live source differences from "
              "the retained source subset; live composition/resource checks ran separately")
    if map_root is not None and not exact_measurement:
        for consumer, added_members, removed_members in archive_deltas:
            print(
                "multicall composition: cross-toolchain libc member delta "
                f"{consumer}: added={list(added_members)!r}, "
                f"removed={list(removed_members)!r}"
            )
        print(
            "multicall composition: live toolchain differs from retained "
            f"measurement: ARM compiler={observed_arm_cc!r}, "
            f"ARM linker={observed_arm_ld!r}, "
            f"bmake={observed_bmake!r}; validated live link-map accounting, "
            "block savings, and root capacity without claiming exact "
            "archive-member or byte replay"
        )


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--ledger", type=Path)
    parser.add_argument("--map-root", type=Path)
    parser.add_argument("--capture-inventory", type=Path)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    root = arguments.root.resolve()
    ledger = arguments.ledger or (
        root / "docs/research/dpucp-2024-multicall-composition.json"
    )
    try:
        document = load_json(ledger)
        if arguments.capture_inventory is not None:
            if arguments.map_root is None:
                raise CompositionError("--capture-inventory requires --map-root")
            arguments.capture_inventory.write_text(
                capture_inventory(document, arguments.map_root), encoding="utf-8"
            )
            return 0
        validate_document(root, document, arguments.map_root)
    except CompositionError as error:
        print(f"multicall composition: {error}")
        return 1
    print(
        "multicall composition: "
        f"{document['denominator']['multicall_executables']} boxes, "
        f"{document['denominator']['multicall_hard_links']} shipped names, "
        f"{document['archive_summary']['distinct_members']} libc members"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
