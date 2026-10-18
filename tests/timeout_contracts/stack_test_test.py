#!/usr/bin/env python3
"""Calibrate timeout static-stack diagnostics."""

from __future__ import annotations

import unittest

import stack_test


class StackDiagnosticTest(unittest.TestCase):
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
