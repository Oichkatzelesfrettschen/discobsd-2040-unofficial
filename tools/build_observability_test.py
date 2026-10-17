"""Calibrate the firmware workflow's bounded, retained build logging."""

import re
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

    def test_repository_workflow_satisfies_the_contract(self):
        observability.check(ROOT)

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


if __name__ == "__main__":
    unittest.main()
