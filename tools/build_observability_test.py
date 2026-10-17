"""Calibrate the firmware workflow's bounded, retained build logging."""

import re
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
HOST_GATES= check-run-logging

check-run-logging: check-python
\t${PYTHON} tools/run_logged_gate_test.py
\t${PYTHON} tools/build_observability_test.py
\t${PYTHON} tools/storage_counter_driver_test.py

logged-build: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-build-local \\
\t    --log .build-logs/rp2040-build-local.log \\
\t    --status .build-logs/rp2040-build-local.status.json \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 build

logged-distribution: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-distribution-local \\
\t    --log .build-logs/rp2040-distribution-local.log \\
\t    --status .build-logs/rp2040-distribution-local.status.json \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 distribution

logged-flash: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-flash-local \\
\t    --log .build-logs/rp2040-flash-local.log \\
\t    --status .build-logs/rp2040-flash-local.status.json \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 flash

logged-kernel: check-python
\t${PYTHON} tools/run_logged_gate.py --label rp2040-kernel-local \\
\t    --log .build-logs/rp2040-kernel-local.log \\
\t    --status .build-logs/rp2040-kernel-local.status.json \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 kernel

logged-check: check-python logged-build
\t${PYTHON} tools/run_logged_gate.py --label rp2040-check-local \\
\t    --log .build-logs/rp2040-check-local.log \\
\t    --status .build-logs/rp2040-check-local.status.json \\
\t    --idle-timeout-seconds 600 --timeout-seconds 3600 \\
\t    -- ${MAKE} MACHINE=rp2040 check
"""


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
            ("${PYTHON} tools/build_observability_test.py", "true"),
            ("${PYTHON} tools/storage_counter_driver_test.py", "true"),
        )
        for original, replacement in mutations:
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "check-run-logging"):
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
            ("logged-check: check-python logged-build", "logged-check: check-python"),
            ("--label rp2040-check-local", "--label wrong-check-local"),
            ("-- ${MAKE} MACHINE=rp2040 check", "-- true"),
        )
        for original, replacement in mutations:
            with self.subTest(original=original):
                source = MAKEFILE_FIXTURE.replace(original, replacement, 1)
                with self.assertRaisesRegex(ValueError, "logged-"):
                    observability.validate_makefile(source)


if __name__ == "__main__":
    unittest.main()
