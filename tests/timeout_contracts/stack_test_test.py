#!/usr/bin/env python3
"""Calibrate timeout static-stack diagnostics."""

from __future__ import annotations

import unittest

import stack_test


class StackDiagnosticTest(unittest.TestCase):
    def test_supported_gcc_13_frames_satisfy_component_bounds(self) -> None:
        self.assertEqual(stack_test.validate_production_frames(128, 48), 176)

    def test_pre_fork_ceiling_is_the_component_sum(self) -> None:
        self.assertEqual(
            stack_test.PRE_FORK_CHAIN_LIMIT,
            stack_test.MAIN_FRAME_LIMIT + stack_test.PARSE_DURATION_FRAME_LIMIT,
        )

    def test_documented_limits_reject_the_old_chain_ceiling(self) -> None:
        with self.assertRaisesRegex(
            stack_test.StackFailure,
            "timeout TESTING policy omits the measured stack limits",
        ):
            stack_test.validate_documented_limits(
                "128-byte main frame, 48-byte parse_duration frame, "
                "and 168-byte pre-fork call-chain ceiling"
            )

    def test_bound_failure_reports_components_total_and_limit(self) -> None:
        with self.assertRaisesRegex(
            stack_test.StackFailure,
            "main=128, parse_duration=48, total=176, limit=168",
        ):
            stack_test.require_stack_limit(
                "timeout pre-fork static call chain",
                176,
                168,
                "main=128, parse_duration=48",
            )


if __name__ == "__main__":
    unittest.main()
