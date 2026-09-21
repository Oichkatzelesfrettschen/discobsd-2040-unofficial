"""Run a pinned V7 behavior oracle under an external PDP-11 simulator."""

from __future__ import annotations

import argparse
import ctypes
import dataclasses
import errno
import hashlib
import json
import os
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
EXPECTED_TRANSCRIPT_SHA256 = (
    "22cbde3ee4913104173cd4cf9787705261c03ad8a1a36076076f3e16f8f5b781"
)
EXPECTED_SIMULATOR_VERSION = (
    "PDP-11 simulator Open SIMH V4.1-0 Current git commit id: a1f57fa3"
)
BEGIN_MARKER = "PDP11_ORACLE_BEGIN"
END_MARKER = "PDP11_ORACLE_END"
GUEST_PROMPT = "PDP11_PROMPT"
INODE_PATTERN = re.compile(r"^\s*([0-9]+) (/oracle\.dir/[ab])$")
AT_FDCWD = -100
RENAME_NOREPLACE = 1


@dataclasses.dataclass(frozen=True)
class ReferenceResult:
    """Outputs and admitted simulator identity from one private run."""

    raw_output: bytes
    normalized_output: str
    simulator_sha256: str
    simulator_version: str


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

    begin_indices = [
        line_index
        for line_index, output_line in enumerate(output_lines)
        if output_line == BEGIN_MARKER
    ]
    end_indices = [
        line_index
        for line_index, output_line in enumerate(output_lines)
        if output_line == END_MARKER
    ]
    if len(begin_indices) != 1 or len(end_indices) != 1:
        raise ValueError("guest transcript requires exactly one begin and end marker")
    begin_index = begin_indices[0]
    end_index = end_indices[0]
    if begin_index >= end_index:
        raise ValueError("guest transcript oracle markers are out of order")

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
    if completed.returncode != 0:
        raise RuntimeError(
            f"simulator version probe exited with status {completed.returncode}"
        )
    output_lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    banner_lines = [
        " ".join(output_line.split())
        for output_line in output_lines
        if "PDP-11 simulator" in output_line
    ]
    if len(banner_lines) != 1:
        raise RuntimeError("simulator invocation did not report exactly one banner")
    return banner_lines[0]


def validate_simulator_version(simulator_path: pathlib.Path) -> str:
    """Admit the pinned Open SIMH release before any guest command runs."""
    version = simulator_version(simulator_path)
    if version != EXPECTED_SIMULATOR_VERSION:
        raise ValueError(
            f"simulator version {version!r} differs from expected "
            f"{EXPECTED_SIMULATOR_VERSION!r}"
        )
    return version


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


def validate_profile(profile_path: pathlib.Path) -> tuple[str, bytes]:
    """Admit only the reviewed SIMH command surface."""
    if not profile_path.is_file():
        raise ValueError(f"profile is not a regular file: {profile_path}")
    profile_contents = profile_path.read_bytes()
    profile_digest = hashlib.sha256(profile_contents).hexdigest()
    if profile_digest != EXPECTED_PROFILE_SHA256:
        raise ValueError(
            f"profile SHA-256 {profile_digest} differs from expected "
            f"{EXPECTED_PROFILE_SHA256}"
        )
    return profile_digest, profile_contents


def validate_expected(expected_path: pathlib.Path) -> tuple[str, str]:
    """Read and admit the exact reviewed transcript before guest execution."""
    if not expected_path.is_file():
        raise ValueError(f"expected transcript is not a regular file: {expected_path}")
    expected_bytes = expected_path.read_bytes()
    expected_digest = hashlib.sha256(expected_bytes).hexdigest()
    if expected_digest != EXPECTED_TRANSCRIPT_SHA256:
        raise ValueError(
            f"expected transcript SHA-256 {expected_digest} differs from expected "
            f"{EXPECTED_TRANSCRIPT_SHA256}"
        )
    try:
        expected_output = expected_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("expected transcript is not ASCII") from error
    return expected_digest, expected_output


def preflight_evidence_directory(evidence_directory: pathlib.Path) -> None:
    """Require an absent destination under an existing directory."""
    if os.path.lexists(evidence_directory):
        raise FileExistsError(
            f"retained evidence destination already exists: {evidence_directory}"
        )
    if not evidence_directory.parent.is_dir():
        raise ValueError(
            f"retained evidence parent is not a directory: {evidence_directory.parent}"
        )


def write_exclusive_file(output_path: pathlib.Path, contents: bytes) -> None:
    """Create one staged file and flush its complete contents."""
    with output_path.open("xb") as output_file:
        output_file.write(contents)
        output_file.flush()
        os.fsync(output_file.fileno())


def sync_directory(directory_path: pathlib.Path) -> None:
    """Flush directory entries required by a retained evidence transaction."""
    open_flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    directory_descriptor = os.open(directory_path, open_flags)
    try:
        os.fsync(directory_descriptor)
    finally:
        os.close(directory_descriptor)


def publish_directory_noreplace(
    staging_directory: pathlib.Path,
    evidence_directory: pathlib.Path,
) -> None:
    """Publish one Linux directory rename without replacing any destination."""
    if not sys.platform.startswith("linux"):
        raise RuntimeError(
            "retained evidence publication requires Linux renameat2 support"
        )
    standard_library = ctypes.CDLL(None, use_errno=True)
    try:
        renameat2 = standard_library.renameat2
    except AttributeError as error:
        raise RuntimeError("the C library does not expose Linux renameat2") from error
    renameat2.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    ]
    renameat2.restype = ctypes.c_int
    result = renameat2(
        AT_FDCWD,
        os.fsencode(staging_directory),
        AT_FDCWD,
        os.fsencode(evidence_directory),
        RENAME_NOREPLACE,
    )
    if result == 0:
        return
    error_number = ctypes.get_errno()
    if error_number == errno.EEXIST:
        raise FileExistsError(
            error_number,
            os.strerror(error_number),
            evidence_directory,
        )
    if error_number == errno.ENOSYS:
        raise RuntimeError("the Linux kernel does not support renameat2")
    raise OSError(
        error_number,
        os.strerror(error_number),
        evidence_directory,
    )


