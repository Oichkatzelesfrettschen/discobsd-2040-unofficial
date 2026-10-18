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
    "utility-printf",
    "utility-readlink",
    "utility-realpath",
    "utility-uudecode",
}
FOUNDATION_MEASUREMENT = "docs/research/libc-array-memory-line-reading.md"
FOUNDATION_MEASUREMENT_DATA = "docs/research/libc-array-memory-line-reading.json"
SHELL_MEASUREMENT = "docs/research/shell-command-getopts-growth-exception.md"
SHELL_MEASUREMENT_DATA = "docs/research/shell-command-getopts-growth-exception.json"
PRINTF_MEASUREMENT = "docs/research/utility-printf-growth-exception.md"
PRINTF_MEASUREMENT_DATA = "docs/research/utility-printf-growth-exception.json"
RECOVERY_MEASUREMENT = "docs/research/recovery-path-utilities-growth-exception.md"
RECOVERY_MEASUREMENT_DATA = "docs/research/recovery-path-utilities-growth-exception.json"


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


def validate_printf_measurement(measurement, repository_root):
    require(
        set(measurement) == {
            "schema_version",
            "profile_id",
            "base_commit",
            "implementation_commit",
            "block_bytes",
            "kernel",
            "programs",
            "root",
            "composition",
            "reviewer_approval",
        },
        "malformed printf growth exception",
    )
    require(measurement["schema_version"] == 2, "unsupported printf measurement schema")
    require(measurement["profile_id"] == "DPUCP-2024", "wrong printf measurement profile")
    for field in ("base_commit", "implementation_commit"):
        require(
            re.fullmatch(r"[0-9a-f]{40}", measurement[field]) is not None,
            f"bad printf measurement {field}",
        )
    require(measurement["block_bytes"] == 1024, "printf measurement block size drift")

    kernel = measurement["kernel"]
    require(
        set(kernel) == {"region_bytes", "base", "candidate", "delta"},
        "malformed printf kernel measurement",
    )
    require(kernel["region_bytes"] == 131072, "printf kernel region drift")
    kernel_fields = {
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "loaded_flash_span_bytes",
        "load_tail_margin_bytes",
    }
    for state in ("base", "candidate"):
        require(set(kernel[state]) == kernel_fields, f"malformed {state} printf kernel")
        require_nonnegative_integers(kernel[state], kernel_fields, f"{state} printf kernel")
        require(
            kernel[state]["load_tail_margin_bytes"]
            == kernel["region_bytes"] - kernel[state]["loaded_flash_span_bytes"],
            f"inconsistent {state} printf kernel margin",
        )
    for field in ("text_bytes", "data_bytes", "bss_bytes"):
        require(
            kernel["delta"][field] == kernel["candidate"][field] - kernel["base"][field],
            f"inconsistent printf kernel {field} delta",
        )

    programs = measurement["programs"]
    require(
        set(programs) == {
            "process_window_bytes",
            "standalone_printf",
            "utilbox",
            "largest_process",
        },
        "malformed printf program measurement",
    )
    require(programs["process_window_bytes"] == 147456, "printf process window drift")
    program_fields = {
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "window_occupancy_before_stack_bytes",
        "window_margin_before_stack_bytes",
        "raw_bytes",
        "packed_bytes",
        "packed_blocks",
    }
    delta_fields = program_fields - {"window_margin_before_stack_bytes"}
    for program_name in ("standalone_printf", "utilbox"):
        program = programs[program_name]
        require(set(program) == {"base", "candidate", "delta"}, f"malformed {program_name}")
        for state in ("base", "candidate"):
            values = program[state]
            require(set(values) == program_fields, f"malformed {state} {program_name}")
            require_nonnegative_integers(values, program_fields, f"{state} {program_name}")
            occupancy = values["text_bytes"] + values["data_bytes"] + values["bss_bytes"]
            require(
                values["window_occupancy_before_stack_bytes"] == occupancy,
                f"inconsistent {state} {program_name} occupancy",
            )
            require(
                values["window_margin_before_stack_bytes"]
                == programs["process_window_bytes"] - occupancy,
                f"inconsistent {state} {program_name} margin",
            )
            require(
                values["raw_bytes"]
                == 32 + values["text_bytes"] + values["data_bytes"],
                f"inconsistent {state} {program_name} a.out size",
            )
        require(set(program["delta"]) == delta_fields, f"malformed {program_name} delta")
        for field in delta_fields:
            require(
                program["delta"][field]
                == program["candidate"][field] - program["base"][field],
                f"inconsistent {program_name} {field} delta",
            )

    largest = programs["largest_process"]
    require(
        set(largest) == {
            "name",
            "base_loaded_bytes",
            "candidate_loaded_bytes",
            "delta_loaded_bytes",
            "candidate_window_margin_before_stack_bytes",
        },
        "malformed largest-process measurement",
    )
    require(largest["name"] == "warp", "largest-process identity drift")
    require_nonnegative_integers(
        largest,
        {
            "base_loaded_bytes",
            "candidate_loaded_bytes",
            "candidate_window_margin_before_stack_bytes",
        },
        "largest-process measurement",
    )
    require(
        largest["delta_loaded_bytes"]
        == largest["candidate_loaded_bytes"] - largest["base_loaded_bytes"],
        "inconsistent largest-process delta",
    )
    require(
        largest["candidate_window_margin_before_stack_bytes"]
        == programs["process_window_bytes"] - largest["candidate_loaded_bytes"],
        "inconsistent largest-process margin",
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
        "malformed printf root measurement",
    )
    require(root["partition_blocks"] == 988, "printf root partition drift")
    require(root["inode_list_blocks"] == 9, "printf root inode-list drift")
    for state in ("base", "candidate"):
        require(
            set(root[state]) == {"files", "data_blocks", "free_blocks"},
            f"malformed printf root {state}",
        )
        require_nonnegative_integers(
            root[state], {"files", "data_blocks", "free_blocks"}, f"printf root {state}"
        )
        require(
            root[state]["data_blocks"] + root[state]["free_blocks"]
            + root["inode_list_blocks"] == root["partition_blocks"],
            f"inconsistent printf root {state} arithmetic",
        )
    for field in ("files", "data_blocks", "free_blocks"):
        require(
            root["delta"][field] == root["candidate"][field] - root["base"][field],
            f"inconsistent printf root {field} delta",
        )
    require(
        root["candidate_free_bytes"]
        == root["candidate"]["free_blocks"] * measurement["block_bytes"],
        "inconsistent printf root free-byte margin",
    )

    composition = measurement["composition"]
    require(
        set(composition) == {
            "shipped_form",
            "archive_members_evidence",
            "candidate_standalone_libc_members",
            "candidate_utilbox_libc_members",
            "alternative_utilbox_libc_members",
            "alternative_utilbox_packed_blocks",
            "alternative_standalone_packed_blocks",
            "alternative_combined_packed_blocks",
            "standalone_rejected_net_root_blocks",
        },
        "malformed printf composition measurement",
    )
    require(composition["shipped_form"] == "utilbox", "printf shipped-form drift")
    require(
        safe_path(composition["archive_members_evidence"]),
        "unsafe printf archive-member evidence path",
    )
    require_nonnegative_integers(
        composition,
        {
            "candidate_standalone_libc_members",
            "candidate_utilbox_libc_members",
            "alternative_utilbox_libc_members",
            "alternative_utilbox_packed_blocks",
            "alternative_standalone_packed_blocks",
            "alternative_combined_packed_blocks",
            "standalone_rejected_net_root_blocks",
        },
        "printf archive-member count",
    )
    require(
        composition["alternative_standalone_packed_blocks"]
        == programs["standalone_printf"]["candidate"]["packed_blocks"],
        "inconsistent alternative standalone block count",
    )
    require(
        composition["alternative_combined_packed_blocks"]
        == composition["alternative_utilbox_packed_blocks"]
        + composition["alternative_standalone_packed_blocks"],
        "inconsistent alternative combined block count",
    )
    require(
        composition["standalone_rejected_net_root_blocks"]
        == composition["alternative_combined_packed_blocks"]
        - programs["utilbox"]["candidate"]["packed_blocks"],
        "inconsistent rejected standalone net block cost",
    )

    evidence_path = repository_root / composition["archive_members_evidence"]
    require(evidence_path.is_file(), "missing printf archive-member evidence")
    try:
        with evidence_path.open(encoding="utf-8", newline="") as evidence_file:
            reader = csv.DictReader(evidence_file, delimiter="\t")
            require(
                reader.fieldnames == ["program", "archive_member"],
                "malformed printf archive-member header",
            )
            evidence_rows = list(reader)
    except (OSError, UnicodeError, csv.Error) as error:
        raise DonorError(f"cannot read printf archive-member evidence: {error}") from error
    expected_programs = {
        "standalone_printf": "candidate_standalone_libc_members",
        "utilbox": "candidate_utilbox_libc_members",
        "utilbox_without_printf": "alternative_utilbox_libc_members",
    }
    require(
        {row["program"] for row in evidence_rows} == set(expected_programs),
        "unexpected printf archive-member program",
    )
    require(
        all(
            re.fullmatch(r"libc[.]a[(][A-Za-z0-9_.]+[)]", row["archive_member"])
            is not None
            for row in evidence_rows
        ),
        "malformed printf archive-member name",
    )
    require(
        len({(row["program"], row["archive_member"]) for row in evidence_rows})
        == len(evidence_rows),
        "duplicate printf archive member",
    )
    for program, count_field in expected_programs.items():
        require(
            sum(row["program"] == program for row in evidence_rows)
            == composition[count_field],
            f"inconsistent {program} archive-member count",
        )

    approval = measurement["reviewer_approval"]
    require(
        set(approval) == {"status", "reviewer", "evidence"},
        "malformed printf reviewer approval",
    )
    require(approval["status"] in {"pending", "approved"}, "bad printf approval state")
    if approval["status"] == "approved":
        require(bool(approval["reviewer"]), "approved printf exception lacks reviewer")
        require(bool(approval["evidence"]), "approved printf exception lacks evidence")
    else:
        require(
            not approval["reviewer"] and not approval["evidence"],
            "pending printf approval has evidence",
        )


