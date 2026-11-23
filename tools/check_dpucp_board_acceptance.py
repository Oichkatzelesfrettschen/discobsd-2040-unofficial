#!/usr/bin/env python
"""Validate the retained DPUCP-2024 physical-board acceptance record."""

import argparse
import json
from pathlib import Path


class EvidenceError(Exception):
    """The retained board evidence violates its checked contract."""


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path):
    try:
        return json.loads(path.read_text(), object_pairs_hook=reject_duplicate_keys)
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read board evidence: {error}") from error


EXPECTED_SURFACES = [
    "shell-command",
    "shell-getopts",
    "utility-printf",
    "utility-readlink",
    "utility-realpath",
    "utility-uudecode",
    "utility-timeout",
]
EXPECTED_ARTIFACTS = {
    "toolchain": {
        "arm_cc": "arm-none-eabi-gcc (Arch Repository) 16.2.0",
        "binutils": "GNU Binutils 2.47.20260726",
        "bmake": "20260912",
        "python": "3.14.7",
        "picotool": "2.3.1",
        "language_policy": "GNU17 production build with strict C17 portable mechanisms",
    },
    "kernel": {
        "path": "sys/arch/rp2040/compile/PICO/unix.uf2",
        "sha256": "6f60e4962e3d871fe87d04aa3e8e372c697974334bb9ef36bcc6029d6ee3223f",
        "uf2_file_bytes": 209920,
        "uf2_blocks": 410,
        "payload_interval": "[0x10000000, 0x10019a00)",
        "payload_bytes": 104960,
        "region_bytes": 131072,
        "region_margin_bytes": 26112,
        "elf_sha256": "87dcb71ece85057ba53666c41e17ceedbeb6e5dbbe593733992f2c57bf2aff78",
        "elf_file_bytes": 895724,
        "text_bytes": 104518,
        "data_bytes": 248,
        "bss_bytes": 39848,
    },
    "root": {
        "path": "distrib/rp2040/flash.uf2",
        "sha256": "370a5a882999fdd58b608936e894a498317957d769d822df6c0184570bcc05a0",
        "uf2_file_bytes": 3145728,
        "uf2_blocks": 6144,
        "payload_interval": "[0x10020000, 0x101a0000)",
        "payload_bytes": 1572864,
        "filesystem_image_sha256": (
            "de0fd61032eebf767443db84e8a5cba42cbcf1518c381267d0e147a895e84acd"
        ),
        "filesystem_image_bytes": 1012736,
        "board_df": {
            "filesystem": "root",
            "one_kib_blocks": 979,
            "used_blocks": 695,
            "available_blocks": 284,
            "capacity": "70%",
            "mounted_on": "/",
        },
    },
    "pre_board_process_accounting": {
        "source": "docs/research/timeout-process-control-growth-exception.json",
        "process_window_bytes": 147456,
        "adminbox_occupancy_before_stack_bytes": 30960,
        "adminbox_margin_before_stack_bytes": 116496,
        "largest_process": "games/warp/warp",
        "largest_process_loaded_bytes": 101300,
        "largest_process_margin_before_stack_bytes": 46156,
        "packed_root_data_blocks": 692,
        "packed_root_free_blocks": 287,
    },
}
EXPECTED_FLASH = {
    "approval": "explicit-owner-approval-in-thread",
    "execution_count": 1,
    "command_projection": [
        "sh",
        "${CHECKOUT}/distrib/rp2040/host/discobsd-flash",
        "${CHECKOUT}/sys/arch/rp2040/compile/PICO/unix.uf2",
        "${CHECKOUT}/distrib/rp2040/flash.uf2",
    ],
    "command_projection_note": (
        "The executed command used the same three absolute checkout paths. "
        "The checked-in projection replaces only the host checkout prefix with "
        "${CHECKOUT}."
    ),
    "raw_command_sha256": "c47fbef9323e4fc2846cc2016c3cc47848377d1088dfc38893fc485682bfe35e",
    "device_by_id": (
        "/dev/serial/by-id/"
        "usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00"
    ),
    "resolved_device": "/dev/ttyACM0",
    "exit_code": 0,
    "observations": [
        "kernel image transfer reached 100%",
        "root image transfer reached 100%",
        "discobsd-flash reported rebooting into the kernel",
    ],
}
EXPECTED_RUNNER = {
    "label": "dpucp-2024-board-acceptance-retry4",
    "command_projection": [
        "${PYTHON}",
        "tools/run_logged_gate.py",
        "--heartbeat-seconds",
        "5",
        "--idle-timeout-seconds",
        "20",
        "--timeout-seconds",
        "90",
        "--",
        "${PYTHON}",
        "<temporary-board-acceptance-harness>",
    ],
    "started_at": "2026-10-18T17:58:19Z",
    "completed_at": "2026-10-18T17:58:49Z",
    "elapsed_seconds": 30.464,
    "heartbeat_seconds": 5,
    "idle_timeout_seconds": 20,
    "timeout_seconds": 90,
    "state": "completed",
    "child_exit_code": 0,
    "exit_code": 0,
    "output_bytes": 42991,
    "log_file_bytes": 44100,
    "terminal_queue_dropped_bytes": 0,
    "receipt_sha256": "339e81692a26c0d60d596ed6ed049c2ee3d8bb7fccd550cb88b22cab16a3350d",
    "log_sha256": "aa92c393f06502992e42831f6844da99bb53c4af4a2b3c54f251519e983f996c",
    "harness_sha256": "cc03ae49d64b9c26ec696a62a0d4b8d1b5ae4a2d6e5a954ae570d22454e196dd",
}
EXPECTED_CASES = [
    (
        "shell-ready",
        "precondition",
        "echo DPUCP-BOARD-READY",
        0,
        0,
        "DPUCP-BOARD-READY",
    ),
    (
        "filesystem",
        "precondition",
        "mount; df /; test -r /etc/passwd",
        0,
        0,
        "root on /; 979 blocks, 695 used, 284 available, 70%",
    ),
    ("command-v", "shell-command", "command -v printf", 0, 0, "/usr/bin/printf"),
    (
        "command-V",
        "shell-command",
        "command -V printf",
        0,
        0,
        "printf is /usr/bin/printf",
    ),
    (
        "command-p",
        "shell-command",
        "command -p printf 'DPUCP-COMMAND-P\\n'",
        0,
        0,
        "DPUCP-COMMAND-P",
    ),
    (
        "getopts",
        "shell-getopts",
        "OPTIND=1; set -- -a; getopts a dpucp_option; "
        'echo "$dpucp_option:${OPTARG-unset}:$OPTIND"',
        0,
        0,
        "a:unset:2",
    ),
    (
        "printf",
        "utility-printf",
        "/usr/bin/printf '%b' 'DPUCP-A\\nDPUCP-B\\n'",
        0,
        0,
        "DPUCP-A\\nDPUCP-B",
    ),
    (
        "readlink",
        "utility-readlink",
        "rm -f /tmp/dpucp-link; ln -s /bin/sh /tmp/dpucp-link; "
        "readlink /tmp/dpucp-link",
        0,
        0,
        "/bin/sh",
    ),
    (
        "realpath",
        "utility-realpath",
        "realpath /tmp/../bin/sh",
        0,
        0,
        "/bin/sh",
    ),
    (
        "uudecode",
        "utility-uudecode",
        "printf DPUCP-BOARD > /tmp/dpucp-payload; "
        "uuencode /tmp/dpucp-payload payload | uudecode -o - | cksum",
        0,
        0,
        "2641808887 11",
    ),
    (
        "timeout-child-status",
        "utility-timeout",
        "/usr/bin/timeout 1 sh -c 'exit 37'",
        37,
        37,
        "",
    ),
    (
        "timeout-expiry",
        "utility-timeout",
        "/usr/bin/timeout 1 sleep 3",
        124,
        124,
        "",
    ),
    (
        "cleanup",
        "cleanup",
        "rm -f /tmp/dpucp-link /tmp/dpucp-payload",
        0,
        0,
        "",
    ),
]
EXPECTED_ATTRIBUTION = {
    "build": "pre-board artifact and UF2 validation",
    "host": "separate calibrated behavior witnesses",
    "cross": "separate Cortex-M0+ compilation and link witnesses",
    "qemu-user": "separate instruction and ABI witnesses",
    "renode": "separate-unrun-in-this-record",
    "physical-board": "pass for the thirteen recorded cases",
}
EXPECTED_RESIDUALS = [
    (
        "The board run samples one RP2040 board, one USB console session, "
        "and the recorded source image."
    ),
    "Runtime peak stack and heap high-water remain unmeasured.",
    (
        "The mounted-root df snapshot preceded cleanup and included mutable state "
        "from earlier harness-development attempts."
    ),
    (
        "The run exercises selected behavior and does not establish complete "
        "POSIX.1-2024 or UNIX conformance."
    ),
    "Renode evidence is separate and absent from this physical-board record.",
    (
        "The raw receipt and transcript remain external retained artifacts "
        "identified by SHA-256; the checked-in record omits host checkout paths "
        "and prompt-by-prompt echo noise."
    ),
]
EXPECTED_CLAIM = (
    "DPUCP-2024 reports tested compatibility subsets and makes no POSIX or UNIX "
    "certification claim."
)


