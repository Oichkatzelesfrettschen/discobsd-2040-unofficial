"""check.sh's dependency check: a skip with a reason by default, a failure
naming the module under SWAP_TIER_MODEL_REQUIRED=1."""

import os
import shlex
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run_without_hypothesis(tmp_path, required):
    shadow = tmp_path / "shadow"
    shadow.mkdir()
    (shadow / "hypothesis.py").write_text(
        "raise ImportError('shadowed for the gate test')\n", encoding="utf-8")
    wrapper = tmp_path / "python"
    # The paths are data: quoted, so a space or a shell character in the
    # temporary directory or the interpreter path stays part of the name.
    wrapper.write_text(f"#!/bin/sh\nPYTHONPATH={shlex.quote(str(shadow))} exec "
                       f"{shlex.quote(sys.executable)} \"$@\"\n", encoding="utf-8")
    wrapper.chmod(0o755)
    env = dict(os.environ, PYTHON=str(wrapper))
    env.pop("SWAP_TIER_MODEL_REQUIRED", None)
    if required:
        env["SWAP_TIER_MODEL_REQUIRED"] = "1"
    return subprocess.run(["sh", str(HERE / "check.sh")], env=env,
                          capture_output=True, text=True, check=False)


def test_missing_module_skips_with_reason(tmp_path):
    result = run_without_hypothesis(tmp_path, required=False)
    assert result.returncode == 0
    assert "SKIP" in result.stdout and "hypothesis" in result.stdout


def test_missing_module_fails_when_required(tmp_path):
    result = run_without_hypothesis(tmp_path, required=True)
    assert result.returncode == 1
    assert "FAIL" in result.stderr and "hypothesis" in result.stderr


def test_wrapper_paths_with_spaces_stay_literal(tmp_path):
    spaced = tmp_path / "swap gate; echo injected"
    spaced.mkdir()
    result = run_without_hypothesis(spaced, required=True)
    assert result.returncode == 1
    assert "hypothesis" in result.stderr
    assert "injected" not in result.stdout + result.stderr.replace(str(spaced), "")
