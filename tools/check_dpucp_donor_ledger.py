"""Validate the bounded DPUCP-2024 BSD donor ledger."""

import argparse
import csv
import hashlib
import io
import json
import re
import subprocess
from pathlib import Path, PurePosixPath

EXPECTED_REPOSITORIES = {
    "4.4BSD-Lite2": (
        "https://github.com/Oichkatzelesfrettschen/4.4BSD-Lite2.git",
        "b10da34408c8a0f32f51d79a7f16bd75337115d2",
    ),
    "netbsd-src": (
        "https://github.com/Oichkatzelesfrettschen/src.git",
        "1e8e727e461bb760bf82162f33b64afea02da2f3",
    ),
    "freebsd-src": (
        "https://github.com/Oichkatzelesfrettschen/freebsd-src.git",
        "88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da",
    ),
    "openbsd-src": (
        "https://github.com/Oichkatzelesfrettschen/openbsd-src.git",
        "8849c5d46b9b299f5d887033706f1ddd60f8f966",
    ),
    "211bsd": (
        "https://github.com/Oichkatzelesfrettschen/211bsd.git",
        "0a38e59d742ff4371b78ea65c5bcc50477b31708",
    ),
    "retrobsd": (
        "https://github.com/Oichkatzelesfrettschen/retrobsd.git",
        "10e2a8d731502d7dfc4ca87ff87a586f194eafa6",
    ),
    "LiteBSD": (
        "https://github.com/Oichkatzelesfrettschen/LiteBSD.git",
        "03934b78b7c78c2ecc52dd63d597c7847ebc1704",
    ),
    "LiteBSD-2": (
        "https://github.com/Oichkatzelesfrettschen/LiteBSD-2.git",
        "25b8a3ff7ad21b426214964eb91786f63e34e5a4",
    ),
}
EXPECTED_CANDIDATES = {
    "libc-reallocarray": (
        "netbsd-src",
        ["dpucp-2024:system-interface:reallocarray"],
    ),
    "libc-memmem": ("netbsd-src", []),
    "libc-getdelim-getline": (
        "netbsd-src",
        [
            "dpucp-2024:system-interface:getdelim",
            "dpucp-2024:system-interface:getline",
        ],
    ),
    "shell-getopts": ("4.4BSD-Lite2", ["dpucp-2024:utility:getopts"]),
    "shell-command": ("freebsd-src", ["dpucp-2024:utility:command"]),
    "utility-printf": ("freebsd-src", ["dpucp-2024:utility:printf"]),
    "utility-readlink": ("openbsd-src", ["dpucp-2024:utility:readlink"]),
    "utility-realpath": ("freebsd-src", ["dpucp-2024:utility:realpath"]),
    "utility-timeout": ("freebsd-src", ["dpucp-2024:utility:timeout"]),
    "utility-uudecode": ("netbsd-src", ["dpucp-2024:utility:uudecode"]),
    "utility-nl-bre": ("freebsd-src", ["dpucp-2024:utility:nl"]),
}
REQUIRED_DOCUMENT_FIELDS = {
    "schema_version",
    "profile_id",
    "claim",
    "selection_rule",
    "repositories",
    "candidates",
}
REQUIRED_CANDIDATE_FIELDS = {
    "id",
    "requirements",
    "roles",
    "repository",
    "sources",
    "behavior",
    "invariants",
    "algorithm",
    "dependencies",
    "c_language",
    "c17_rewrite",
    "archive_members",
    "size_estimate",
    "deciding_gate",
    "disposition",
    "residual",
}
DISPOSITIONS = {"candidate", "promoted", "deferred", "rejected"}
REVIEWED_LICENSES = {"BSD-2-Clause", "BSD-3-Clause", "BSD-4-Clause-UC", "MIT"}
PROMOTED_CANDIDATES = {
    "libc-reallocarray",
    "libc-memmem",
    "libc-getdelim-getline",
    "shell-getopts",
    "shell-command",
}
FOUNDATION_MEASUREMENT = "docs/research/libc-array-memory-line-reading.md"
FOUNDATION_MEASUREMENT_DATA = "docs/research/libc-array-memory-line-reading.json"
SHELL_MEASUREMENT = "docs/research/shell-command-getopts-growth-exception.md"
SHELL_MEASUREMENT_DATA = "docs/research/shell-command-getopts-growth-exception.json"


