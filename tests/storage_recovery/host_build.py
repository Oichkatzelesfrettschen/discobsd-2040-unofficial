"""Resolve the sanitizer runtime for shared libraries loaded by Python."""

import subprocess
import sys
from pathlib import Path


def sanitizer_link_flags(compiler):
    macros = subprocess.check_output(
        [*compiler, "-dM", "-E", "-x", "c", "-"], input="", text=True)
    flags = ["-fsanitize=undefined"]
    if "#define __clang__ " in macros:
        if sys.platform == "darwin":
            name = "libclang_rt.ubsan_osx_dynamic.dylib"
        else:
            builtins = Path(subprocess.check_output(
                [*compiler, "--rtlib=compiler-rt", "-print-libgcc-file-name"],
                text=True).strip()).name
            prefix = "libclang_rt.builtins"
            if not builtins.startswith(prefix) or not builtins.endswith(".a"):
                raise RuntimeError("Clang compiler-rt library name is unavailable")
            suffix = builtins[len(prefix):-2]
            name = "libclang_rt.ubsan_standalone" + suffix + ".so"
        runtime = Path(subprocess.check_output(
            [*compiler, "-print-file-name=" + name], text=True).strip())
        if not runtime.is_file():
            raise RuntimeError("Clang shared UBSan runtime is unavailable: " + str(runtime))
        flags += ["-shared-libsan", "-Xlinker", "-rpath", "-Xlinker", str(runtime.parent)]
    return flags
