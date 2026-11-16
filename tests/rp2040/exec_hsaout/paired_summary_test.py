"""Calibrate the paired exec timing summary and provenance validation."""

import copy
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import summarize_packed_exec_pairs as summary  # noqa: E402


def sample():
    packed = (26.14, 26.51, 26.42, 26.45, 26.59, 26.95)
    raw = (25.74, 27.41, 26.99, 26.74, 26.54, 26.15)
    record = {key: "a" * 64 for key in summary.HASH_KEYS}
    record.update({"schema": 1, "boot_id": "fixture-boot", "workload": "fixture-workload",
                   "timer_resolution_seconds": 0.01, "iterations": 20,
                   "pairs": [{"id": str(index), "order": "packed-raw",
                              "packed_success": True, "raw_success": True,
                              "packed_seconds": packed_value, "raw_seconds": raw_value}
                             for index, (packed_value, raw_value) in enumerate(zip(packed, raw))]})
    return record


class PairedSummaryTest(unittest.TestCase):
    def test_known_six_pair_values(self):
        result = summary.summarize(sample())
        self.assertAlmostEqual(result["mean_difference_seconds_per_exec"], -0.00425)
        self.assertAlmostEqual(result["standard_error_seconds_per_exec"], 0.0128392302469)
        interval = result["paired_t95_sampling_interval_seconds_per_exec"]
        self.assertAlmostEqual(interval[0], -0.037254294, places=7)
        self.assertAlmostEqual(interval[1], 0.028754294, places=7)
        self.assertAlmostEqual(result["timer_quantization_bound_seconds_per_exec"], 0.001)

    def test_invalid_capture_is_rejected(self):
        cases = []
        for key, value in (("kernel_sha256", "missing"), ("boot_id", ""), ("schema", True),
                           ("iterations", 0), ("timer_resolution_seconds", 0),
                           ("timer_resolution_seconds", 10**400)):
            bad = sample()
            bad[key] = value
            cases.append(bad)
        bad = sample()
        bad["pairs"] = bad["pairs"][:1]
        cases.append(bad)
        bad = sample()
        bad["pairs"][1]["id"] = bad["pairs"][0]["id"]
        cases.append(bad)
        for key, value in (("raw_success", False), ("order", "unknown"),
                           ("raw_seconds", math.nan), ("packed_seconds", -1)):
            bad = sample()
            bad["pairs"][0][key] = value
            cases.append(bad)
        for record in cases:
            with self.subTest(record=record), self.assertRaises(ValueError):
                summary.summarize(record)

    def test_pairing_and_order_are_retained(self):
        record = sample()
        permuted = copy.deepcopy(record)
        permuted["pairs"][0]["raw_seconds"], permuted["pairs"][1]["raw_seconds"] = (
            permuted["pairs"][1]["raw_seconds"], permuted["pairs"][0]["raw_seconds"]
        )
        self.assertNotEqual(summary.summarize(record)["sample_sd_seconds_per_exec"],
                            summary.summarize(permuted)["sample_sd_seconds_per_exec"])
        record["pairs"][0]["order"] = "raw-packed"
        self.assertEqual(summary.summarize(record)["pairs"], 6)

    def test_large_sample_uses_conservative_quantile(self):
        record = sample()
        record["pairs"] = [dict(record["pairs"][0], id=str(index)) for index in range(40)]
        self.assertGreater(summary.summarize(record)["t_quantile"], summary.T95[-1])

    def test_cli_accepts_valid_and_rejects_incomplete_workload(self):
        with tempfile.TemporaryDirectory(prefix="discobsd-paired-summary-") as temporary:
            capture = Path(temporary) / "capture.json"
            record = sample()
            capture.write_text(json.dumps(record), encoding="utf-8")
            command = [sys.executable, str(ROOT / "tools/summarize_packed_exec_pairs.py"),
                       str(capture)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["pairs"], 6)
            record["pairs"][0]["packed_success"] = False
            capture.write_text(json.dumps(record), encoding="utf-8")
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("both workloads must complete successfully", result.stderr)


if __name__ == "__main__":
    unittest.main()