class DonorError(RuntimeError):
    """Report a donor-ledger provenance or decision contradiction."""


def require(condition, message):
    if not condition:
        raise DonorError(message)


def require_nonnegative_integers(values, fields, label):
    for field in fields:
        value = values[field]
        require(
            isinstance(value, int) and not isinstance(value, bool) and value >= 0,
            f"invalid {label} {field}",
        )


def validate_foundation_measurement(measurement):
    require(
        set(measurement) == {
            "schema_version", "profile_id", "base_commit", "root", "resources"
        },
        "malformed foundational-libc measurement",
    )
    require(measurement["schema_version"] == 1, "unsupported measurement schema")
    require(measurement["profile_id"] == "DPUCP-2024", "wrong measurement profile")
    require(
        re.fullmatch(r"[0-9a-f]{40}", measurement["base_commit"]) is not None,
        "bad measurement base commit",
    )
    root = measurement["root"]
    require(
        set(root) == {"partition_blocks", "inode_list_blocks", "base", "candidate"},
        "malformed root measurement",
    )
    require(root["partition_blocks"] == 988, "root partition drift")
    require(root["inode_list_blocks"] == 9, "root inode-list drift")
    for state in ("base", "candidate"):
        counters = root[state]
        require(
            set(counters) == {"data_blocks", "free_blocks"},
            f"malformed {state} root counters",
        )
        require(
            counters["data_blocks"] + counters["free_blocks"]
            + root["inode_list_blocks"] == root["partition_blocks"],
            f"inconsistent {state} root counters",
        )
    require(
        root["candidate"]["data_blocks"] - root["base"]["data_blocks"] == -1,
        "unexpected root data-block delta",
    )
    resources = measurement["resources"]
    require(set(resources) == {"memmem", "line_reader"}, "malformed resources")
    require(
        resources["memmem"] == {
            "baseline_stack_bytes": 24,
            "stack_bytes": 24,
        },
        "memmem stack measurement drift",
    )
    require(
        resources["line_reader"] == {
            "baseline_stack_bytes": 48,
            "stack_bytes": 48,
            "witness_bytes": 300,
            "final_buffer_bytes": 512,
            "moving_realloc_peak_payload_bytes": 768,
        },
        "line-reader resource measurement drift",
    )


