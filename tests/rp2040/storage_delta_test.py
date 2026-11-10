"""Calibrate storage snapshot parsing and rejection of ambiguous deltas."""

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import summarize_rp2040_storage_delta as storage  # noqa: E402


def snapshot(values=None, *, saturated=0, dirty=0, peak=0, version=1):
    values = values or {}
    lines = [f"machdep.storage_stats=version {version} saturated {saturated} "
             f"dirty {dirty} peak {peak}"]
    lines.extend(f"{name} {values.get(name, 0)}" for name in storage.METRICS)
    return "\n".join(lines) + "\n"


class StorageDeltaTest(unittest.TestCase):
    def test_cli_reports_delta_and_rejects_saturation(self):
        with tempfile.TemporaryDirectory() as directory:
            before = Path(directory) / "before"
            after = Path(directory) / "after"
            before.write_text(snapshot(), encoding="utf-8")
            after.write_text(snapshot({"root_writes": 1}), encoding="utf-8")
            command = [sys.executable, storage.__file__, str(before), str(after),
                       "--before-boot", "boot-1", "--after-boot", "boot-1",
                       "--known-work", "root_writes"]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["counter_deltas"]["root_writes"], 1)
            after.write_text(snapshot({"root_writes": 1}, saturated=1), encoding="utf-8")
            failed = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(failed.returncode, 2)

    def test_schema_matches_production_abi_and_formatter(self):
        root = Path(__file__).resolve().parents[2]
        header = (root / "sys/sys/storage_stats.h").read_text(encoding="utf-8")
        formatter = (root / "sbin/sysctl/sysctl.c").read_text(encoding="utf-8")
        enum = header.split("enum storage_metric {", 1)[1].split("STORAGE_METRIC_COUNT", 1)[0]
        names = re.findall(r"\bSTORAGE_([A-Z_]+)\s*,", enum)
        self.assertEqual(tuple(name.lower() for name in names), storage.METRICS)
        table = formatter.split("static const char *const names[STORAGE_METRIC_COUNT]", 1)[1]
        table = table.split("};", 1)[0]
        self.assertEqual(tuple(re.findall(r'"([a-z_]+)"', table)), storage.METRICS)

    def test_valid_snapshot_and_known_work(self):
        before = snapshot({"root_writes": 2}, dirty=1, peak=2)
        after = snapshot({"root_writes": 5, "root_write_bytes": 1024}, dirty=0, peak=3)
        result = storage.summarize(before, after, "boot-1", "boot-1", ["root_writes"])
        self.assertEqual(result["counter_deltas"]["root_writes"], 3)
        self.assertEqual(result["counter_deltas"]["root_write_bytes"], 1024)
        self.assertEqual(result["dirty_current"], [1, 0])
        self.assertEqual(result["dirty_peak"], [2, 3])

    def test_invalid_snapshots_are_rejected(self):
        before = snapshot({"root_writes": 2})
        for after in (snapshot({"root_writes": 1}), snapshot(saturated=1),
                      snapshot(version=2), snapshot(dirty=2, peak=1),
                      snapshot() + "root_reads 0\n", snapshot().replace("root_reads 0\n", ""),
                      snapshot().replace("root_reads 0\n", "root_reads -1\n"),
                      snapshot().replace("root_reads 0\n", "root_reads 4294967296\n")):
            with self.subTest(after=after), self.assertRaises(ValueError):
                storage.summarize(before, after, "boot-1", "boot-1", [])
        with self.assertRaises(ValueError):
            storage.summarize(snapshot(), snapshot(), "boot-1", "boot-2", [])
        with self.assertRaises(ValueError):
            storage.summarize(snapshot(), snapshot(), "boot-1", "boot-1", ["root_writes"])
        with self.assertRaises(ValueError):
            storage.summarize(snapshot(), snapshot(), "boot-1", "boot-1", ["typo"])


if __name__ == "__main__":
    unittest.main()
