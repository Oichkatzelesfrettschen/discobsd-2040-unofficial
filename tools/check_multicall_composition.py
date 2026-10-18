"""Verify the finite DPUCP-2024 multicall and packed-root composition."""

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path


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


def validate_root(document):
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


def validate_clean_distribution_receipts(document):
    receipts = document.get("clean_distribution_receipts")
    if not isinstance(receipts, dict) or set(receipts) != {"base", "candidate"}:
        raise CompositionError("clean-distribution receipt set differs")
    expected_fields = {
        "state",
        "exit_code",
        "elapsed_seconds",
        "output_bytes",
        "terminal_queue_dropped_bytes",
        "status_sha256",
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
        if (
            not isinstance(receipt["status_sha256"], str)
            or re.fullmatch(r"[0-9a-f]{64}", receipt["status_sha256"]) is None
        ):
            raise CompositionError(
                f"invalid {receipt_name} clean-distribution receipt hash"
            )


def validate_archive_summary(root, document, map_root):
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
    for consumer in [*document["boxes"], *document.get("standalone_programs", [])]:
        members = consumer_members[consumer["id"]]
        require_equal(
            consumer.get("libc_member_count"),
            len(members),
            f"{consumer['id']} archive-member count",
        )
        if map_root is not None:
            require_equal(
                list(parse_libc_members(map_root / consumer["link_map"])),
                members,
                f"{consumer['id']} live libc members",
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


def validate_document(root, document, map_root=None):
    if document.get("schema_version") != 1:
        raise CompositionError("unsupported composition schema")
    if document.get("profile_id") != "DPUCP-2024":
        raise CompositionError("composition profile identity differs")
    claims = document.get("claims", {})
    if claims.get("posix_conformance") is not False:
        raise CompositionError("composition ledger claims POSIX conformance")
    if claims.get("unix_conformance") is not False:
        raise CompositionError("composition ledger claims UNIX conformance")

    source_hashes = document.get("source_sha256")
    if not isinstance(source_hashes, dict) or not source_hashes:
        raise CompositionError("composition source identities are absent")
    for relative_path, expected_hash in source_hashes.items():
        if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise CompositionError(f"invalid source digest for {relative_path}")
        require_equal(
            sha256_path(root / relative_path),
            expected_hash,
            f"source digest {relative_path}",
        )

    boxes = document.get("boxes")
    if not isinstance(boxes, list) or not boxes:
        raise CompositionError("composition boxes are absent")
    box_ids = [box.get("id") for box in boxes]
    installed_paths = [box.get("installed_path") for box in boxes]
    if len(box_ids) != len(set(box_ids)):
        raise CompositionError("duplicate multicall box identity")
    if len(installed_paths) != len(set(installed_paths)):
        raise CompositionError("duplicate multicall installed path")

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
    validate_archive_summary(root, document, map_root)
    validate_root(document)
    validate_clean_distribution_receipts(document)


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