def validate_shell_measurement(measurement):
    require(
        set(measurement) == {
            "schema_version",
            "profile_id",
            "base_commit",
            "implementation_commit",
            "block_bytes",
            "kernel",
            "shell",
            "root",
            "reviewer_approval",
        },
        "malformed shell growth exception",
    )
    require(measurement["schema_version"] == 2, "unsupported shell measurement schema")
    require(measurement["profile_id"] == "DPUCP-2024", "wrong shell measurement profile")
    for field in ("base_commit", "implementation_commit"):
        require(
            re.fullmatch(r"[0-9a-f]{40}", measurement[field]) is not None,
            f"bad shell measurement {field}",
        )
    require(measurement["block_bytes"] == 1024, "shell measurement block size drift")

    kernel = measurement["kernel"]
    require(
        set(kernel) == {"region_bytes", "base", "candidate", "delta"},
        "malformed kernel measurement",
    )
    require(kernel["region_bytes"] == 131072, "kernel region drift")
    kernel_fields = {
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "section_sum_spare_bytes",
        "load_alignment_bytes",
        "loaded_flash_span_bytes",
        "load_tail_margin_bytes",
    }
    for state in ("base", "candidate"):
        require(set(kernel[state]) == kernel_fields, f"malformed {state} kernel measurement")
        require_nonnegative_integers(
            kernel[state], kernel_fields, f"{state} kernel measurement"
        )
        require(
            kernel[state]["section_sum_spare_bytes"]
            == kernel["region_bytes"]
            - kernel[state]["text_bytes"]
            - kernel[state]["data_bytes"],
            f"inconsistent {state} kernel section-sum spare",
        )
        require(
            kernel[state]["loaded_flash_span_bytes"]
            == kernel[state]["text_bytes"]
            + kernel[state]["data_bytes"]
            + kernel[state]["load_alignment_bytes"],
            f"inconsistent {state} kernel loaded span",
        )
        require(
            kernel[state]["load_tail_margin_bytes"]
            == kernel["region_bytes"] - kernel[state]["loaded_flash_span_bytes"],
            f"inconsistent {state} kernel load-tail margin",
        )
    for field in ("text_bytes", "data_bytes", "bss_bytes"):
        require(
            kernel["delta"][field] == kernel["candidate"][field] - kernel["base"][field],
            f"inconsistent kernel {field} delta",
        )

    shell = measurement["shell"]
    require(
        set(shell) == {"process_window_bytes", "base", "candidate", "delta"},
        "malformed shell measurement",
    )
    require(shell["process_window_bytes"] == 147456, "shell process window drift")
    shell_fields = {
        "aout_bytes",
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "window_occupancy_before_stack_bytes",
        "window_margin_before_stack_bytes",
    }
    for state in ("base", "candidate"):
        values = shell[state]
        require(set(values) == shell_fields, f"malformed {state} shell measurement")
        require_nonnegative_integers(
            values, shell_fields, f"{state} shell measurement"
        )
        occupancy = values["text_bytes"] + values["data_bytes"] + values["bss_bytes"]
        require(
            values["window_occupancy_before_stack_bytes"] == occupancy,
            f"inconsistent {state} shell occupancy",
        )
        require(
            values["window_margin_before_stack_bytes"]
            == shell["process_window_bytes"] - occupancy,
            f"inconsistent {state} shell margin",
        )
    for field in (
        "aout_bytes",
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "window_occupancy_before_stack_bytes",
    ):
        require(
            shell["delta"][field] == shell["candidate"][field] - shell["base"][field],
            f"inconsistent shell {field} delta",
        )

    root = measurement["root"]
    require(
        set(root) == {
            "partition_blocks",
            "inode_list_blocks",
            "base",
            "candidate",
            "delta",
            "candidate_free_bytes",
        },
        "malformed shell root measurement",
    )
    require(root["partition_blocks"] == 988, "shell root partition drift")
    require(root["inode_list_blocks"] == 9, "shell root inode-list drift")
    for state in ("base", "candidate"):
        require(
            set(root[state]) == {"files", "data_blocks", "free_blocks"},
            f"malformed shell root {state} measurement",
        )
        require_nonnegative_integers(
            root[state], {"files", "data_blocks", "free_blocks"},
            f"shell root {state} measurement",
        )
        require(
            root[state]["data_blocks"]
            + root[state]["free_blocks"]
            + root["inode_list_blocks"]
            == root["partition_blocks"],
            f"inconsistent shell root {state} arithmetic",
        )
    for field in ("files", "data_blocks", "free_blocks"):
        require(
            root["delta"][field] == root["candidate"][field] - root["base"][field],
            f"inconsistent shell root {field} delta",
        )
    require(
        isinstance(root["candidate_free_bytes"], int)
        and not isinstance(root["candidate_free_bytes"], bool)
        and root["candidate_free_bytes"] >= 0,
        "invalid shell root free-byte margin",
    )
    require(
        root["candidate_free_bytes"]
        == root["candidate"]["free_blocks"] * measurement["block_bytes"],
        "inconsistent shell root free-byte margin",
    )

    approval = measurement["reviewer_approval"]
    require(
        set(approval) == {"status", "reviewer", "evidence"},
        "malformed shell reviewer approval",
    )
    require(approval["status"] in {"pending", "approved"}, "bad shell approval state")
    if approval["status"] == "approved":
        require(bool(approval["reviewer"]), "approved shell exception lacks reviewer")
        require(bool(approval["evidence"]), "approved shell exception lacks evidence")
    else:
        require(
            not approval["reviewer"] and not approval["evidence"],
            "pending shell approval has evidence",
        )


