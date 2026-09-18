"""Calibrate warning enforcement against successful and rejected compilations.

Every fixture includes or copies the production makefile under test. No fixture
invokes a board, installs a file outside its temporary directory, or changes the
source tree. The ARM compiler is required: a host compiler cannot validate the
cross-build's effective command line.
"""

import argparse
import os
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("--make", default="bmake")
PARSER.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
OPTIONS = PARSER.parse_args()
ROOT = OPTIONS.root.resolve()
MAKE = shlex.split(OPTIONS.make)
GOOD = "int probe(int value) { return value; }\n"
WARNING = '#warning warning_policy_fixture\n' + GOOD
EXTRA = "int probe(int value) { return 0; }\n"


class WarningPolicy(unittest.TestCase):
    def setUp(self):
        self.storage = tempfile.TemporaryDirectory(prefix="discobsd-warnings-")
        self.addCleanup(self.storage.cleanup)
        self.work = Path(self.storage.name)
        self.environment = os.environ.copy()
        # A subprocess must not inherit closed bmake job-server descriptors or
        # the caller's -f/target overrides; each test chooses its own make mode.
        for name in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
            self.environment.pop(name, None)

    def write(self, path, content):
        dest = self.work / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)

    def make(self, *arguments, cwd=None):
        return subprocess.run(
            MAKE + ["MACHINE=rp2040", f"TOPSRC={ROOT}", f"S={ROOT / 'sys'}", "BUILD=0"]
            + list(arguments),
            cwd=cwd or self.work,
            env=self.environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
            check=False,
        )

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout)

    def assert_warning_failure(self, result, diagnostic):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(diagnostic, result.stdout)
        self.assertFalse((self.work / "probe.o").exists())

    def simple_fixture(self, fragment, compiler="CC"):
        self.write(
            "Makefile",
            f'.include "{ROOT / fragment}"\n'
            # Deliberately discard inherited CFLAGS, as several real leaves do.
            'CFLAGS= -O0\n'
            'probe.o: probe.c\n'
            f'\t${{{compiler}}} ${{CFLAGS}} -c probe.c -o probe.o\n',
        )

    def check_pair(self):
        self.write("probe.c", GOOD)
        self.assert_success(self.make("probe.o"))
        self.assertTrue((self.work / "probe.o").exists())
        (self.work / "probe.o").unlink()
        self.write("probe.c", WARNING)
        self.assert_warning_failure(self.make("probe.o"), "warning_policy_fixture")

    def test_target_survives_replaced_cflags(self):
        self.simple_fixture("share/mk/sys.mk")
        self.check_pair()

    def test_host_tools_survive_replaced_cflags(self):
        self.simple_fixture("tools/Makefile.inc")
        self.check_pair()

    def test_host_generators_use_shared_policy(self):
        self.simple_fixture("share/mk/sys.mk", "HOST_CC")
        self.check_pair()

    def test_sort_accepts_shared_host_command(self):
        result = self.make("test", f"PYTHON={sys.executable}", cwd=ROOT / "usr.bin/sort")
        self.assert_success(result)
        self.assertIn("sort: byte domain", result.stdout)

    def test_archive_accepts_host_command_arguments(self):
        # Exercise the same argv interface as the production make recipe.
        result = self.make("-V", "${HOST_CC}", cwd=ROOT / "usr.bin/as/tests")
        self.assert_success(result)
        command = shlex.split(result.stdout.strip())
        self.assertIn("-Werror", command)
        environment = self.environment | {
            "AR": str(ROOT / "tools/bin/ar"),
            "RANLIB": str(ROOT / "tools/bin/ranlib"),
            "SOURCE_ROOT": str(ROOT / "usr.bin/as/tests"),
            "WORK": str(self.work),
        }
        result = subprocess.run(
            ["sh", str(ROOT / "usr.bin/as/tests/archive-transactional-rewrite.sh"),
             *command], env=environment, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=60, check=False,
        )
        self.assert_success(result)
        self.assertIn("archive:", result.stdout)

    def kernel_fixture(self, config="PICO", suffix="c"):
        self.write(
            "Makefile",
            f'S= {ROOT / "sys"}\n'
            f'.include "{ROOT / "sys/arch/rp2040/compile" / config / "Makefile"}"\n'
            f'probe.o: probe.{suffix} ${{PARAMSTAMP}} .deps\n'
            f'\t${{COMPILE_{suffix.upper()}}}\n'
            'version-test:\n'
            '\t${SYSTEM_LD_HEAD}\n'
            '\ttouch reached\n',
        )

    def test_both_kernel_configs_enable_extra_and_error(self):
        for config in ("PICO", "PICO_UART"):
            with self.subTest(config=config):
                self.kernel_fixture(config)
                self.write("probe.c", GOOD)
                self.assert_success(self.make("probe.o"))
                (self.work / "probe.o").unlink()
                self.write("probe.c", EXTRA)
                self.assert_warning_failure(self.make("probe.o"), "unused-parameter")

    def test_kernel_assembler_warning_is_fatal(self):
        self.kernel_fixture(suffix="S")
        source = ".text\n.globl probe\nprobe:\n bx lr\n"
        self.write("probe.S", source)
        self.assert_success(self.make("probe.o"))
        (self.work / "probe.o").unlink()
        self.write("probe.S", '.warning "warning_policy_fixture"\n' + source)
        self.assert_warning_failure(self.make("probe.o"), "warning_policy_fixture")

    def test_flag_change_rebuilds_existing_object(self):
        self.kernel_fixture()
        self.write("probe.c", GOOD)
        self.assert_success(self.make("probe.o"))
        obj = self.work / "probe.o"
        first = obj.stat().st_mtime_ns
        signature = (self.work / ".params").read_bytes()
        self.assert_success(self.make("probe.o"))
        self.assertEqual(first, obj.stat().st_mtime_ns)
        self.assertEqual(signature, (self.work / ".params").read_bytes())
        # Older supported make implementations compare whole-second mtimes.
        time.sleep(1.05)
        self.assert_success(self.make("CWARNFLAGS=-Wall -Wextra -Werror -Wshadow", "probe.o"))
        self.assertNotEqual(first, obj.stat().st_mtime_ns)
        self.assertNotEqual(signature, (self.work / ".params").read_bytes())

    def test_signature_quotes_shell_metacharacters(self):
        self.kernel_fixture()
        # -V only: recording flags must not execute shell metacharacters. The
        # compiler command is not run and does not need to accept this value.
        marker = self.work / "injected"
        poison = f"value'; touch {marker}; #"
        self.assert_success(self.make(f"COPTS={poison}", "-V", "PARAMSTAMP"))
        self.assertFalse(marker.exists())
        self.assertIn(poison, (self.work / ".params").read_text())

    def test_version_compile_failure_cannot_be_masked_by_cleanup(self):
        self.kernel_fixture()
        self.assert_success(self.make("version-test"))
        self.assertTrue((self.work / "reached").exists())
        (self.work / "reached").unlink()
        result = self.make("CC=false", "LIBGCC=", "version-test")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse((self.work / "reached").exists())

    def test_native_library_failures_reach_parent(self):
        for source, targets in (("lib/libc_aout/Makefile", ("all", "install")),
                                ("lib/Makefile", ("install",))):
            for target in targets:
                for jobs in ((), ("-j2",)):
                    with self.subTest(source=source, target=target, jobs=jobs):
                        self.write("Makefile", (ROOT / source).read_text())
                        self.write("elf32-arm.ld", "")
                        self.write("first/Makefile", "all install:\n\ttrue\n")
                        self.write("later/Makefile", "all install:\n\ttouch ../reached\n")
                        self.assert_success(self.make(*jobs, "SUBDIR=first later", target))
                        self.assertTrue((self.work / "reached").exists())
                        (self.work / "reached").unlink()
                        self.write("first/Makefile", "all install:\n\texit 42\n")
                        result = self.make(*jobs, "SUBDIR=first later", target)
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertFalse((self.work / "reached").exists())

    def test_root_tools_target_propagates_compiler_failure(self):
        self.write("Makefile", (ROOT / "Makefile").read_text())
        self.write("tools/Makefile",
                   f'.include "{ROOT / "tools/Makefile.inc"}"\n'
                   'CFLAGS= -O0\n'
                   'install: probe.o\n'
                   'probe.o: probe.c\n'
                   '\t${CC} ${CFLAGS} -c probe.c -o probe.o\n')
        for jobs in ((), ("-j2",)):
            with self.subTest(jobs=jobs):
                self.write("tools/probe.c", GOOD)
                self.assert_success(self.make(*jobs, "tools"))
                (self.work / "tools/probe.o").unlink()
                self.write("tools/probe.c", WARNING)
                result = self.make(*jobs, "tools")
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("warning_policy_fixture", result.stdout)
                self.assertFalse((self.work / "tools/probe.o").exists())

    def test_real_leaf_commands_preserve_native_assembler(self):
        for leaf in ("usr.bin/smlrc", "lib/libc_aout/libc"):
            with self.subTest(leaf=leaf):
                result = self.make("-V", "${CC}", "-V", "${CFLAGS}", cwd=ROOT / leaf)
                self.assert_success(result)
                self.assertIn("-Werror", result.stdout)
                self.assertIn("-mcpu=cortex-m0plus", result.stdout)
                if "libc_aout" in leaf:
                    self.assertIn("-B", result.stdout)
                    self.assertIn("-Wa,-x", result.stdout)


if __name__ == "__main__":
    unittest.main(argv=[__file__], verbosity=2)
