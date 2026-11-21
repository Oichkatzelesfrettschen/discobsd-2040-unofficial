"""Calibrate synchronization using production identities and real generation."""

import concurrent.futures
import os
import shutil
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

    def test_service_callback_has_no_arguments(self):
        configuration = self.root / sync.COMPILE / "PICO/Config"
        with configuration.open("a", encoding="utf-8") as stream:
            stream.write("\nservice pty\n")
        generator = sync.build_generator(self.root, "bmake")
        sync.generate(generator, configuration.parent, "PICO service fixture")
        generated = (configuration.parent / "ioconf.c").read_text(encoding="utf-8")
        self.assertIn("void ptyattach(void);", generated)
        self.assertIn("{ ptyattach }", generated)

    def test_stm32_controller_and_device_descriptors(self):
        architecture = Path("sys/arch/stm32")
        shutil.copytree(sync.ROOT / architecture / "conf", self.root / architecture / "conf")
        directory = self.root / architecture / "compile/F405WEACTCORE"
        directory.mkdir(parents=True)
        shutil.copyfile(sync.ROOT / architecture / "compile/F405WEACTCORE/Config",
                        directory / "Config")
        generator = sync.build_generator(self.root, "bmake")
        sync.generate(generator, directory, "STM32 controller fixture")
        generated = (directory / "ioconf.c").read_text(encoding="utf-8")
        self.assertIn("extern struct controller_driver sdiodriver;", generated)
        self.assertIn("extern const struct driver sddriver;", generated)
        self.assertIn("&sddriver,\t&sdiodriver", generated)

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

    SYSTRACE_LINE = "options         SYSTRACE                    # kern.systrace console trace\n"
    CAPACITY_LINE = "options         CAPACITY_STATS              # fixed-table and u-area peaks\n"
    PCM_LINE = "options         DEV_PCM                     # /dev/audio sample output, cdevsw 13\n"
    PSRAM_LINE = "device          psram\n"
    PANEL_LINE = "options         LCD_PANELTEST               # panel register sets and readback\n"

    def test_variant_config_drift_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC/Config", 'options         "NFL=2"',
                    'options         "NFL=3"')
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_sd_variant_rejects_a_generated_flash_root(self):
        self.mutate(sync.COMPILE / "PICOCALC_SD/swapunix.c", "rootdev = makedev(5, 1)",
                    "rootdev = makedev(0, 1)")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_SD: generated swapunix.c differs"):
            sync.check(self.root)

    def test_sd_variant_keeps_the_full_panel(self):
        self.mutate(sync.COMPILE / "PICOCALC_SD/Config", "options         LCD_PANELTEST", "")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_SD: Config must equal"):
            sync.check(self.root)

    def test_lean_variant_without_a_kept_device_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN/Config", self.PSRAM_LINE, "")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_lean_variant_carrying_a_device_option_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + self.PCM_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_lean_variant_carrying_systrace_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + self.SYSTRACE_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_lean_variant_carrying_the_panel_test_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + self.PANEL_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_diagnostic_variant_carrying_the_panel_test_is_rejected(self):
        # LEAN_DIAG has no /dev/lcdgfx for the panel test to run through.
        self.mutate(sync.COMPILE / "PICOCALC_LEAN_DIAG/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + self.PANEL_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN_DIAG: Config must equal"):
            sync.check(self.root)

    def test_full_kernel_carrying_the_panel_test_is_rejected(self):
        # The panel test lives in PICOCALC_SD alone; a PICOCALC that regains
        # it no longer equals PICOCALC_LEAN plus the lean kernel's cuts.
        self.mutate(sync.COMPILE / "PICOCALC/Config", self.CAPACITY_LINE,
                    self.CAPACITY_LINE + self.PANEL_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_full_kernel_carrying_systrace_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC/Config", self.CAPACITY_LINE,
                    self.SYSTRACE_LINE + self.CAPACITY_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN: Config must equal"):
            sync.check(self.root)

    def test_sd_variant_without_systrace_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_SD/Config", "options         SYSTRACE\n", "")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_SD: Config must equal"):
            sync.check(self.root)

    def test_full_kernel_without_a_device_option_is_rejected(self):
        # The lean kernel's removal list names DEV_PCM, so a PICOCALC that
        # loses it no longer differs from PICOCALC_LEAN by that cut.
        self.mutate(sync.COMPILE / "PICOCALC/Config", self.PCM_LINE, "")
        with self.assertRaisesRegex(
                sync.SyncError,
                "PICOCALC_LEAN: PICOCALC Config lacks the directive options DEV_PCM"):
            sync.check(self.root)

    def test_full_kernel_without_capacity_stats_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC/Config", self.CAPACITY_LINE, "")
        with self.assertRaisesRegex(
                sync.SyncError,
                "PICOCALC_LEAN: PICOCALC Config lacks the directive options CAPACITY_STATS"):
            sync.check(self.root)

    def test_lean_pico_carrying_capacity_stats_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO/Config", 'options         "CORE1"',
                    self.CAPACITY_LINE + 'options         "CORE1"')
        with self.assertRaisesRegex(sync.SyncError, "PICO_DIAG: Config must equal"):
            sync.check(self.root)

    def test_pico_diagnostic_kernel_without_systrace_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO_DIAG/Config", self.SYSTRACE_LINE, "")
        with self.assertRaisesRegex(sync.SyncError, "PICO_DIAG: Config must equal"):
            sync.check(self.root)

    def test_pico_diagnostic_kernel_drifting_from_pico_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO_DIAG/Config", '"SWAPRAM_KB=16"', '"SWAPRAM_KB=32"')
        with self.assertRaisesRegex(sync.SyncError, "PICO_DIAG: Config must equal"):
            sync.check(self.root)

    def test_diagnostic_variant_with_an_unlisted_option_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN_DIAG/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + "options         UNLISTED_DIAGNOSTIC\n")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN_DIAG: Config must equal"):
            sync.check(self.root)

    def test_diagnostic_variant_without_an_inherited_option_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN_DIAG/Config", self.SYSTRACE_LINE, "")
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN_DIAG: Config must equal"):
            sync.check(self.root)

    def test_diagnostic_variant_with_a_device_option_is_rejected(self):
        self.mutate(sync.COMPILE / "PICOCALC_LEAN_DIAG/Config", self.PSRAM_LINE,
                    self.PSRAM_LINE + self.PCM_LINE)
        with self.assertRaisesRegex(sync.SyncError, "PICOCALC_LEAN_DIAG: Config must equal"):
            sync.check(self.root)

    def test_removal_variant_equals_its_base_without_the_removed_lines(self):
        # PICO is PICO_DIAG without the diagnostic options, so the pair read
        # the other way round is a variant that removes directives.
        removal = (("PICO", "PICO_DIAG", (), sync.DIAGNOSTIC_OPTIONS),)
        with patch.object(sync, "VARIANTS", removal):
            sync.check_variant_delta(self.root)

    def test_removal_variant_keeping_a_removed_line_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO/Config", 'options         "CORE1"',
                    self.CAPACITY_LINE + 'options         "CORE1"')
        removal = (("PICO", "PICO_DIAG", (), sync.DIAGNOSTIC_OPTIONS),)
        with patch.object(sync, "VARIANTS", removal):
            with self.assertRaisesRegex(
                    sync.SyncError,
                    r"PICO: Config must equal PICO_DIAG Config \(minus options SYSTRACE"):
                sync.check_variant_delta(self.root)

    def test_removal_the_base_lacks_is_rejected(self):
        self.mutate(sync.COMPILE / "PICO_DIAG/Config", self.SYSTRACE_LINE, "")
        removal = (("PICO", "PICO_DIAG", (), sync.DIAGNOSTIC_OPTIONS),)
        with patch.object(sync, "VARIANTS", removal):
            with self.assertRaisesRegex(
                    sync.SyncError,
                    "PICO: PICO_DIAG Config lacks the directive options SYSTRACE"):
                sync.check_variant_delta(self.root)

    # Every PicoCalc kernel carries the psram device and the third mount slot
    # /etc/rc.rp2040 mounts the PSRAM /tmp on, so the production rows describe
    # NMOUNT=3 in all three. A mount-table size that differs between the full
    # kernel and the lean kernel, with the diagnostic lean kernel following
    # the full one, is a variant that removes the base's value and adds its
    # own: PICOCALC_LEAN drops PICOCALC's NMOUNT=3 for NMOUNT=2, and
    # PICOCALC_LEAN_DIAG drops that NMOUNT=2 for NMOUNT=3 beside the
    # diagnostic options.
    MOUNT_TABLE = (
        ("PICO_DIAG", "PICO", sync.DIAGNOSTIC_OPTIONS, ()),
        ("PICOCALC_LEAN", "PICOCALC", ('options "NMOUNT=2"',),
         sync.OPTIONAL_DEVICE_OPTIONS + ('options CAPACITY_STATS',
                                         'options "NMOUNT=3"')),
        ("PICOCALC_LEAN_DIAG", "PICOCALC_LEAN",
         sync.DIAGNOSTIC_OPTIONS + ('options "NMOUNT=3"',),
         ('options "NMOUNT=2"',)),
    )

    def lower_mount_table(self, *boards):
        for board in boards:
            self.mutate(sync.COMPILE / board / "Config",
                        'options         "NMOUNT=3"', 'options         "NMOUNT=2"')

    def test_production_configs_carry_the_psram_tmp_mount_slot(self):
        for board in ("PICOCALC", "PICOCALC_LEAN", "PICOCALC_LEAN_DIAG"):
            text = (self.root / sync.COMPILE / board / "Config").read_text(
                encoding="utf-8")
            self.assertIn('options         "NMOUNT=3"', text, board)

    def test_lean_variant_keeping_the_two_mount_table_is_expressible(self):
        self.lower_mount_table("PICOCALC_LEAN")
        with patch.object(sync, "VARIANTS", self.MOUNT_TABLE):
            sync.check_variant_delta(self.root)

    def test_lean_variant_following_the_full_mount_table_is_rejected(self):
        with patch.object(sync, "VARIANTS", self.MOUNT_TABLE):
            with self.assertRaisesRegex(sync.SyncError,
                                        "PICOCALC_LEAN: Config must equal PICOCALC Config"):
                sync.check_variant_delta(self.root)

    def test_lean_kernel_with_the_two_mount_table_is_rejected(self):
        # The lean kernel must keep the slot the PSRAM /tmp mount needs.
        self.lower_mount_table("PICOCALC_LEAN")
        with self.assertRaisesRegex(sync.SyncError,
                                    "PICOCALC_LEAN: Config must equal PICOCALC Config"):
            sync.check_variant_delta(self.root)

    def test_diagnostic_lean_kernel_with_the_two_mount_table_is_rejected(self):
        self.lower_mount_table("PICOCALC_LEAN_DIAG")
        with self.assertRaisesRegex(
                sync.SyncError,
                "PICOCALC_LEAN_DIAG: Config must equal PICOCALC_LEAN Config"):
            sync.check_variant_delta(self.root)

    def test_full_kernel_with_the_two_mount_table_is_rejected(self):
        self.lower_mount_table("PICOCALC")
        with self.assertRaisesRegex(sync.SyncError,
                                    "PICOCALC_LEAN: Config must equal PICOCALC Config"):
            sync.check_variant_delta(self.root)

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

    def test_prototype_warnings_are_fatal_on_admitted_kernel_units(self):
        template = (self.root / sync.CONF / "Makefile.rp2040").read_text()
        self.assertIn("COMPILE_SIZE_C=", template)
        self.assertRegex(template, r"(?m)^C17_PROTO_FLAGS=[ \t]+"
                         r"-Werror=strict-prototypes -Werror=old-style-definition$")
        for board in sync.BOARDS:
            generated = (self.root / sync.COMPILE / board / "Makefile").read_text()
            for unit in ("kern_proc", "kern_resource", "kern_time", "tty_subr", "vm_sched"):
                compile_rule = "${COMPILE_SIZE_C}" if unit == "kern_time" else "${COMPILE_C}"
                rule = (f"{unit}.o: $S/kern/{unit}.c\n\t{compile_rule}"
                        " ${C17_PROTO_FLAGS}\n")
                self.assertIn(rule, generated)

    def test_kernel_optimization_and_aliasing_policy(self):
        template = (self.root / sync.CONF / "Makefile.rp2040").read_text()
        self.assertIn("COPTS?=\t\t-Os\n", template)
        self.assertIn("-fno-strict-aliasing", template)
        # The cycle-counted rule compiles at -O whatever COPTS says.
        cycle = ("COMPILE_CYCLE_C=${CC} ${DEBUG} ${DEBUG_PATH_MAP} ${CWARNFLAGS} ${CSTDFLAGS} \\\n"
                 "\t\t${CMACHFLAGS} -O ${DEFS} -c $<\n")
        self.assertIn(cycle, template)
        self.assertNotIn("COPTS", cycle)
        for board in sync.BOARDS:
            generated = (self.root / sync.COMPILE / board / "Makefile").read_text()
            self.assertIn("COPTS?=\t\t-Os\n", generated)
            self.assertIn("-fno-strict-aliasing", generated)
            self.assertIn(cycle, generated)

    def test_psram_transfer_uses_cycle_counted_compile_rule(self):
        rule = ("psram_xfer.o: $S/arch/rp2040/dev/psram_xfer.c\n"
                "\t${COMPILE_CYCLE_C}\n")
        for board in ("PICOCALC", "PICOCALC_LEAN", "PICOCALC_LEAN_DIAG"):
            generated = (self.root / sync.COMPILE / board / "Makefile").read_text()
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
        # PICO_DIAG repeats PICO's directives, so both carry the mutation and
        # the variant check passes; the generator must then fail on PICO.
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