def safe_path(value):
    path = PurePosixPath(value)
    return bool(value) and not path.is_absolute() and ".." not in path.parts


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DonorError(f"cannot read {path}: {error}") from error


def load_requirement_ids(path):
    try:
        reader = csv.DictReader(io.StringIO(path.read_text(encoding="utf-8")), delimiter="\t")
    except (OSError, UnicodeError) as error:
        raise DonorError(f"cannot read {path}: {error}") from error
    require("requirement_id" in (reader.fieldnames or []), "requirement ledger lacks ids")
    return {row["requirement_id"] for row in reader}


def validate_document(root, document, requirement_ids):
    require(set(document) == REQUIRED_DOCUMENT_FIELDS, "malformed donor ledger")
    require(document.get("schema_version") == 2, "unsupported donor schema")
    require(document.get("profile_id") == "DPUCP-2024", "wrong donor profile")
    measurement_path = root / FOUNDATION_MEASUREMENT_DATA
    require(measurement_path.is_file(), "missing foundational-libc measurement data")
    validate_foundation_measurement(load_json(measurement_path))
    shell_measurement_path = root / SHELL_MEASUREMENT_DATA
    require(shell_measurement_path.is_file(), "missing shell growth-exception data")
    validate_shell_measurement(load_json(shell_measurement_path))
    require(
        "makes no POSIX or UNIX conformance claim" in document.get("claim", ""),
        "donor ledger makes a conformance claim",
    )
    require(bool(document.get("selection_rule")), "donor selection rule is absent")
    repositories = document.get("repositories")
    require(isinstance(repositories, dict), "repository pins are absent")
    require(set(repositories) == set(EXPECTED_REPOSITORIES), "repository set drift")
    for name, (origin, revision) in EXPECTED_REPOSITORIES.items():
        record = repositories[name]
        require(set(record) == {"origin", "revision"}, f"bad repository pin: {name}")
        require(record.get("revision") == revision, f"repository revision drift: {name}")
        require(record.get("origin") == origin, f"repository origin drift: {name}")

    candidates = document.get("candidates")
    require(isinstance(candidates, list) and candidates, "donor candidates are absent")
    candidate_ids = [candidate.get("id") for candidate in candidates]
    require(
        candidate_ids == list(EXPECTED_CANDIDATES),
        "bounded donor candidate set or order drift",
    )
    seen = set()
    for candidate in candidates:
        require(set(candidate) == REQUIRED_CANDIDATE_FIELDS, "malformed donor candidate")
        candidate_id = candidate["id"]
        require(candidate_id and candidate_id not in seen, f"duplicate donor: {candidate_id}")
        seen.add(candidate_id)
        expected_repository, expected_requirements = EXPECTED_CANDIDATES[candidate_id]
        require(
            candidate["repository"] == expected_repository,
            f"repository binding drift: {candidate_id}",
        )
        require(
            candidate["requirements"] == expected_requirements,
            f"requirement binding drift: {candidate_id}",
        )
        for requirement in candidate["requirements"]:
            require(requirement in requirement_ids, f"unknown requirement: {requirement}")
        for field in ("roles", "invariants", "dependencies"):
            values = candidate[field]
            require(
                isinstance(values, list)
                and values
                and all(isinstance(value, str) and value for value in values),
                f"missing {field}: {candidate_id}",
            )
        for field in ("behavior", "algorithm", "c_language", "c17_rewrite", "residual"):
            require(
                isinstance(candidate[field], str) and bool(candidate[field]),
                f"missing {field}: {candidate_id}",
            )
        sources = candidate["sources"]
        require(isinstance(sources, list) and sources, f"missing sources: {candidate_id}")
        source_paths = set()
        for source in sources:
            require(
                set(source) == {"repository", "path", "sha256", "license"},
                f"bad source: {candidate_id}",
            )
            source_repository = source["repository"]
            require(
                source_repository in repositories,
                f"unknown source repository: {candidate_id}",
            )
            require(safe_path(source["path"]), f"unsafe donor path: {candidate_id}")
            source_identity = (source_repository, source["path"])
            require(
                source_identity not in source_paths,
                f"duplicate donor path: {candidate_id}",
            )
            source_paths.add(source_identity)
            require(
                re.fullmatch(r"[0-9a-f]{64}", source["sha256"]) is not None,
                f"bad donor hash: {candidate_id}",
            )
            require(
                source["license"] in REVIEWED_LICENSES,
                f"unknown donor license: {candidate_id}",
            )
        archive_members = candidate["archive_members"]
        require(
            isinstance(archive_members, list)
            and archive_members
            and all(isinstance(member, str) and member for member in archive_members),
            f"bad archive members: {candidate_id}",
        )
        size = candidate["size_estimate"]
        require(set(size) == {"status", "surfaces", "evidence"}, f"bad size record: {candidate_id}")
        require(
            isinstance(size["surfaces"], list)
            and size["surfaces"]
            and all(
                isinstance(surface, str) and surface for surface in size["surfaces"]
            ),
            f"missing size surfaces: {candidate_id}",
        )
        if candidate_id in PROMOTED_CANDIDATES:
            require(size["status"] == "measured", f"unmeasured promotion: {candidate_id}")
            expected_evidence = (
                SHELL_MEASUREMENT
                if candidate_id in {"shell-getopts", "shell-command"}
                else FOUNDATION_MEASUREMENT
            )
            require(size["evidence"] == expected_evidence, f"unbound size evidence: {candidate_id}")
            require(
                (root / expected_evidence).is_file(),
                f"missing size evidence: {candidate_id}",
            )
        else:
            require(size["status"] == "unmeasured", f"premature size state: {candidate_id}")
            require(size["evidence"] == "", f"unmeasured size has evidence: {candidate_id}")
        require(bool(candidate["deciding_gate"]), f"missing deciding gate: {candidate_id}")
        require(candidate["disposition"] in DISPOSITIONS, f"bad disposition: {candidate_id}")
        if candidate_id in PROMOTED_CANDIDATES:
            expected_disposition = "promoted"
        elif candidate_id == "utility-nl-bre":
            expected_disposition = "deferred"
        else:
            expected_disposition = "candidate"
        require(
            candidate["disposition"] == expected_disposition,
            f"disposition drift: {candidate_id}",
        )
    return len(candidates)


def verify_workspace(document, workspace):
    for candidate in document["candidates"]:
        for source in candidate["sources"]:
            repository = source["repository"]
            revision = document["repositories"][repository]["revision"]
            checkout = workspace / repository
            require((checkout / ".git").exists(), f"donor checkout absent: {repository}")
            result = subprocess.run(
                ["git", "show", f"{revision}:{source['path']}"],
                cwd=checkout,
                check=True,
                capture_output=True,
            )
            digest = hashlib.sha256(result.stdout).hexdigest()
            require(digest == source["sha256"], f"donor bytes drift: {candidate['id']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--workspace", type=Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    document = load_json(root / "docs/research/dpucp-2024-donor-ledger.json")
    requirement_ids = load_requirement_ids(root / "docs/research/dpucp-2024-requirements.tsv")
    count = validate_document(root, document, requirement_ids)
    if arguments.workspace is not None:
        verify_workspace(document, arguments.workspace.resolve())
    print(f"DPUCP donor ledger: {count} bounded candidates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
