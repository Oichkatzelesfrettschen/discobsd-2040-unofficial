"""Calibrate synchronization using production identities and real generation."""

import concurrent.futures
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import check_generated_sync as sync


class GeneratedSyncTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-config-fixture-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        sync.copy_inputs(sync.ROOT, self.root, expected=True)

    def mutate(self, relative, before, after):
        path = self.root / relative
        contents = path.read_text()
        self.assertIn(before, contents)
        path.write_text(contents.replace(before, after, 1))

    def test_typed_startup_callbacks(self):
        header = sync.ROOT / "sys/sys/config.h"
        good = """#include "config-contract.h"
static int controller(struct conf_ctlr *c) { return c->ctlr_unit == 3; }
static int device(struct conf_device *d) { return d->dev_unit == 7; }
static int attached;
static void service(void) { attached++; }
static struct driver bus = { .d_name = "bus", .d_ctlr_init = controller };
static struct driver disk = { .d_name = "disk", .d_device_init = device };
int main(void) {
    struct conf_ctlr c = { .ctlr_driver = &bus, .ctlr_unit = 3 };
    struct conf_device d = { .dev_driver = &disk, .dev_unit = 7 };
    struct conf_service s = { service };
    s.svc_attach();
    return !(c.ctlr_driver->d_ctlr_init(&c) &&
             d.dev_driver->d_device_init(&d) && attached == 1);
}
"""
        (self.root / "config-contract.h").write_bytes(header.read_bytes())
        cc = shlex.split(os.environ.get("HOST_CC", "cc"))
        flags = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                 "-Wstrict-prototypes", "-Wold-style-definition"]
        source = self.root / "callbacks.c"
        source.write_text(good)
        binary = self.root / "callbacks"
        subprocess.run(cc + flags + [str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        for before, after in [
            (".d_ctlr_init = controller", ".d_ctlr_init = device"),
            (".d_device_init = device", ".d_device_init = controller"),
            ("static void service(void)", "static void service(int unused)"),
        ]:
            source.write_text(good.replace(before, after))
            rejected = subprocess.run(cc + flags + ["-fsyntax-only", str(source)],
                                      capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b"incompatible", rejected.stderr)

    def test_generated_service_has_no_arguments(self):
        generator = sync.build_generator(self.root, "bmake")
        directory = self.root / sync.COMPILE / "PICO"
        config = directory / "Config"
        config.write_text(config.read_text() + "\nservice pty\n")
        sync.generate(generator, directory, "PICO")
        output = (directory / "ioconf.c").read_text()
        self.assertIn("void ptyattach(void);", output)
        self.assertIn("{ ptyattach }", output)

    def test_synchronized_pair_without_kernel_or_include_link(self):
        self.assertFalse((self.root / "include/machine").exists())
        for board in sync.BOARDS:
            self.assertFalse((self.root / sync.COMPILE / board / "unix").exists())
        before = {path.relative_to(self.root): path.read_bytes()
                  for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(set(sync.check(self.root)), set(sync.BOARDS))
        after = {path.relative_to(self.root): path.read_bytes()
                 for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(before, after)

    def test_template_change_requires_regeneration(self):
        path = self.root / sync.CONF / "Makefile.rp2040"
        path.write_text(path.read_text() + "# synchronization fixture\n")
        with self.assertRaisesRegex(sync.SyncError, "PICO: generated Makefile differs"):
            sync.check(self.root)

    def test_unknown_template_directive_is_rejected_despite_exit_zero(self):
        path = self.root / sync.CONF / "Makefile.rp2040"
        path.write_text(path.read_text() + "%SYNC_INVALID\n")
        with self.assertRaisesRegex(sync.SyncError, "config generator diagnostic.*"):
            sync.check(self.root)

    def test_config_change_requires_regeneration(self):
        self.mutate(sync.COMPILE / "PICO_UART/Config", "115200", "57600")
        with self.assertRaisesRegex(sync.SyncError, "PICO_UART: generated Makefile differs"):
            sync.check(self.root)

    def test_direct_generated_edit_is_rejected(self):
        path = self.root / sync.COMPILE / "PICO/Makefile"
        path.write_text(path.read_text() + "# direct output edit\n")
        with self.assertRaisesRegex(sync.SyncError, "regenerated/sys/arch/rp2040/compile/PICO"):
            sync.check(self.root)

    def test_missing_generated_output_is_rejected(self):
        (self.root / sync.COMPILE / "PICO_UART/Makefile").unlink()
        with self.assertRaisesRegex(sync.SyncError, "PICO_UART: missing tracked output"):
            sync.check(self.root)

    def test_missing_configuration_is_rejected(self):
        (self.root / sync.COMPILE / "PICO/Config").unlink()
        with self.assertRaisesRegex(sync.SyncError, "missing required input.*PICO/Config"):
            sync.check(self.root)

    def test_missing_generator_source_is_rejected(self):
        (self.root / "tools/config/config.y").unlink()
        with self.assertRaisesRegex(sync.SyncError, "missing required input.*config.y"):
            sync.check(self.root)

    def test_variant_config_drift_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC/Config", 'options         "NFL=2"',
                    'options         "NFL=3"')
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_PSRAM: Config must equal"):
            sync.check(self.root)

    def test_diagnostic_option_missing_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO_DIAG/Config",
                    "options         CAPACITY_STATS              # fixed-table and u-area peaks\n",
                    "")
        with self.assertRaisesRegex(sync.SyncError, "PICO_DIAG: Config must equal"):
            sync.check(self.root)

    def test_lean_base_with_diagnostic_option_is_rejected(self):
        path = self.root / sync.COMPILE / "PICO/Config"
        path.write_text(path.read_text() + "options CAPACITY_STATS\n")
        with self.assertRaisesRegex(sync.SyncError, "PICO_DIAG: Config must equal"):
            sync.check(self.root)

    def test_variant_without_its_device_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_PSRAM/Config", "device          psram\n", "")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_PSRAM: Config must equal"):
            sync.check(self.root)

    def test_base_config_carrying_the_variant_device_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC/Config", "device          picocalc\n",
                    "device          picocalc\ndevice          psram\n")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_PSRAM: Config must equal"):
            sync.check(self.root)

    def test_production_identity_is_preserved(self):
        self.mutate(sync.COMPILE / "PICO/Config", 'board           "PICO"',
                    'board           "SYNTHETIC"')
        with self.assertRaisesRegex(sync.SyncError, "production board identity"):
            sync.check(self.root)

    def test_file_list_is_a_real_input(self):
        path = self.root / sync.CONF / "files.rp2040"
        path.write_text(path.read_text() + "rp2040/sync_fixture.c standard\n")
        with self.assertRaisesRegex(sync.SyncError, "generated Makefile differs"):
            sync.check(self.root)

    def test_kern_time_uses_size_optimized_compile_rule(self):
        template = (self.root / sync.CONF / "Makefile.rp2040").read_text()
        self.assertIn("COMPILE_SIZE_C=", template)
        for board in sync.BOARDS:
            generated = (self.root / sync.COMPILE / board / "Makefile").read_text()
            rule = "kern_time.o: $S/kern/kern_time.c\n\t${COMPILE_SIZE_C}\n"
            self.assertIn(rule, generated)

    def test_kernel_optimization_and_aliasing_policy(self):
        template = (self.root / sync.CONF / "Makefile.rp2040").read_text()
        self.assertIn("COPTS?=\t\t-Os\n", template)
        self.assertIn("-fno-strict-aliasing", template)
        # The cycle-counted rule compiles at -O whatever COPTS says.
        cycle = ("COMPILE_CYCLE_C=${CC} ${DEBUG} ${CWARNFLAGS} ${CSTDFLAGS} \\\n"
                 "\t\t${CMACHFLAGS} -O ${DEFS} -c $<\n")
        self.assertIn(cycle, template)
        self.assertNotIn("COPTS", cycle)
        for board in sync.BOARDS:
            generated = (self.root / sync.COMPILE / board / "Makefile").read_text()
            self.assertIn("COPTS?=\t\t-Os\n", generated)
            self.assertIn("-fno-strict-aliasing", generated)
            self.assertIn(cycle, generated)

    def test_alignment_sort_mode_is_explicit(self):
        sync.copy_file(sync.ROOT, self.root, sync.CONF / "compiler.mk")
        for board in sync.BOARDS:
            directory = self.root / sync.COMPILE / board
            environment = sync.child_environment()
            for value, enabled in (("yes", True), ("no", False)):
                result = subprocess.run(
                    ["bmake", "LIBGCC=", f"KERNEL_SORT_ALIGNMENT={value}",
                     "-V", "${PARAM} ${LDFLAGS}"],
                    cwd=directory, env=environment, text=True, capture_output=True,
                    check=True,
                )
                self.assertEqual("--sort-section=alignment" in result.stdout, enabled)
                self.assertEqual("-DKERNEL_SORT_ALIGNMENT" in result.stdout, enabled)
            result = subprocess.run(
                ["bmake", "LIBGCC=", "KERNEL_SORT_ALIGNMENT=invalid", "-V", "PARAM"],
                cwd=directory, env=environment, text=True, capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("KERNEL_SORT_ALIGNMENT must be yes or no", result.stderr)

    def test_psram_transfer_uses_cycle_counted_compile_rule(self):
        generated = (self.root / sync.COMPILE / "PICOCALC_PSRAM/Makefile").read_text()
        rule = ("psram_xfer.o: $S/arch/rp2040/dev/psram_xfer.c\n"
                "\t${COMPILE_CYCLE_C}\n")
        self.assertIn(rule, generated)

    def test_board_file_list_is_a_real_input(self):
        path = self.root / sync.COMPILE / "PICO_UART/files.PICO_UART"
        path.write_text("rp2040/sync_fixture.c standard\n")
        with self.assertRaisesRegex(sync.SyncError, "PICO_UART: generated Makefile differs"):
            sync.check(self.root)

    def test_synchronized_template_edit_passes(self):
        addition = "# synchronized fixture\n"
        for relative in [sync.CONF / "Makefile.rp2040"] + [
            sync.COMPILE / board / "Makefile" for board in sync.BOARDS
        ]:
            path = self.root / relative
            path.write_text(path.read_text() + addition)
        sync.check(self.root)

    def test_unrelated_source_edit_passes(self):
        path = self.root / "sys/kern/unrelated.c"
        path.parent.mkdir(parents=True)
        path.write_text("/* Unrelated to config generation. */\n")
        sync.check(self.root)

    def test_generator_failure_is_rejected(self):
        for board in ("PICO", "PICO_DIAG"):
            self.mutate(sync.COMPILE / board / "Config", 'architecture    "rp2040"',
                        'architecture    "invalid"')
        with self.assertRaisesRegex(sync.SyncError, "PICO: config generator: exit"):
            sync.check(self.root)

    def test_success_without_generated_file_is_rejected(self):
        generator = self.root / "empty-generator"
        generator.write_text("#!/bin/sh\nexit 0\n")
        generator.chmod(0o755)
        directory = self.root / "empty-output"
        directory.mkdir()
        with self.assertRaisesRegex(sync.SyncError, "PICO: generator produced no Makefile"):
            sync.generate(generator, directory, "PICO")

    def test_concurrent_invocations_have_private_builds(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(sync.check, [self.root, self.root]))
        self.assertEqual(results[0], results[1])
        self.assertFalse((self.root / "tools/config/config").exists())

    def test_selected_host_compiler_is_required(self):
        with patch.dict(os.environ, {"HOST_CC": "false"}):
            with self.assertRaisesRegex(sync.SyncError, "generator build: exit"):
                sync.check(self.root)

    def test_root_recipe_exports_host_compiler_override(self):
        environment = sync.child_environment()
        result = subprocess.run(
            ["bmake", "-n", "-C", str(sync.ROOT), "MACHINE=rp2040",
             "HOST_CC=cc -O1", "check-config-generated-sync"],
            env=environment, text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.count("HOST_CC=cc\\ -O1 "), 2)


if __name__ == "__main__":
    unittest.main()
