"""Check the selected per-fix ledger's provenance joins, not semantic truth."""

import argparse
import base64
import binascii
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

from bsd211_donor_witness import verify_witness
from bsd211_regression_bindings import validate as validate_bindings
from sanitize_bsd211_execution_report import canonical_bytes, reject_absolute_strings

ROOT = Path(__file__).resolve().parents[2]
LEDGER = "docs/research/211bsd-semantic-ledger.json"
DISPOSITIONS = {"present", "equivalent", "missing", "superseded", "not-built",
                "not-applicable", "unresolved"}


class InfrastructureError(Exception):
    """A pinned Git object is unavailable or unreadable."""


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load(path):
    return json.loads(path.read_text(), object_pairs_hook=unique_object)


def load_blob(contents, label):
    try:
        return json.loads(contents, object_pairs_hook=unique_object)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f"{label}: invalid JSON") from error


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value, length, label):
    require(isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{length}}}", value),
            f"{label}: expected {length}-digit digest")


def relative_path(value):
    require(isinstance(value, str) and value and "\\" not in value,
            "expected repository-relative path")
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts and
            str(path) == value, f"unsafe or noncanonical path: {value}")
    return value


def blob(root, revision, path):
    result = subprocess.run(["git", "-C", str(root), "show", f"{revision}:{path}"],
                            capture_output=True, check=False)
    if result.returncode:
        raise InfrastructureError(result.stderr.decode(errors="replace").strip())
    return result.stdout


def tree(root, revision):
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--verify", f"{revision}^{{tree}}"],
        capture_output=True, check=False,
    )
    if result.returncode:
        raise InfrastructureError(result.stderr.decode(errors="replace").strip())
    object_id = result.stdout.decode("ascii").strip()
    digest(object_id, 40, f"{revision} tree")
    return object_id


def retained_commit_tree(encoded, expected_commit, expected_sha256):
    try:
        contents = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("executed commit object is not valid base64") from error
    require(hashlib.sha256(contents).hexdigest() == expected_sha256,
            "executed commit object SHA-256 mismatch")
    header = f"commit {len(contents)}\0".encode("ascii")
    object_id = hashlib.sha1(header + contents, usedforsecurity=False).hexdigest()
    require(object_id == expected_commit, "executed commit object ID mismatch")
    match = re.match(rb"tree ([0-9a-f]{40})\n", contents)
    require(match is not None, "executed commit object lacks a tree header")
    return match[1].decode("ascii")


def summary(data):
    lines = ["| Mechanism | Disposition | Validation |", "| --- | --- | --- |"]
    lines.extend(f"| `{row['id']}` | {row['disposition']} | {row['validation']} |"
                 for row in data["fixes"])
    return "\n".join(lines)


def check_summary(data, contents):
    markers = ("<!-- semantic-ledger-summary:start -->", "<!-- semantic-ledger-summary:end -->")
    require(all(contents.count(marker) == 1 for marker in markers),
            "summary marker count mismatch")
    projection = contents.split(markers[0], 1)[1].split(markers[1], 1)[0].strip()
    require(projection == summary(data), "summary differs from canonical rows")


