"""Resolve the sanitizer runtime for shared libraries loaded by Python."""

import subprocess
from pathlib import Path


def sanitizer_link_flags(compiler):
    macros = subprocess.check_output(
        [*compiler, "-dM", "-E", "-x", "c", "-"], input="", text=True)
    flags = ["-fsanitize=undefined"]
    if "#define __clang__ " in macros:
        runtime = subprocess.check_output(
            [*compiler, "--print-runtime-dir"], text=True).strip()
        if not runtime or not Path(runtime).is_dir():
            raise RuntimeError("Clang sanitizer runtime directory is unavailable")
        flags += ["-shared-libsan", "-Xlinker", "-rpath", "-Xlinker", runtime]
    return flags
