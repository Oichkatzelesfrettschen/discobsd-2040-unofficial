"""Calibrate synchronization using production identities and real generation."""

import concurrent.futures
import os
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
        self.mutate(sync.COMPILE / "PICO/Config", 'architecture    "rp2040"',
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