def validate(data, evidence, report_bytes, witness_archive, bindings_bytes,
             read_blob, read_tree):
    require(data["schema_version"] == 1, "unsupported ledger schema")
    require(data["complete_numbered_series"] is False,
            "selected ledger cannot claim complete numbered-series coverage")
    require(bool(data["scope"].strip()), "missing scope")
    digest(data["recipient_commit"], 40, "recipient commit")
    digest(data["donor"]["commit"], 40, "donor commit")
    digest(data["donor"]["ledger_sha256"], 64, "donor ledger hash")
    digest(data["donor"]["witness_sha256"], 64, "donor witness hash")
    relative_path(data["donor"]["witness_path"])
    require(hashlib.sha256(witness_archive).hexdigest() ==
            data["donor"]["witness_sha256"], "donor witness archive hash mismatch")
    require(data["donor"]["raw_hash_authority"].startswith("donor-ledger attestation"),
            "raw patch hashes require their donor-ledger qualification")
    relative_path(data["donor"]["ledger_path"])
    for number, patch in data["patches"].items():
        require(number.isdecimal(), "patch number must be decimal")
        for field in ("commit", "parent"):
            digest(patch[field], 40, f"patch {number} {field}")
        digest(patch["raw_patch_sha256"], 64, f"patch {number} raw hash")
    for row in data["fixes"]:
        for source in row["original"]:
            relative_path(source["path"])
    verify_witness(data, witness_archive)
    relative_path(data["regression_bindings"])
    digest(data["regression_bindings_sha256"], 64, "regression binding hash")
    require(hashlib.sha256(bindings_bytes).hexdigest() ==
            data["regression_bindings_sha256"], "regression binding hash mismatch")
    bindings = load_blob(bindings_bytes, "regression bindings")

    identifiers = data["declared_ids"]
    require(len(identifiers) == len(set(identifiers)), "duplicate declared id")
    rows = {row["id"]: row for row in data["fixes"]}
    require(len(rows) == len(data["fixes"]), "duplicate fix id")
    require(set(identifiers) == set(rows), "declared/fix key-set mismatch")
    receipts = {receipt["variant"]: receipt for receipt in evidence["variants"]}
    require(len(receipts) == len(evidence["variants"]), "duplicate execution receipt")
    digest(evidence["revision"], 40, "execution revision")
    digest(evidence["original_report_sha256"], 64, "execution report hash")
    digest(evidence["sanitized_report_sha256"], 64, "sanitized report hash")
    relative_path(evidence["sanitized_report"])
    require(evidence["transformation_version"] == 1,
            "unsupported execution report transformation")
    require(hashlib.sha256(report_bytes).hexdigest() == evidence["sanitized_report_sha256"],
            "sanitized execution report hash mismatch")
    report = load_blob(report_bytes, "sanitized execution report")
    require(canonical_bytes(report) == report_bytes,
            "sanitized execution report is not canonical")
    reject_absolute_strings(report)
    require(report["sanitization"] == {
        "version": evidence["transformation_version"],
        "original_report_sha256": evidence["original_report_sha256"],
    }, "sanitized execution report provenance mismatch")
    report_receipts = {receipt["variant"]: receipt for receipt in report["variants"]}
    require(len(report_receipts) == len(report["variants"]),
            "duplicate retained execution receipt")
    require(report["revision"] == evidence["revision"] and
            report["inventory_sha256"] == evidence["inventory_sha256"] and
            report["tracked_files_modified"] == evidence["tracked_files_modified"] and
            report["returncode"] == 0, "retained execution report identity mismatch")
    require(report["invocation"] == [
        "bmake", ".MAKE.LEVEL.ENV=MAKELEVEL", "MACHINE=rp2040",
        "TEST_EXECUTION_REPORT=<ci-temp>/ilp32-execution.json",
        "MACHINE=rp2040", "REQUIRE_ILP32=yes", "TEST_EXECUTION_REQUIRED=yes",
        "check-ilp32-execution-recipes", "TEST_EXECUTION_DIR=<execution-temp>",
    ], "retained required aggregate invocation mismatch")
    comparison = evidence["source_comparison"]
    require(comparison["recipient_commit"] == data["recipient_commit"] and
            comparison["executed_commit"] == evidence["revision"],
            "execution/source comparison revision mismatch")
    digest(comparison["recipient_tree"], 40, "recipient tree")
    digest(comparison["executed_tree"], 40, "executed tree")
    digest(comparison["executed_commit_object_sha256"], 64,
           "executed commit object hash")
    recipient_tree = read_tree(data["recipient_commit"])
    require(recipient_tree == comparison["recipient_tree"],
            "recipient commit tree mismatch")
    executed_tree = retained_commit_tree(comparison["executed_commit_object_base64"],
                                         evidence["revision"],
                                         comparison["executed_commit_object_sha256"])
    require(executed_tree == comparison["executed_tree"],
            "executed commit tree mismatch")
    require(recipient_tree == executed_tree,
            "tested integration and landed recipient trees differ")
    for path, expected_hash in comparison["source_sha256"].items():
        relative_path(path)
        digest(expected_hash, 64, f"{path} source hash")
        require(hashlib.sha256(read_blob(data["recipient_commit"], path)).hexdigest() ==
                expected_hash, f"{path}: recipient source hash mismatch")
        require(hashlib.sha256(read_blob(executed_tree, path)).hexdigest() == expected_hash,
                f"{path}: executed source hash mismatch")
    inventory_path = "tools/test-execution-inventory.json"
    digest(evidence["inventory_sha256"], 64, "execution inventory hash")
    recipient_inventory_blob = read_blob(data["recipient_commit"], inventory_path)
    executed_inventory_blob = read_blob(executed_tree, inventory_path)
    require(hashlib.sha256(recipient_inventory_blob).hexdigest() ==
            evidence["inventory_sha256"], "recipient execution inventory hash mismatch")
    require(hashlib.sha256(executed_inventory_blob).hexdigest() ==
            evidence["inventory_sha256"], "executed execution inventory hash mismatch")
    inventory = load_blob(recipient_inventory_blob, "recipient execution inventory")
    executed_inventory = load_blob(executed_inventory_blob, "executed execution inventory")
    require(inventory == executed_inventory, "recipient/executed inventory content mismatch")
    variants = {variant["id"]: variant for variant in inventory["variants"]}
    require(len(variants) == len(inventory["variants"]), "duplicate inventory variant")
    require(len(variants) == 62 and set(report_receipts) == set(variants),
            "retained report does not cover all 62 pinned variants")
    require(evidence["tracked_files_modified"] is False, "execution source was modified")
    require(evidence["run_url"].startswith(
        "https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/actions/runs/"),
        "execution owner URL is outside the recipient repository")
    for identifier, reported in report_receipts.items():
        expected = variants[identifier]
        directory = expected["directory"]
        program = expected["program"]
        expected_cwd = directory
        expected_path = program if directory == "." else f"{directory}/{program}"
        expected_invocation = ["./" + program]
        if identifier == "dd.filesystem.ilp32":
            expected_path = expected["subject"]
            expected_invocation = ["sh", expected["script"], expected["subject"]]
        elif identifier == "shell.posix.ilp32":
            expected_path = expected["subject"]
            expected_invocation = ["sh", expected["script"]]
        elif identifier == "textbox.getline.ilp32":
            expected_path = "<test-temp>/getline_test32"
            expected_invocation = [expected_path]
        require(reported["cwd"] == expected_cwd and
                reported["executable"]["path"] == expected_path and
                reported["executable"]["sha256"],
                f"{identifier}: executable path or working directory mismatch")
        digest(reported["executable"]["sha256"], 64,
               f"{identifier} executable hash")
        require(reported["owner"] == expected["owner"] and
                reported["width"] == expected["width"] and
                reported["capability"] == expected["capability"],
                f"{identifier}: retained owner/width/capability drift")
        require(reported["invocation"] == expected_invocation,
                f"{identifier}: retained invocation mismatch")
        require(reported["outcome"] == "PASS" and reported["reason"] is None and
                reported["returncode"] == reported["expected_exit"] ==
                expected.get("expected_exit", 0),
                f"{identifier}: retained execution lacks PASS")
    for identifier, receipt in receipts.items():
        require(identifier in variants, f"unknown receipt: {identifier}")
        expected = variants[identifier]
        reported = report_receipts[identifier]
        require(receipt["owner"] == expected["owner"] and
                receipt["width"] == expected["width"], f"{identifier}: owner/width drift")
        require(receipt["invocation"] == ["./" + expected["program"]],
                f"{identifier}: invocation mismatch")
        require(receipt["outcome"] == "PASS" and
                receipt["returncode"] == receipt["expected_exit"] ==
                expected.get("expected_exit", 0), f"{identifier}: execution lacks PASS")
        digest(receipt["executable_sha256"], 64, f"{identifier} executable hash")
        require(receipt["owner"] == reported["owner"] and
                receipt["width"] == reported["width"] and
                receipt["invocation"] == reported["invocation"] and
                receipt["outcome"] == reported["outcome"] and
                receipt["returncode"] == reported["returncode"] and
                receipt["expected_exit"] == reported["expected_exit"] and
                receipt["executable_sha256"] == reported["executable"]["sha256"],
                f"{identifier}: retained report receipt mismatch")

    for identifier, row in rows.items():
        require(re.fullmatch(r"[a-z][a-z0-9-]+", identifier), "invalid fix id")
        require(row["disposition"] in DISPOSITIONS, f"{identifier}: invalid disposition")
        require(row["validation"] in {"executed", "open"}, f"{identifier}: invalid validation")
        for field in ("relation", "invariant", "evidence", "footprint", "abi", "licensing",
                      "next_action"):
            require(bool(row[field].strip()), f"{identifier}: missing {field}")
        require(set(row["recipient_decisions"]) ==
                {"rp2040", "upstream_discobsd", "native_211bsd"},
                f"{identifier}: missing recipient decision")
        require(all(row["recipient_decisions"].values()), f"{identifier}: empty decision")
        require(row["patches"] and set(row["patches"]) <= set(data["patches"]),
                f"{identifier}: missing patch provenance")
        require(row["original"] and row["recipient"], f"{identifier}: missing source witness")
        for source in row["original"] + row["recipient"]:
            relative_path(source["path"])
            require(bool(source["symbol"].strip()), f"{identifier}: missing symbol")
        for source in row["recipient"]:
            contents = read_blob(data["recipient_commit"], source["path"])
            require(source["symbol"].encode() in contents,
                    f"{identifier}: source witness symbol is absent")
        require(len(row["variants"]) == len(set(row["variants"])),
                f"{identifier}: duplicate variant")
        require(set(row["variants"]) <= set(receipts), f"{identifier}: missing execution receipt")
        if row["validation"] == "executed":
            require(row["disposition"] in {"present", "equivalent"} and row["variants"],
                    f"{identifier}: executed row lacks behavioral witness")
            require(row["regression_sources"], f"{identifier}: missing regression source")
            for path in [source["path"] for source in row["recipient"]] + row["regression_sources"]:
                relative_path(path)
                require(path in comparison["source_sha256"],
                        f"{identifier}: missing execution/source comparison")
        require(set(row["dependencies"]) <= set(rows), f"{identifier}: unknown dependency")
        if row["validation"] == "executed":
            require(all(rows[parent]["validation"] == "executed"
                        for parent in row["dependencies"]), f"{identifier}: open dependency")

    visited = set()

    def visit(identifier, active):
        require(identifier not in active, f"dependency cycle at {identifier}")
        if identifier in visited:
            return
        for parent in rows[identifier]["dependencies"]:
            visit(parent, active | {identifier})
        visited.add(identifier)

    for identifier in rows:
        visit(identifier, set())
    validate_bindings(data, evidence, bindings, inventory, report, read_blob)
    return sum(row["validation"] == "executed" for row in rows.values()), len(rows)


