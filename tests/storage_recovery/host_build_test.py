"""Calibrate compiler-selected shared sanitizer runtime resolution."""

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from host_build import sanitizer_link_flags


class RuntimeResolutionTest(unittest.TestCase):
    def check_clang(self, builtins, library, platform="linux", installed=True):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / "installed runtime"
            runtime.mkdir()
            shared = runtime / library
            if installed:
                shared.touch()
            compiler = ["clang", "--target=test-target"]
            calls = []

            def output(command, **kwargs):
                self.assertEqual(command[:len(compiler)], compiler)
                option = command[len(compiler):]
                calls.append(option)
                if option == ["-dM", "-E", "-x", "c", "-"]:
                    return "#define __clang__ 1\n"
                if option == ["--print-runtime-dir"]:
                    return str(Path(temporary) / "missing-target-directory")
                if option == ["--rtlib=compiler-rt", "-print-libgcc-file-name"]:
                    return str(runtime / builtins)
                if option == ["-print-file-name=" + library]:
                    return str(shared) if installed else library
                self.fail(f"unexpected compiler query: {option}")

            with patch("host_build.subprocess.check_output", side_effect=output), \
                    patch("host_build.sys", SimpleNamespace(platform=platform), create=True):
                if installed:
                    self.assertEqual(sanitizer_link_flags(compiler), [
                        "-fsanitize=undefined", "-shared-libsan", "-Xlinker", "-rpath",
                        "-Xlinker", str(runtime)])
                else:
                    with self.assertRaisesRegex(RuntimeError, "UBSan runtime"):
                        sanitizer_link_flags(compiler)
            self.assertIn(["-print-file-name=" + library], calls)

    def test_target_directory_layout(self):
        self.check_clang("libclang_rt.builtins.a", "libclang_rt.ubsan_standalone.so")

    def test_architecture_suffix_layout(self):
        self.check_clang("libclang_rt.builtins-x86_64.a",
                         "libclang_rt.ubsan_standalone-x86_64.so")

    def test_arm_suffix_layout(self):
        self.check_clang("libclang_rt.builtins-armhf.a",
                         "libclang_rt.ubsan_standalone-armhf.so")

    def test_macos_dynamic_runtime(self):
        self.check_clang("libclang_rt.osx.a", "libclang_rt.ubsan_osx_dynamic.dylib",
                         platform="darwin")

    def test_missing_runtime_is_rejected(self):
        self.check_clang("libclang_rt.builtins.a", "libclang_rt.ubsan_standalone.so",
                         installed=False)

    def test_gcc_uses_driver_linkage(self):
        with patch("host_build.subprocess.check_output", return_value="#define __GNUC__ 13") \
                as query:
            self.assertEqual(sanitizer_link_flags(["gcc"]), ["-fsanitize=undefined"])
            self.assertEqual(query.call_count, 1)


if __name__ == "__main__":
    unittest.main()
