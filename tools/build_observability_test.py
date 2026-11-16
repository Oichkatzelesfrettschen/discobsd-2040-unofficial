"""Calibrate the firmware workflow's bounded, retained build logging."""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import check_build_observability as observability

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = """\
jobs:
  fixture:
    timeout-minutes: 20
    runs-on: ubuntu-24.04
    steps:
      - name: Run the fixture
        timeout-minutes: 5
        run: |
          "$PYTHON" tools/run_logged_gate.py \\
            --label fixture --log .build-logs/fixture.log \\
            --status .build-logs/fixture.status.json \\
            --idle-timeout-seconds 60 --timeout-seconds 240 \\
            -- bmake MACHINE=rp2040 build
      - name: Retain fixture logs
        if: always()
        uses: actions/upload-artifact@v4
        with:
          path: .build-logs/
          include-hidden-files: true
"""
MAKEFILE_FIXTURE = """\
HOST_GATES= check-run-logging check-callgraph-capture \\
  check-rp2040-storage-delta check-compiler-matrix-harness

check-run-logging: check-python
\t${PYTHON} tools/run_logged_gate_test.py
\t${PYTHON} tools/run_logged_build_check_test.py
\t${PYTHON} tools/build_observability_test.py
\t${PYTHON} tools/storage_counter_driver_test.py

check-compiler-matrix-harness: check-python
\t${PYTHON} tests/compiler_matrix/check.py

check-callgraph-capture: check-python
\t${PYTHON} tests/callgraph/check.py

check-rp2040-storage-delta: check-python
\t${PYTHON} tests/rp2040/storage_delta_test.py

check-qemu: check-python
\t${MAKE} check-exec-dispatch-qemu

check-exec-dispatch-qemu: check-python
\t${PYTHON} tests/kernel/exec_dispatch_qemu.py \\
\t    --gcc ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-gcc \\
\t    --clang ${CLANG:Uclang:Q}

CROSS_CONTRACT_GATES= check-compiler-object-attribution
check-cross: check-cross-contracts check-cross-kernel
check-cross-contracts: ${CROSS_CONTRACT_GATES}
check-cross-kernel: check-rp2040-execsw .WAIT check-rp2040-vectors .WAIT \\
  check-rp2040-syscalls .WAIT check-rp2040-devsw .WAIT \\
  check-rp2040-fileops .WAIT check-rp2040-ioconf .WAIT \\
  check-rp2040-tty-roots .WAIT check-rp2040-callout-roots .WAIT \\
  check-rp2040-nlist .WAIT check-kernel-headroom

check-compiler-object-attribution: check-python
\t${PYTHON} tests/compiler_matrix/object_attribution.py \\
\t    --gcc ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-gcc \\
\t    --objdump ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-objdump

check-rp2040-execsw: check-python
\t${PYTHON} tests/rp2040_execsw/check.py --kernel sys/arch/rp2040/compile/PICO/unix.elf
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t${PYTHON} tools/check_rp2040_execsw.py sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf
.endfor

check-rp2040-vectors: check-python
\t${PYTHON} tests/rp2040_vectors/check.py --kernel sys/arch/rp2040/compile/PICO/unix.elf
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t${PYTHON} tools/check_rp2040_vectors.py sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf
.endfor

check-rp2040-syscalls: check-python
\t${PYTHON} tests/rp2040_syscalls/check.py --kernel sys/arch/rp2040/compile/PICO/unix.elf
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@SYSCALL_CHECK@
.endfor

check-rp2040-devsw: check-python
.for KERNEL_CONFIGURATION in PICO PICOCALC_PSRAM
\t@DEVSW_FIXTURE@
.endfor
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@DEVSW_CHECK@
.endfor

check-rp2040-fileops: check-python
\t${PYTHON} tests/rp2040_fileops/check.py --kernel sys/arch/rp2040/compile/PICO/unix.elf
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t${PYTHON} tools/check_rp2040_fileops.py sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf
.endfor

check-rp2040-ioconf: check-python
.for KERNEL_CONFIGURATION in PICO PICOCALC_PSRAM
\t@IOCONF_FIXTURE@
.endfor
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@IOCONF_CHECK@
.endfor

check-rp2040-tty-roots: check-python
\t${PYTHON} tests/rp2040_tty_roots/check.py --build-dir sys/arch/rp2040/compile/PICOCALC_PSRAM
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@TTY_ROOT_CHECK@
.endfor

check-rp2040-callout-roots: check-python
\t@CALLOUT_FIXTURE@
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@CALLOUT_CHECK@
.endfor

check-rp2040-nlist: check-python
\t@NLIST_FIXTURE@
.for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}
\t@NLIST_CHECK@
.endfor

logged-build: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-build-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 build

logged-distribution: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-distribution-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 distribution

logged-flash: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-flash-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 flash

logged-kernel: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-kernel-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 kernel

logged-ilp32: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-ilp32-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 check-ilp32-execution

logged-check: check-python
\t${PYTHON} tools/run_logged_build_check.py --label rp2040-check-local \\
\t    --evidence-root .build-logs \\
\t    --execution-lock .build-logs/rp2040-products.lock \\
\t    --idle-timeout-seconds 600 --timeout-seconds 5400 \\
\t    -- ${MAKE} MACHINE=rp2040
"""
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@NLIST_FIXTURE@",
    "${PYTHON} tests/rp2040_nlist/check.py --object sys/arch/rp2040/compile/PICO/sysctl.o "
    "--kernel sys/arch/rp2040/compile/PICO/unix.elf",
).replace(
    "@NLIST_CHECK@",
    "${PYTHON} tools/check_rp2040_nlist.py --object "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/sysctl.o "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf",
)
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@SYSCALL_CHECK@",
    "${PYTHON} tools/check_rp2040_syscalls.py --master sys/kern/syscalls.master "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf",
)
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@TTY_ROOT_CHECK@",
    "${PYTHON} tools/check_rp2040_tty_roots.py --build-dir "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}",
)
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@CALLOUT_FIXTURE@",
    "${PYTHON} tests/rp2040_callout_roots/check.py --build-dir "
    "sys/arch/rp2040/compile/PICOCALC_PSRAM",
).replace(
    "@CALLOUT_CHECK@",
    "${PYTHON} tools/check_rp2040_callout_roots.py --build-dir "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}",
)
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@IOCONF_FIXTURE@",
    "${PYTHON} tests/rp2040_ioconf/check.py --kernel "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf --object "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/ioconf.o",
).replace(
    "@IOCONF_CHECK@",
    "${PYTHON} tools/check_rp2040_ioconf.py --object "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/ioconf.o "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf",
)
MAKEFILE_FIXTURE = MAKEFILE_FIXTURE.replace(
    "@DEVSW_FIXTURE@",
    "${PYTHON} tests/rp2040_devsw/check.py --kernel "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf --object "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/conf.o",
).replace(
    "@DEVSW_CHECK@",
    "${PYTHON} tools/check_rp2040_devsw.py --object "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/conf.o "
    "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf",
)