def verify(root):
    data = load(root / LEDGER)
    evidence = load(root / relative_path(data["execution_evidence"]))
    report_bytes = (root / relative_path(evidence["sanitized_report"])).read_bytes()
    witness_archive = (root / relative_path(data["donor"]["witness_path"])).read_bytes()
    bindings_bytes = (root / relative_path(data["regression_bindings"])).read_bytes()
    result = validate(
        data, evidence, report_bytes, witness_archive, bindings_bytes,
        lambda revision, path: blob(root, revision, path),
        lambda revision: tree(root, revision),
    )
    document = root / "docs/research/211bsd-semantic-ledger.md"
    check_summary(data, document.read_text())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--summary", action="store_true", help="print the Markdown row projection")
    arguments = parser.parse_args()
    try:
        if arguments.summary:
            data = load(arguments.root / LEDGER)
            print(summary(data))
            return 0
        executed, total = verify(arguments.root)
    except (InfrastructureError, OSError) as error:
        print(f"ERROR semantic-ledger: {error}", file=sys.stderr)
        return 2
    except (ValueError, KeyError, TypeError, AttributeError) as error:
        print(f"FAIL semantic-ledger: {error}", file=sys.stderr)
        return 1
    print(f"PASS semantic-ledger: {total} declared rows; {executed} execution-backed, "
          f"{total - executed} open; provenance joins only, not whole-series coverage")
    return 0


if __name__ == "__main__":
    sys.exit(main())