def run_reference(
    simulator_path: pathlib.Path,
    image_path: pathlib.Path,
    profile_contents: bytes,
    timeout_seconds: int,
) -> ReferenceResult:
    """Probe and run one private simulator snapshot against admitted inputs."""
    original_digest = file_sha256(image_path)
    if original_digest != EXPECTED_IMAGE_SHA256:
        raise RuntimeError("the image changed after admission")
    if hashlib.sha256(profile_contents).hexdigest() != EXPECTED_PROFILE_SHA256:
        raise RuntimeError("the simulator profile changed after admission")
    try:
        with tempfile.TemporaryDirectory(
            prefix="discobsd-pdp11-reference-"
        ) as directory_name:
            temporary_directory = pathlib.Path(directory_name)
            working_image = temporary_directory / "v7.dsk"
            shutil.copyfile(image_path, working_image)
            if file_sha256(working_image) != EXPECTED_IMAGE_SHA256:
                raise RuntimeError(
                    "the private image copy differs from the admitted image"
                )
            working_profile = temporary_directory / "v7_rl02.simh"
            write_exclusive_file(working_profile, profile_contents)
            simulator_snapshot = temporary_directory / "simh-pdp11"
            shutil.copyfile(simulator_path, simulator_snapshot)
            simulator_snapshot.chmod(0o700)
            simulator_digest = file_sha256(simulator_snapshot)
            admitted_simulator_version = validate_simulator_version(simulator_snapshot)
            completed = subprocess.run(
                [str(simulator_snapshot), str(working_profile)],
                cwd=temporary_directory,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=timeout_seconds,
            )
    finally:
        final_digest = file_sha256(image_path)
        if final_digest != original_digest:
            raise RuntimeError(
                "the externally supplied image changed during the oracle run"
            )
    if completed.returncode != 0:
        raise RuntimeError(
            f"simulator exited with status {completed.returncode}:\n"
            + completed.stdout.decode("utf-8", errors="replace")
        )
    return ReferenceResult(
        raw_output=completed.stdout,
        normalized_output=normalize_transcript(completed.stdout),
        simulator_sha256=simulator_digest,
        simulator_version=admitted_simulator_version,
    )


def write_evidence(
    evidence_directory: pathlib.Path,
    provenance: dict[str, object],
    raw_output: bytes,
    normalized_output: str,
) -> None:
    """Stage a complete evidence bundle and publish it with one rename."""
    preflight_evidence_directory(evidence_directory)
    staging_name = tempfile.mkdtemp(
        prefix=f".{evidence_directory.name}.stage-",
        dir=evidence_directory.parent,
    )
    staging_directory = pathlib.Path(staging_name)
    try:
        write_exclusive_file(
            staging_directory / "simulator-output.bin",
            raw_output,
        )
        write_exclusive_file(
            staging_directory / "normalized-transcript.txt",
            normalized_output.encode("ascii"),
        )
        provenance_bytes = (
            json.dumps(provenance, indent=2, sort_keys=True) + "\n"
        ).encode("ascii")
        write_exclusive_file(
            staging_directory / "provenance.json",
            provenance_bytes,
        )
        sync_directory(staging_directory)
        publish_directory_noreplace(staging_directory, evidence_directory)
        sync_directory(evidence_directory.parent)
    finally:
        if staging_directory.exists():
            shutil.rmtree(staging_directory)


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
        if arguments.timeout <= 0:
            raise ValueError("timeout must be greater than zero")
        evidence_directory = None
        if arguments.evidence_dir is not None:
            evidence_directory = pathlib.Path(os.path.abspath(arguments.evidence_dir))
            preflight_evidence_directory(evidence_directory)
        image_path = arguments.image.resolve()
        profile_path = arguments.profile.resolve()
        expected_path = arguments.expected.resolve()
        image_digest = validate_image(image_path)
        profile_digest, profile_contents = validate_profile(profile_path)
        expected_digest, expected_output = validate_expected(expected_path)
        simulator_path = resolve_simulator(arguments.simulator)
        reference_result = run_reference(
            simulator_path,
            image_path,
            profile_contents,
            arguments.timeout,
        )
        if reference_result.normalized_output != expected_output:
            raise RuntimeError(
                "normalized guest output differs from the pinned transcript:\n"
                + reference_result.normalized_output
            )

        provenance: dict[str, object] = {
            "expected_sha256": expected_digest,
            "image_sha256": image_digest,
            "image_size": image_path.stat().st_size,
            "package_identity": package_identity(simulator_path),
            "profile_sha256": profile_digest,
            "simulator_path": str(simulator_path),
            "simulator_sha256": reference_result.simulator_sha256,
            "simulator_version": reference_result.simulator_version,
            "transcript_sha256": hashlib.sha256(
                reference_result.normalized_output.encode("ascii")
            ).hexdigest(),
        }
        if evidence_directory is not None:
            write_evidence(
                evidence_directory,
                provenance,
                reference_result.raw_output,
                reference_result.normalized_output,
            )
        print(json.dumps(provenance, indent=2, sort_keys=True))
        print("PDP-11 V7 reference behavior passed")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"pdp11 reference: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