class BuildObservabilityTest(unittest.TestCase):
    def validate(self, source):
        observability.validate_workflow(
            source,
            expected_jobs={"fixture"},
            expected_labels={"fixture"},
        )

    def test_known_good_fixture_is_accepted(self):
        self.validate(FIXTURE)

    def test_job_deadline_is_required(self):
        source = FIXTURE.replace("    timeout-minutes: 20\n", "", 1)
        with self.assertRaisesRegex(ValueError, "job fixture.*timeout-minutes"):
            self.validate(source)

    def test_step_deadline_is_required(self):
        source = FIXTURE.replace("        timeout-minutes: 5\n", "", 1)
        with self.assertRaisesRegex(ValueError, "fixture.*step timeout-minutes"):
            self.validate(source)

    def test_runner_idle_and_hard_deadlines_are_required(self):
        for option in ("--idle-timeout-seconds 60 ", "--timeout-seconds 240 "):
            with self.subTest(option=option):
                source = FIXTURE.replace(option, "", 1)
                with self.assertRaisesRegex(ValueError, "fixture.*deadline"):
                    self.validate(source)

    def test_direct_bmake_invocation_is_rejected(self):
        source = re.sub(
            r'          "\$PYTHON" tools/run_logged_gate\.py.*?'
            r"(?=      - name: Retain fixture logs)",
            "          bmake MACHINE=rp2040 build\n",
            FIXTURE,
            flags=re.DOTALL,
        )
        with self.assertRaisesRegex(ValueError, "direct bmake"):
            self.validate(source)

    def test_always_retained_logs_are_required(self):
        source = FIXTURE.replace("        if: always()\n", "", 1)
        with self.assertRaisesRegex(ValueError, "fixture.*always"):
            self.validate(source)

    def test_hidden_logs_must_be_included_in_the_artifact(self):
        source = FIXTURE.replace("          include-hidden-files: true\n", "")
        with self.assertRaisesRegex(ValueError, "fixture.*hidden"):
            self.validate(source)

    def test_commented_runner_is_rejected(self):
        source = FIXTURE.replace(
            '          "$PYTHON" tools/run_logged_gate.py \\\n',
            '          # "$PYTHON" tools/run_logged_gate.py \\\n',
            1,
        )
        with self.assertRaisesRegex(ValueError, "runner"):
            self.validate(source)

    def test_env_prefixed_direct_bmake_is_rejected(self):
        source = FIXTURE.replace(
            "      - name: Retain fixture logs\n",
            "      - name: Unbounded build\n"
            "        run: env BUILD_FIXTURE=yes bmake MACHINE=rp2040 build\n"
            "      - name: Retain fixture logs\n",
            1,
        )
        with self.assertRaisesRegex(ValueError, "direct bmake"):
            self.validate(source)

    def test_duplicate_deadline_option_is_rejected(self):
        source = FIXTURE.replace(
            "--idle-timeout-seconds 60 --timeout-seconds 240 \\\n",
            "--idle-timeout-seconds 60 --timeout-seconds 240 "
            "--timeout-seconds 0 \\\n",
            1,
        )
        with self.assertRaisesRegex(ValueError, "fixture.*hard deadline"):
            self.validate(source)

    def test_equals_form_duplicate_deadline_option_is_rejected(self):
        source = FIXTURE.replace(
            "--idle-timeout-seconds 60 --timeout-seconds 240 \\\n",
            "--idle-timeout-seconds 60 --timeout-seconds 240 "
            "--timeout-seconds=0 \\\n",
            1,
        )
        with self.assertRaisesRegex(ValueError, "fixture.*hard deadline"):
            self.validate(source)

    def test_path_qualified_direct_bmake_is_rejected(self):
        source = FIXTURE.replace(
            "      - name: Retain fixture logs\n",
            "      - name: Unbounded absolute build\n"
            "        run: /usr/bin/bmake MACHINE=rp2040 build\n"
            "      - name: Retain fixture logs\n",
            1,
        )
        with self.assertRaisesRegex(ValueError, "direct bmake"):
            self.validate(source)

    def test_repository_labels_are_bound_to_expected_commands(self):
        source = (ROOT / ".github/workflows/firmware.yml").read_text(
            encoding="utf-8"
        ).replace(
            '-- bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 build',
            "-- true",
            1,
        )
        with self.assertRaisesRegex(ValueError, "linux-build.*command"):
            observability.validate_workflow(source)

    def test_storage_prefix_runner_binds_the_parent_range(self):
        source = (ROOT / ".github/workflows/firmware.yml").read_text(
            encoding="utf-8"
        ).replace("STORAGE_PREFIX_PARENT_BLOCKS=1", "STORAGE_PREFIX_PARENT_BLOCKS=10", 1)
        with self.assertRaisesRegex(ValueError, "runner linux-storage-prefix command differs"):
            observability.validate_workflow(source)

    def test_repository_workflow_satisfies_the_contract(self):
        observability.check(ROOT)

    def test_check_rejects_unbounded_host_workflow(self):
        with tempfile.TemporaryDirectory(prefix="discobsd-host-observability-") as temporary:
            root = Path(temporary)
            workflow_directory = root / ".github/workflows"
            workflow_directory.mkdir(parents=True)
            (workflow_directory / "firmware.yml").write_text(
                (ROOT / ".github/workflows/firmware.yml").read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            (workflow_directory / "host.yml").write_text(
                (ROOT / ".github/workflows/host.yml").read_text(
                    encoding="utf-8"
                ).replace("    timeout-minutes: 20\n", "", 1),
                encoding="utf-8",
            )
            (root / "Makefile").write_text(MAKEFILE_FIXTURE, encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "host workflow"):
                observability.check(root)

    def test_host_workflow_failure_surfaces_are_calibrated(self):
        source = (ROOT / ".github/workflows/host.yml").read_text(encoding="utf-8")
        mutations = (
            (
                source.replace("        timeout-minutes: 10\n", "", 1),
                "step.*deadline",
            ),
            (
                source.replace(".build-logs/host-test.log", ".build-logs/missing.log"),
                "decision log",
            ),
            (source.replace("        if: always()\n", "", 1), "always"),
            (
                source.replace("          include-hidden-files: true\n", "", 1),
                "hidden",
            ),
        )
        for candidate, diagnostic in mutations:
            with self.subTest(diagnostic=diagnostic):
                with self.assertRaisesRegex(ValueError, diagnostic):
                    observability.validate_host_workflow(candidate)

    def test_host_tee_pipeline_requires_explicit_pipefail_shell(self):
        source = (ROOT / ".github/workflows/host.yml").read_text(encoding="utf-8")
        candidate = source.replace("        shell: bash\n", "", 1)

        with self.assertRaisesRegex(ValueError, "tee pipeline.*bash"):
            observability.validate_host_workflow(candidate)

    def test_host_test_requires_a_streaming_log(self):
        source = (ROOT / ".github/workflows/host.yml").read_text(encoding="utf-8")
        candidate = source.replace(
            ".build-logs/host-test.log", ".build-logs/missing-test.log"
        )

        with self.assertRaisesRegex(ValueError, "decision log"):
            observability.validate_host_workflow(candidate)

    def test_host_powershell_tee_preserves_native_failure(self):
        source = (ROOT / ".github/workflows/host.yml").read_text(encoding="utf-8")
        candidate = source.replace(
            "          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\n",
            "",
            1,
        )

        with self.assertRaisesRegex(ValueError, "Tee-Object.*LASTEXITCODE"):
            observability.validate_host_workflow(candidate)

    def test_makefile_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        mutations = (
            ("HOST_GATES= check-run-logging", "HOST_GATES="),
            ("${PYTHON} tools/run_logged_gate_test.py", "true"),
            ("${PYTHON} tools/run_logged_build_check_test.py", "true"),
            ("${PYTHON} tools/build_observability_test.py", "true"),
            ("${PYTHON} tools/storage_counter_driver_test.py", "true"),
        )
        for original, replacement in mutations:
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-run-logging"):
                    observability.validate_makefile(source)

    def test_matrix_harness_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-compiler-matrix-harness\n", "\n"),
            ("${PYTHON} tests/compiler_matrix/check.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-compiler-matrix-harness"):
                    observability.validate_makefile(source)

    def test_callgraph_capture_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-callgraph-capture ", " "),
            ("${PYTHON} tests/callgraph/check.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-callgraph-capture"):
                    observability.validate_makefile(source)

    def test_storage_delta_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-storage-delta ", " "),
            ("${PYTHON} tests/rp2040/storage_delta_test.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-storage-delta"):
                    observability.validate_makefile(source)

    def test_execsw_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            ("check-cross: check-cross-contracts check-cross-kernel",
             "check-cross: check-cross-contracts"),
            ("check-cross-kernel: check-rp2040-execsw", "check-cross-kernel:"),
            ("${PYTHON} tests/rp2040_execsw/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_execsw.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-execsw"):
                    observability.validate_makefile(source)

    def test_vectors_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-vectors ", " "),
            ("${PYTHON} tests/rp2040_vectors/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_vectors.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-vectors"):
                    observability.validate_makefile(source)

    def test_syscalls_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-syscalls ", " "),
            ("${PYTHON} tests/rp2040_syscalls/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_syscalls.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-syscalls"):
                    observability.validate_makefile(source)

    def test_devsw_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-devsw ", " "),
            ("${PYTHON} tests/rp2040_devsw/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_devsw.py", "true"),
            (".for KERNEL_CONFIGURATION in PICO PICOCALC_PSRAM",
             ".for KERNEL_CONFIGURATION in PICO"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-devsw"):
                    observability.validate_makefile(source)

    def test_fileops_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-fileops ", " "),
            ("${PYTHON} tests/rp2040_fileops/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_fileops.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-fileops"):
                    observability.validate_makefile(source)

    def test_ioconf_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-ioconf ", " "),
            ("${PYTHON} tests/rp2040_ioconf/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_ioconf.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-ioconf"):
                    observability.validate_makefile(source)

    def test_tty_roots_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-tty-roots ", " "),
            ("${PYTHON} tests/rp2040_tty_roots/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_tty_roots.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-tty-roots"):
                    observability.validate_makefile(source)

    def test_callout_roots_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-callout-roots ", " "),
            ("${PYTHON} tests/rp2040_callout_roots/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_callout_roots.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-callout-roots"):
                    observability.validate_makefile(source)

    def test_nlist_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            (" check-rp2040-nlist ", " "),
            ("${PYTHON} tests/rp2040_nlist/check.py", "true"),
            ("${PYTHON} tools/check_rp2040_nlist.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-rp2040-nlist"):
                    observability.validate_makefile(source)

    def test_exec_dispatch_qemu_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            ("${MAKE} check-exec-dispatch-qemu", "true"),
            ("${PYTHON} tests/kernel/exec_dispatch_qemu.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-exec-dispatch-qemu"):
                    observability.validate_makefile(source)

    def test_object_attribution_cross_route_is_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        for original, replacement in (
            ("CROSS_CONTRACT_GATES= check-compiler-object-attribution", "CROSS_CONTRACT_GATES="),
            ("check-cross: check-cross-contracts check-cross-kernel",
             "check-cross: check-cross-kernel"),
            ("check-cross-contracts: ${CROSS_CONTRACT_GATES}", "check-cross-contracts:"),
            ("${PYTHON} tests/compiler_matrix/object_attribution.py", "true"),
        ):
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-compiler-object-attribution"):
                    observability.validate_makefile(source)

    def test_local_logged_entrypoints_are_required(self):
        observability.validate_makefile(MAKEFILE_FIXTURE)
        mutations = (
            ("logged-build: check-python", "unlogged-build: check-python"),
            ("--label rp2040-build-local", "--label wrong-build-local"),
            ("-- ${MAKE} MACHINE=rp2040 build", "-- true"),
            ("logged-distribution: check-python", "unlogged-distribution: check-python"),
            ("-- ${MAKE} MACHINE=rp2040 distribution", "-- true"),
            ("logged-flash: check-python", "unlogged-flash: check-python"),
            ("-- ${MAKE} MACHINE=rp2040 flash", "-- true"),
            ("logged-kernel: check-python", "unlogged-kernel: check-python"),
            ("-- ${MAKE} MACHINE=rp2040 kernel", "-- true"),
            ("logged-ilp32: check-python", "unlogged-ilp32: check-python"),
            ("-- ${MAKE} MACHINE=rp2040 check-ilp32-execution", "-- true"),
            ("logged-check: check-python", "unlogged-check: check-python"),
            ("--label rp2040-check-local", "--label wrong-check-local"),
            ("-- ${MAKE} MACHINE=rp2040", "-- true"),
        )
        for original, replacement in mutations:
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "logged-"):
                    observability.validate_makefile(source)

    def test_local_logged_targets_allocate_owned_evidence(self):
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        for target in (
            "logged-build",
            "logged-distribution",
            "logged-flash",
            "logged-kernel",
            "logged-ilp32",
        ):
            with self.subTest(target=target):
                result = subprocess.run(
                    [
                        "bmake",
                        "-n",
                        f"PYTHON={sys.executable}",
                        "MACHINE=rp2040",
                        target,
                    ],
                    cwd=ROOT,
                    env=environment,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                output = result.stdout + result.stderr
                self.assertEqual(result.returncode, 0, output)
                self.assertIn("--evidence-root .build-logs", output)
                self.assertIn(
                    "--execution-lock .build-logs/rp2040-products.lock",
                    output,
                )
                self.assertNotRegex(
                    output,
                    rf"--log \.build-logs/rp2040-{target.removeprefix('logged-')}-local\.log",
                )

    def test_logged_check_uses_one_bounded_sequence(self):
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        result = subprocess.run(
            [
                "bmake",
                "-n",
                f"PYTHON={sys.executable}",
                "MACHINE=rp2040",
                "logged-check",
            ],
            cwd=ROOT,
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("tools/run_logged_build_check.py", output)
        self.assertIn("--evidence-root .build-logs", output)
        self.assertIn(
            "--execution-lock .build-logs/rp2040-products.lock",
            output,
        )
        self.assertNotIn("tools/run_logged_gate.py --label rp2040-check-local", output)
        self.assertNotIn("tools/run_logged_gate.py --label rp2040-build-local", output)


if __name__ == "__main__":
    unittest.main()
