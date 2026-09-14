"""Run a pinned V7 behavior oracle under an external PDP-11 simulator."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


EXPECTED_IMAGE_SHA256 = (
    "235426852d2fdc2b7b3432f46bb2174d579a6e730f84d66f1464a2ae564a1c81"
)
EXPECTED_IMAGE_SIZE = 10_485_760
EXPECTED_PROFILE_SHA256 = (
    "2e51125dfa47c7efd99cfa62021fd3725b6f124e4ea08902c6086bd1428ef0d7"
)
BEGIN_MARKER = "PDP11_ORACLE_BEGIN"
END_MARKER = "PDP11_ORACLE_END"
GUEST_PROMPT = "PDP11_PROMPT"
INODE_PATTERN = re.compile(r"^\s*([0-9]+) (/oracle\.dir/[ab])$")


def file_sha256(path: pathlib.Path) -> str:
    """Return the SHA-256 digest without loading the whole artifact."""
    digest = hashlib.sha256()
    with path.open("rb") as input_file:
        while True:
            block = input_file.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def normalize_transcript(raw_output: bytes) -> str:
    """Extract the guest oracle and validate its only variable field."""
    decoded_output = raw_output.decode("ascii", errors="replace")
    normalized_newlines = decoded_output.replace("\r\n", "\n").replace("\r", "\n")
    output_lines = []
    for raw_line in normalized_newlines.splitlines():
        output_line = raw_line
        while output_line.startswith(GUEST_PROMPT):
            output_line = output_line[len(GUEST_PROMPT) :]
        output_lines.append(output_line)

    try:
        begin_index = output_lines.index(BEGIN_MARKER)
        end_index = output_lines.index(END_MARKER, begin_index + 1)
    except ValueError as error:
        raise ValueError("guest transcript lacks complete oracle markers") from error

    oracle_lines = output_lines[begin_index : end_index + 1]
    inode_values: list[str] = []
    for line_index, output_line in enumerate(oracle_lines):
        inode_match = INODE_PATTERN.fullmatch(output_line)
        if inode_match is None:
            continue
        inode_values.append(inode_match.group(1))
        oracle_lines[line_index] = f"<same-inode> {inode_match.group(2)}"

    if len(inode_values) != 2 or inode_values[0] != inode_values[1]:
        raise ValueError("guest hard-link probe did not report one shared inode")

    return "\n".join(oracle_lines) + "\n"


def simulator_version(simulator_path: pathlib.Path) -> str:
    """Read the simulator banner from a bounded, non-guest invocation."""
    completed = subprocess.run(
        [str(simulator_path)],
        input=b"exit\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=10,
    )
    banner_lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    for banner_line in banner_lines:
        if "PDP-11 simulator" in banner_line:
            return banner_line.strip()
    raise RuntimeError("simulator invocation did not report a PDP-11 banner")


def package_identity(simulator_path: pathlib.Path) -> str:
    """Return the first host package owner that can identify the simulator."""
    package_queries = (
        ("pacman", "-Qo", str(simulator_path)),
        ("dpkg-query", "-S", str(simulator_path)),
        ("pkg_info", "-W", str(simulator_path)),
        ("pkg", "which", "-q", str(simulator_path)),
    )
    for package_query in package_queries:
        if shutil.which(package_query[0]) is None:
            continue
        completed = subprocess.run(
            package_query,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=10,
            text=True,
        )
        package_output = completed.stdout.strip()
        if completed.returncode == 0 and package_output:
            return package_output
    return "unavailable"


def resolve_simulator(simulator_name: str) -> pathlib.Path:
    """Resolve the requested executable before hashing or launching it."""
    resolved_name = shutil.which(simulator_name)
    if resolved_name is None:
        raise ValueError(f"simulator is unavailable: {simulator_name}")
    simulator_path = pathlib.Path(resolved_name).resolve()
    if not simulator_path.is_file():
        raise ValueError(f"simulator is not a regular file: {simulator_path}")
    return simulator_path


def validate_image(image_path: pathlib.Path) -> str:
    """Reject every image except the pinned, externally supplied V7 disk."""
    if not image_path.is_file():
        raise ValueError(f"image is not a regular file: {image_path}")
    image_size = image_path.stat().st_size
    if image_size != EXPECTED_IMAGE_SIZE:
        raise ValueError(
            f"image size {image_size} differs from expected {EXPECTED_IMAGE_SIZE}"
        )
    image_digest = file_sha256(image_path)
    if image_digest != EXPECTED_IMAGE_SHA256:
        raise ValueError(
            f"image SHA-256 {image_digest} differs from expected {EXPECTED_IMAGE_SHA256}"
        )
    return image_digest


def validate_profile(profile_path: pathlib.Path) -> str:
    """Admit only the reviewed SIMH command surface."""
    if not profile_path.is_file():
        raise ValueError(f"profile is not a regular file: {profile_path}")
    profile_digest = file_sha256(profile_path)
    if profile_digest != EXPECTED_PROFILE_SHA256:
        raise ValueError(
            f"profile SHA-256 {profile_digest} differs from expected "
            f"{EXPECTED_PROFILE_SHA256}"
        )
    return profile_digest


def run_reference(
    simulator_path: pathlib.Path,
    image_path: pathlib.Path,
    profile_path: pathlib.Path,
    timeout_seconds: int,
) -> tuple[bytes, str]:
    """Run SIMH against a private image copy and preserve the supplied image."""
    original_digest = file_sha256(image_path)
    if original_digest != EXPECTED_IMAGE_SHA256:
        raise RuntimeError("the image changed after admission")
    with tempfile.TemporaryDirectory(
        prefix="discobsd-pdp11-reference-"
    ) as directory_name:
        temporary_directory = pathlib.Path(directory_name)
        working_image = temporary_directory / "v7.dsk"
        shutil.copyfile(image_path, working_image)
        if file_sha256(working_image) != EXPECTED_IMAGE_SHA256:
            raise RuntimeError("the private image copy differs from the admitted image")
        completed = subprocess.run(
            [str(simulator_path), str(profile_path)],
            cwd=temporary_directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=timeout_seconds,
        )
    if completed.returncode != 0:
        raise RuntimeError(
            f"simulator exited with status {completed.returncode}:\n"
            + completed.stdout.decode("utf-8", errors="replace")
        )
    final_digest = file_sha256(image_path)
    if final_digest != original_digest:
        raise RuntimeError(
            "the externally supplied image changed during the oracle run"
        )
    return completed.stdout, normalize_transcript(completed.stdout)


def write_evidence(
    evidence_directory: pathlib.Path,
    provenance: dict[str, object],
    raw_output: bytes,
    normalized_output: str,
) -> None:
    """Write bounded evidence only when the caller names its destination."""
    evidence_directory.mkdir(parents=True, exist_ok=True)
    raw_output_path = evidence_directory / "simulator-output.bin"
    normalized_output_path = evidence_directory / "normalized-transcript.txt"
    provenance_path = evidence_directory / "provenance.json"
    for evidence_path in (raw_output_path, normalized_output_path, provenance_path):
        if evidence_path.exists():
            raise FileExistsError(f"retained evidence already exists: {evidence_path}")
    with raw_output_path.open("xb") as raw_output_file:
        raw_output_file.write(raw_output)
    with normalized_output_path.open("x", encoding="ascii") as normalized_output_file:
        normalized_output_file.write(normalized_output)
    with provenance_path.open("x", encoding="ascii") as provenance_file:
        provenance_file.write(json.dumps(provenance, indent=2, sort_keys=True) + "\n")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", default="simh-pdp11")
    parser.add_argument("--image", required=True, type=pathlib.Path)
    parser.add_argument("--profile", required=True, type=pathlib.Path)
    parser.add_argument("--expected", required=True, type=pathlib.Path)
    parser.add_argument("--evidence-dir", type=pathlib.Path)
    parser.add_argument("--timeout", type=int, default=60)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        image_path = arguments.image.resolve()
        profile_path = arguments.profile.resolve()
        expected_path = arguments.expected.resolve()
        simulator_path = resolve_simulator(arguments.simulator)
        image_digest = validate_image(image_path)
        profile_digest = validate_profile(profile_path)
        raw_output, normalized_output = run_reference(
            simulator_path, image_path, profile_path, arguments.timeout
        )
        expected_output = expected_path.read_text(encoding="ascii")
        if normalized_output != expected_output:
            raise RuntimeError(
                "normalized guest output differs from the pinned transcript:\n"
                + normalized_output
            )

        provenance: dict[str, object] = {
            "image_sha256": image_digest,
            "image_size": image_path.stat().st_size,
            "package_identity": package_identity(simulator_path),
            "profile_sha256": profile_digest,
            "simulator_path": str(simulator_path),
            "simulator_sha256": file_sha256(simulator_path),
            "simulator_version": simulator_version(simulator_path),
            "transcript_sha256": hashlib.sha256(
                normalized_output.encode("ascii")
            ).hexdigest(),
        }
        if arguments.evidence_dir is not None:
            write_evidence(
                arguments.evidence_dir.resolve(),
                provenance,
                raw_output,
                normalized_output,
            )
        print(json.dumps(provenance, indent=2, sort_keys=True))
        print("PDP-11 V7 reference behavior passed")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"pdp11 reference: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