def validate_recovery_measurement(measurement, repository_root):
    require(
        set(measurement) == {
            "schema_version",
            "profile_id",
            "base_commit",
            "implementation_commit",
            "block_bytes",
            "kernel",
            "programs",
            "root",
            "composition",
            "maps",
            "resources",
            "reviewer_approval",
        },
        "malformed recovery growth exception",
    )
    require(measurement["schema_version"] == 1, "unsupported recovery schema")
    require(measurement["profile_id"] == "DPUCP-2024", "wrong recovery profile")
    for field in ("base_commit", "implementation_commit"):
        require(
            re.fullmatch(r"[0-9a-f]{40}", measurement[field]) is not None,
            f"bad recovery measurement {field}",
        )
    require(measurement["block_bytes"] == 1024, "recovery block size drift")

    kernel = measurement["kernel"]
    require(
        set(kernel) == {"region_bytes", "base", "candidate", "delta"},
        "malformed recovery kernel measurement",
    )
    require(kernel["region_bytes"] == 131072, "recovery kernel region drift")
    kernel_fields = {
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "loaded_flash_span_bytes",
        "load_tail_margin_bytes",
    }
    for state in ("base", "candidate"):
        require(set(kernel[state]) == kernel_fields, f"malformed {state} recovery kernel")
        require_nonnegative_integers(
            kernel[state], kernel_fields, f"{state} recovery kernel"
        )
        require(
            kernel[state]["load_tail_margin_bytes"]
            == kernel["region_bytes"] - kernel[state]["loaded_flash_span_bytes"],
            f"inconsistent {state} recovery kernel margin",
        )
    require(
        set(kernel["delta"]) == {"text_bytes", "data_bytes", "bss_bytes"},
        "malformed recovery kernel delta",
    )
    for field in kernel["delta"]:
        require(
            kernel["delta"][field]
            == kernel["candidate"][field] - kernel["base"][field],
            f"inconsistent recovery kernel {field} delta",
        )

    programs = measurement["programs"]
    require(
        set(programs)
        == {
            "process_window_bytes",
            "textbox",
            "uudecode",
            "readlink",
            "realpath",
            "largest_process",
        },
        "malformed recovery program measurement",
    )
    require(programs["process_window_bytes"] == 147456, "recovery process window drift")
    program_fields = {
        "text_bytes",
        "data_bytes",
        "bss_bytes",
        "window_occupancy_before_stack_bytes",
        "window_margin_before_stack_bytes",
        "raw_bytes",
        "packed_bytes",
        "packed_blocks",
    }
    delta_fields = program_fields - {"window_margin_before_stack_bytes"}
    for program_name in ("textbox", "uudecode"):
        program = programs[program_name]
        require(set(program) == {"base", "candidate", "delta"}, f"malformed {program_name}")
        for state in ("base", "candidate"):
            values = program[state]
            require(set(values) == program_fields, f"malformed {state} {program_name}")
            require_nonnegative_integers(values, program_fields, f"{state} {program_name}")
            occupancy = values["text_bytes"] + values["data_bytes"] + values["bss_bytes"]
            require(
                values["window_occupancy_before_stack_bytes"] == occupancy,
                f"inconsistent {state} {program_name} occupancy",
            )
            require(
                values["window_margin_before_stack_bytes"]
                == programs["process_window_bytes"] - occupancy,
                f"inconsistent {state} {program_name} margin",
            )
            require(
                values["raw_bytes"] == 32 + values["text_bytes"] + values["data_bytes"],
                f"inconsistent {state} {program_name} a.out size",
            )
        require(set(program["delta"]) == delta_fields, f"malformed {program_name} delta")
        for field in delta_fields:
            require(
                program["delta"][field]
                == program["candidate"][field] - program["base"][field],
                f"inconsistent {program_name} {field} delta",
            )

    for program_name in ("readlink", "realpath"):
        values = programs[program_name]
        require(set(values) == program_fields, f"malformed {program_name}")
        require_nonnegative_integers(values, program_fields, program_name)
        occupancy = values["text_bytes"] + values["data_bytes"] + values["bss_bytes"]
        require(
            values["window_occupancy_before_stack_bytes"] == occupancy,
            f"inconsistent {program_name} occupancy",
        )
        require(
            values["window_margin_before_stack_bytes"]
            == programs["process_window_bytes"] - occupancy,
            f"inconsistent {program_name} margin",
        )
        require(
            values["raw_bytes"] == 32 + values["text_bytes"] + values["data_bytes"],
            f"inconsistent {program_name} a.out size",
        )

    largest = programs["largest_process"]
    require(
        set(largest)
        == {
            "name",
            "base_loaded_bytes",
            "candidate_loaded_bytes",
            "delta_loaded_bytes",
            "candidate_window_margin_before_stack_bytes",
        },
        "malformed recovery largest-process measurement",
    )
    require(largest["name"] == "warp", "recovery largest-process identity drift")
    require_nonnegative_integers(
        largest,
        {
            "base_loaded_bytes",
            "candidate_loaded_bytes",
            "candidate_window_margin_before_stack_bytes",
        },
        "recovery largest-process measurement",
    )
    require(
        largest["delta_loaded_bytes"]
        == largest["candidate_loaded_bytes"] - largest["base_loaded_bytes"],
        "inconsistent recovery largest-process delta",
    )
    require(
        largest["candidate_window_margin_before_stack_bytes"]
        == programs["process_window_bytes"] - largest["candidate_loaded_bytes"],
        "inconsistent recovery largest-process margin",
    )

    root = measurement["root"]
    require(
        set(root)
        == {
            "partition_blocks",
            "inode_list_blocks",
            "base",
            "candidate",
            "delta",
            "candidate_free_bytes",
        },
        "malformed recovery root measurement",
    )
    require(root["partition_blocks"] == 988, "recovery root partition drift")
    require(root["inode_list_blocks"] == 9, "recovery root inode-list drift")
    for state in ("base", "candidate"):
        require(
            set(root[state]) == {"files", "data_blocks", "free_blocks"},
            f"malformed recovery root {state}",
        )
        require_nonnegative_integers(
            root[state], {"files", "data_blocks", "free_blocks"}, f"recovery root {state}"
        )
        require(
            root[state]["data_blocks"] + root[state]["free_blocks"]
            + root["inode_list_blocks"] == root["partition_blocks"],
            f"inconsistent recovery root {state} arithmetic",
        )
    require(
        set(root["delta"]) == {"files", "data_blocks", "free_blocks"},
        "malformed recovery root delta",
    )
    for field in root["delta"]:
        require(
            root["delta"][field] == root["candidate"][field] - root["base"][field],
            f"inconsistent recovery root {field} delta",
        )
    require(
        root["candidate_free_bytes"]
        == root["candidate"]["free_blocks"] * measurement["block_bytes"],
        "inconsistent recovery root free-byte margin",
    )

    composition = measurement["composition"]
    require(
        set(composition)
        == {
            "shipped_form",
            "archive_members_evidence",
            "candidate_readlink_libc_members",
            "candidate_realpath_libc_members",
            "candidate_textbox_libc_members",
            "alternative_textbox_libc_members",
            "alternative_textbox",
            "alternative_readlink_packed_blocks",
            "alternative_realpath_packed_blocks",
            "alternative_combined_packed_blocks",
            "shipped_textbox_packed_blocks",
            "multicall_saved_root_blocks",
        },
        "malformed recovery composition measurement",
    )
    require(composition["shipped_form"] == "textbox", "recovery shipped-form drift")
    require(
        safe_path(composition["archive_members_evidence"]),
        "unsafe recovery archive-member evidence path",
    )
    count_fields = {
        "candidate_readlink_libc_members",
        "candidate_realpath_libc_members",
        "candidate_textbox_libc_members",
        "alternative_textbox_libc_members",
        "alternative_readlink_packed_blocks",
        "alternative_realpath_packed_blocks",
        "alternative_combined_packed_blocks",
        "shipped_textbox_packed_blocks",
        "multicall_saved_root_blocks",
    }
    require_nonnegative_integers(composition, count_fields, "recovery composition")
    alternative = composition["alternative_textbox"]
    alternative_fields = program_fields - {"window_margin_before_stack_bytes"}
    require(set(alternative) == alternative_fields, "malformed alternative textbox")
    require_nonnegative_integers(alternative, alternative_fields, "alternative textbox")
    require(
        alternative["window_occupancy_before_stack_bytes"]
        == alternative["text_bytes"] + alternative["data_bytes"] + alternative["bss_bytes"],
        "inconsistent alternative textbox occupancy",
    )
    require(
        alternative["raw_bytes"] == 32 + alternative["text_bytes"] + alternative["data_bytes"],
        "inconsistent alternative textbox a.out size",
    )
    require(
        composition["alternative_readlink_packed_blocks"] == programs["readlink"]["packed_blocks"],
        "inconsistent alternative readlink block count",
    )
    require(
        composition["alternative_realpath_packed_blocks"] == programs["realpath"]["packed_blocks"],
        "inconsistent alternative realpath block count",
    )
    require(
        composition["alternative_combined_packed_blocks"]
        == alternative["packed_blocks"]
        + composition["alternative_readlink_packed_blocks"]
        + composition["alternative_realpath_packed_blocks"],
        "inconsistent recovery alternative block count",
    )
    require(
        composition["shipped_textbox_packed_blocks"]
        == programs["textbox"]["candidate"]["packed_blocks"],
        "inconsistent shipped textbox block count",
    )
    require(
        composition["multicall_saved_root_blocks"]
        == composition["alternative_combined_packed_blocks"]
        - composition["shipped_textbox_packed_blocks"],
        "inconsistent recovery multicall savings",
    )

    evidence_path = repository_root / composition["archive_members_evidence"]
    require(evidence_path.is_file(), "missing recovery archive-member evidence")
    try:
        with evidence_path.open(encoding="utf-8", newline="") as evidence_file:
            reader = csv.DictReader(evidence_file, delimiter="\t")
            require(
                reader.fieldnames == ["program", "archive_member"],
                "malformed recovery archive-member header",
            )
            evidence_rows = list(reader)
    except (OSError, UnicodeError, csv.Error) as error:
        raise DonorError(f"cannot read recovery archive-member evidence: {error}") from error
    expected_programs = {
        "standalone_readlink": "candidate_readlink_libc_members",
        "standalone_realpath": "candidate_realpath_libc_members",
        "textbox": "candidate_textbox_libc_members",
        "textbox_without_path_applets": "alternative_textbox_libc_members",
    }
    require(
        {row["program"] for row in evidence_rows} == set(expected_programs),
        "unexpected recovery archive-member program",
    )
    require(
        all(
            re.fullmatch(r"libc[.]a[(][A-Za-z0-9_.]+[)]", row["archive_member"])
            is not None
            for row in evidence_rows
        ),
        "malformed recovery archive-member name",
    )
    require(
        len({(row["program"], row["archive_member"]) for row in evidence_rows})
        == len(evidence_rows),
        "duplicate recovery archive member",
    )
    for program, count_field in expected_programs.items():
        require(
            sum(row["program"] == program for row in evidence_rows)
            == composition[count_field],
            f"inconsistent {program} archive-member count",
        )

    maps = measurement["maps"]
    require(
        set(maps) == {"readlink", "realpath", "textbox", "textbox_without_path_applets"},
        "malformed recovery map identities",
    )
    for map_name, identities in maps.items():
        require(
            set(identities) == {"map_sha256", "binary_sha256"},
            f"malformed {map_name} identity",
        )
        require(
            all(re.fullmatch(r"[0-9a-f]{64}", value) is not None for value in identities.values()),
            f"bad {map_name} identity",
        )

    resources = measurement["resources"]
    require(set(resources) == {"realpath_stack"}, "malformed recovery resources")
    stack = resources["realpath_stack"]
    require(
        set(stack) == {"classification", "measured_bytes", "ceiling_bytes", "known_bad_bytes"},
        "malformed realpath stack measurement",
    )
    require(stack["classification"] == "static", "realpath stack is not static")
    require_nonnegative_integers(
        stack, {"measured_bytes", "ceiling_bytes", "known_bad_bytes"}, "realpath stack"
    )
    require(stack["measured_bytes"] <= stack["ceiling_bytes"], "realpath stack exceeds ceiling")
    require(stack["known_bad_bytes"] > stack["ceiling_bytes"], "realpath known-bad stack fits")

    approval = measurement["reviewer_approval"]
    require(
        set(approval) == {"status", "reviewer", "evidence"},
        "malformed recovery reviewer approval",
    )
    require(approval["status"] in {"pending", "approved"}, "bad recovery approval state")
    if approval["status"] == "approved":
        require(bool(approval["reviewer"]), "approved recovery exception lacks reviewer")
        require(bool(approval["evidence"]), "approved recovery exception lacks evidence")
    else:
        require(
            not approval["reviewer"] and not approval["evidence"],
            "pending recovery approval has evidence",
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
    printf_measurement_path = root / PRINTF_MEASUREMENT_DATA
    require(printf_measurement_path.is_file(), "missing printf growth-exception data")
    validate_printf_measurement(load_json(printf_measurement_path), root)
    recovery_measurement_path = root / RECOVERY_MEASUREMENT_DATA
    require(recovery_measurement_path.is_file(), "missing recovery growth-exception data")
    validate_recovery_measurement(load_json(recovery_measurement_path), root)
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
            if candidate_id in {"shell-getopts", "shell-command"}:
                expected_evidence = SHELL_MEASUREMENT
            elif candidate_id == "utility-printf":
                expected_evidence = PRINTF_MEASUREMENT
            elif candidate_id in {
                "utility-readlink",
                "utility-realpath",
                "utility-uudecode",
            }:
                expected_evidence = RECOVERY_MEASUREMENT
            else:
                expected_evidence = FOUNDATION_MEASUREMENT
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
