"""Calibrate terminal output, child status and bounded completion ownership."""

import os
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNNER = Path(__file__).with_name("ptyrun.py")


class CompletionTests(unittest.TestCase):
    def run_child(self, code, *, timeout="5", script="", runner=RUNNER):
        return subprocess.run(
            [sys.executable, str(runner), "-t", timeout, "--wait-exit", "--",
             sys.executable, "-c", code], input=script, text=True,
            capture_output=True, timeout=10, check=False)

    def test_delayed_output(self):
        result = self.run_child(
            'import os,time; time.sleep(1.2); os.write(1,b"terminal-ready\\n")')
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (0, "terminal-ready\n", ""))

    def test_output_after_script(self):
        result = self.run_child(
            'import os,time; os.write(1,b"ready\\n"); time.sleep(1.2); '
            'os.write(1,b"complete\\n")', script="expect ready\n")
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (0, "ready\ncomplete\n", ""))

    def test_exit_status(self):
        result = self.run_child('import sys; sys.exit(7)')
        self.assertEqual((result.returncode, result.stdout, result.stderr), (7, "", ""))

    def test_signal_status(self):
        result = self.run_child('import os,signal; os.kill(os.getpid(),signal.SIGTERM)')
        self.assertEqual(result.returncode, 128 + signal.SIGTERM)

    def test_terminal_close_precedes_exit(self):
        result = self.run_child(
            'import os,time; os.close(0); os.close(1); os.close(2); '
            'time.sleep(1.2); os._exit(7)')
        self.assertEqual((result.returncode, result.stdout, result.stderr), (7, "", ""))

    def test_timeout_reaps_resistant_child(self):
        result = self.run_child(
            'import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); '
            'print(os.getpid(),flush=True); time.sleep(60)', timeout="3")
        self.assertEqual(result.returncode, 1)
        self.assertIn("timeout waiting for child exit", result.stderr)
        child = int(result.stdout.strip())
        with self.assertRaises(ProcessLookupError):
            os.kill(child, 0)

    def test_output_loss_control(self):
        original = RUNNER.read_text()
        anchor = "    out += data\n    return bool(data)"
        self.assertEqual(original.count(anchor), 1)
        with tempfile.TemporaryDirectory(prefix="pty-output-control-") as temporary:
            mutant = Path(temporary) / "ptyrun.py"
            mutant.write_text(original.replace(anchor, "    out += b''\n    return bool(data)"))
            result = self.run_child('print("terminal-ready",flush=True)', runner=mutant)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        self.assertNotEqual(result.stdout, "terminal-ready\n")


if __name__ == "__main__":
    unittest.main()
