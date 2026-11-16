"""Calibrate matched ARM compiler-object attribution and artifact identity."""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import compiler_matrix_identity as identity

ROOT = Path(__file__).resolve().parents[2]
ANALYZER = ROOT / "tools/analyze_compiler_objects.py"


def run(command, cwd, expected=0):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"{command!r}: exit {result.returncode}: {result.stderr}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", required=True)
    parser.add_argument("--objdump", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="compiler-object-attribution-") as temporary:
        directory = Path(temporary)
        root, capture = directory / "repo", directory / "capture"
        root.mkdir()
        capture.mkdir()
        source = root / "fixture.c"
        source.write_text("int target(int value) { return value * 3 + 1; }\n", encoding="utf-8")
        run(["git", "init", "-q"], root)
        run(["git", "config", "user.name", "Fixture"], root)
        run(["git", "config", "user.email", "fixture@example.invalid"], root)
        run(["git", "add", "fixture.c"], root)
        run(["git", "commit", "-qm", "fixture"], root)
        (capture / "source-manifest.json").write_text(
            json.dumps(identity.snapshot(root, [], [])), encoding="utf-8"
        )
        objdump = args.objdump.resolve(strict=True)
        (capture / "tool-inputs.json").write_text(json.dumps({
            "gcc16_objdump": {"path": str(objdump),
                              "sha256": hashlib.sha256(objdump.read_bytes()).hexdigest()}
        }), encoding="utf-8")
        rows = []
        for level in ("O0", "Os"):
            row = capture / f"gcc16-{level}"
            row.mkdir()
            run([args.gcc, "-Wall", "-Wextra", "-Werror", "-mcpu=cortex-m0plus", "-mthumb",
                 "-fstack-usage", f"-{level}", "-c", str(source), "-o", str(row / "unit.o")], root)
            report = row / "unit.su"
            report_hash = hashlib.sha256(report.read_bytes()).hexdigest()
            rows.append({"compiler": "gcc16", "level": level,
                         "stack_usage_sha256": {"unit.su": report_hash},
                         "artifacts_sha256": identity.artifact_hashes(row)})
        (capture / "results.json").write_text(json.dumps(rows), encoding="utf-8")
        output = directory / "analysis"
        command = [sys.executable, str(ANALYZER), "--capture", str(capture),
                   "--compiler", "gcc16", "--objdump", str(objdump), "--output", str(output)]
        run(command, root)
        record = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        assert record["object_deltas"][0]["object"] == "unit.o"
        assert record["object_deltas"][0]["exec_delta"] > 0
        assert any(entry["name"] == "target" and entry["delta"] > 0
                   for entry in record["function_deltas"])
        assert "target" in (output / "gcc16-O0/unit.o.objdump").read_text(encoding="utf-8")
        obj = capture / "gcc16-O0/unit.o"
        original = obj.read_bytes()
        obj.write_bytes(original + b"changed")
        corrupt = command.copy()
        corrupt[corrupt.index(str(output))] = str(directory / "corrupt")
        run(corrupt, root, expected=1)
        assert not (directory / "corrupt/manifest.json").exists()
        obj.write_bytes(original)
        obj.write_bytes(b"not an ELF")
        rows[0]["artifacts_sha256"] = identity.artifact_hashes(capture / "gcc16-O0")
        (capture / "results.json").write_text(json.dumps(rows), encoding="utf-8")
        malformed = command.copy()
        malformed[malformed.index(str(output))] = str(directory / "malformed")
        run(malformed, root, expected=1)
        assert not (directory / "malformed/manifest.json").exists()
        obj.write_bytes(original)
        rows[0]["artifacts_sha256"] = identity.artifact_hashes(capture / "gcc16-O0")
        (capture / "results.json").write_text(json.dumps(rows), encoding="utf-8")
        extra = capture / "gcc16-Os/unexpected.o"
        extra.write_bytes(original)
        unexpected = command.copy()
        unexpected[unexpected.index(str(output))] = str(directory / "unexpected")
        run(unexpected, root, expected=1)
        extra.unlink()
        wrong_tool = command.copy()
        wrong_tool[wrong_tool.index(str(output))] = str(directory / "wrong-tool")
        wrong_tool[wrong_tool.index(str(objdump))] = str(Path(args.gcc).resolve())
        run(wrong_tool, root, expected=1)
    print("PASS compiler-object attribution and identity mutations")


if __name__ == "__main__":
    main()
