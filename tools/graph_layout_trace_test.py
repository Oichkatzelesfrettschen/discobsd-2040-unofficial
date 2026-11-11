"""Independent cache, attribution, held-out and linker candidate controls."""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import graph_layout_trace as gt

FIXTURE = Path(__file__).with_name("graph-layout-example")


class TraceTests(unittest.TestCase):
    def setUp(self):
        self.symbols = gt.read_symbols(FIXTURE / "collision.nm")
        self.training = gt.read_trace(FIXTURE / "training.csv", self.symbols)
        self.heldout = gt.read_trace(FIXTURE / "heldout.csv", self.symbols)

    def test_conflict_and_cold_warm_controls(self):
        addresses = [s["address"] for s in self.symbols]
        for policy in ("lru", "fifo"):
            self.assertEqual(gt.replay(self.training, addresses, policy=policy)["line_misses"], 9)
            packed, _ = gt.place(self.symbols, [0, 1, 2], addresses[0], 8, 0x10020000)
            self.assertEqual(gt.replay(self.training, packed, policy=policy)["line_misses"], 3)
            self.assertEqual(
                gt.replay(self.training, packed, policy=policy, warm=True)["line_misses"], 0
            )

    def test_policy_distinction_and_cross_line_fetch(self):
        # A B A C B: LRU loses B, FIFO retains B in a two-way single set.
        trace = [(i, 0, 2) for i in (0, 1, 0, 2, 1)]
        addresses = [0, 8, 16]
        self.assertEqual(gt.replay(trace, addresses, 16, 8, 2, "lru")["line_misses"], 4)
        self.assertEqual(gt.replay(trace, addresses, 16, 8, 2, "fifo")["line_misses"], 3)
        self.assertEqual(gt.replay([(0, 6, 4)], [0])["line_misses"], 2)

    def test_heldout_cannot_change_selection(self):
        with self.assertRaises(ValueError):
            gt.study(self.symbols, self.training, self.heldout, 10, 7, 3)
        a = gt.study(self.symbols, self.training, self.heldout, 10, 7, 8)
        b = gt.study(self.symbols, self.training, list(reversed(self.heldout)), 10, 7, 8)
        self.assertEqual(a["order"], b["order"])
        self.assertEqual(a["order"], [0, 1, 2])
        for metric in a["metrics"]["heldout"].values():
            self.assertLess(metric["candidate"]["line_misses"], metric["original"]["line_misses"])

    def test_invalid_trace_and_aliases(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.csv"
            for row in ("0x10000001,2", "0x10000006,4", "0x20000000,2"):
                path.write_text("pc,width\n" + row + "\n")
                with self.assertRaises(ValueError):
                    gt.read_trace(path, self.symbols)
            path.write_text("10000000 8 T a\n10000000 8 T alias\n")
            with self.assertRaises(ValueError):
                gt.read_symbols(path)
        with self.assertRaises(ValueError):
            gt.place(self.symbols, [0, 1, 2], 0x10000000, 3, 0x10020000)
        with self.assertRaises(ValueError):
            gt.place(self.symbols, [0, 1, 2], 0x10000000, 8, 0x10000008)

    def test_cli_and_graph_exports(self):
        command = [
            sys.executable,
            str(Path(gt.__file__)),
            "--nm",
            str(FIXTURE / "collision.nm"),
            "--training",
            str(FIXTURE / "training.csv"),
            "--heldout",
            str(FIXTURE / "heldout.csv"),
            "--trials",
            "0",
        ]
        report = json.loads(subprocess.check_output(command, text=True))
        self.assertEqual(report["order"], [0, 1, 2])
        symbols = json.loads(subprocess.check_output(command + ["--emit", "symbols"], text=True))
        self.assertEqual(symbols[0], {"name": "a", "bytes": 8})
        transitions = subprocess.check_output(command + ["--emit", "transitions"], text=True)
        self.assertIn("a,b,3", transitions)

    @unittest.skipUnless(
        shutil.which("cc") and shutil.which("ld") and shutil.which("nm"),
        "host cc/ld/nm unavailable",
    )
    def test_fragment_with_host_linker(self):
        command = [
            sys.executable,
            str(Path(gt.__file__)),
            "--nm",
            str(FIXTURE / "collision.nm"),
            "--training",
            str(FIXTURE / "training.csv"),
            "--heldout",
            str(FIXTURE / "heldout.csv"),
            "--trials",
            "0",
            "--emit",
            "linker",
        ]
        fragment = subprocess.check_output(command, text=True)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "fixture.c").write_text("void c(void) {}\nvoid b(void) {}\nvoid a(void) {}\n")
            (root / "layout.ld").write_text(
                "SECTIONS { .text 0x10000000 : {\n" + fragment + "\n*(.text*) } }"
            )
            subprocess.run(
                [
                    "cc",
                    "-ffunction-sections",
                    "-fno-asynchronous-unwind-tables",
                    "-c",
                    str(root / "fixture.c"),
                    "-o",
                    str(root / "fixture.o"),
                ],
                check=True,
            )
            subprocess.run(
                [
                    "ld",
                    "-T",
                    str(root / "layout.ld"),
                    str(root / "fixture.o"),
                    "-o",
                    str(root / "fixture.elf"),
                ],
                check=True,
            )
            nm = subprocess.check_output(["nm", "-n", str(root / "fixture.elf")], text=True)
            names = [line.split()[-1] for line in nm.splitlines() if " T " in line]
            self.assertEqual(names, ["a", "b", "c"])


if __name__ == "__main__":
    unittest.main()