def validate_cases(cases):
    require(isinstance(cases, list), "board case set differs")
    require(
        [case.get("id") for case in cases] == [case[0] for case in EXPECTED_CASES],
        "board case set differs",
    )
    for observed_case, expected_case in zip(cases, EXPECTED_CASES, strict=True):
        (
            case_id,
            surface,
            command,
            expected_status,
            observed_status,
            observed_output,
        ) = expected_case
        require(
            observed_case.get("surface") == surface
            and observed_case.get("command") == command
            and observed_case.get("expected_status") == expected_status
            and observed_case.get("observed_status") == observed_status
            and observed_case.get("observed_output") == observed_output,
            f"board case differs: {case_id}",
        )
    covered_surfaces = {
        case["surface"] for case in cases if case["surface"] in EXPECTED_SURFACES
    }
    require(covered_surfaces == set(EXPECTED_SURFACES), "release surface coverage differs")


def validate(root, evidence):
    require(evidence.get("schema_version") == 1, "unsupported board evidence schema")
    require(evidence.get("profile") == "DPUCP-2024", "board evidence profile differs")
    require(evidence.get("profile_version") == "2024.2", "profile version differs")
    require(
        evidence.get("evidence_class") == "physical-board",
        "evidence class is not physical-board",
    )
    require(
        evidence.get("claim") == EXPECTED_CLAIM,
        "compatibility claim differs",
    )
    require(
        evidence.get("source_commit")
        == "e528c1a1ad499ecb0d6983c45c678c39c03b5241",
        "source commit differs",
    )
    require(
        evidence.get("profile_contract_commit")
        == "f1dc15845f7cc67361c52c694c45fe5135b2a3ef",
        "profile contract commit differs",
    )
    require(evidence.get("release_surfaces") == EXPECTED_SURFACES, "release surface differs")

    profile = load_json(root / "docs/research/dpucp-2024-profile.json")
    require(
        profile.get("first_hardware_release") == evidence["release_surfaces"],
        "profile release surface differs from board evidence",
    )
    artifacts = evidence.get("artifacts")
    require(isinstance(artifacts, dict), "artifact record is absent")
    require(
        artifacts.get("kernel") == EXPECTED_ARTIFACTS["kernel"],
        "kernel artifact differs",
    )
    require(
        artifacts.get("root") == EXPECTED_ARTIFACTS["root"],
        "root artifact differs",
    )
    require(artifacts == EXPECTED_ARTIFACTS, "artifact record differs")

    flash = evidence.get("flash")
    require(isinstance(flash, dict), "flash record is absent")
    require(flash.get("execution_count") == 1, "flash execution count differs")
    require(
        flash.get("command_projection") == EXPECTED_FLASH["command_projection"],
        "flash command projection differs",
    )
    require(flash == EXPECTED_FLASH, "flash record differs")

    acceptance = evidence.get("acceptance")
    require(isinstance(acceptance, dict), "acceptance record is absent")
    require(acceptance.get("runner") == EXPECTED_RUNNER, "acceptance runner differs")
    validate_cases(acceptance.get("cases"))
    require(acceptance.get("outcome") == "pass", "board acceptance outcome differs")
    require(evidence.get("attribution") == EXPECTED_ATTRIBUTION, "evidence attribution differs")
    require(
        evidence.get("residual_uncertainty") == EXPECTED_RESIDUALS,
        "residual uncertainty differs",
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    evidence = load_json(
        arguments.root / "docs/research/dpucp-2024-board-acceptance.json"
    )
    validate(arguments.root, evidence)
    print("DPUCP-2024 board acceptance: 7 surfaces, 13 cases, physical-board pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
