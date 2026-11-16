"""Calibrate Dhara traffic counts, resumed bytes and unrelated sentinels."""

import importlib
import tempfile
import unittest
from pathlib import Path


class DharaTrafficTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path(__file__).with_name("dhara_backend.py").exists():
            raise AssertionError("the Dhara traffic replay bridge is absent")
        cls.module = importlib.import_module("dhara_backend")
        cls.temporary = tempfile.TemporaryDirectory(prefix="dhara-traffic-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.library = cls.module.compile_dhara(Path(cls.temporary.name))

    def backend(self):
        backend = self.module.DharaBackend(self.library, bytes(64 * 1024))
        self.addCleanup(backend.close)
        return backend

    def test_synced_partial_write_preserves_neighbors_and_resumes(self):
        backend = self.backend()
        backend.write(513, b"sentinel")
        expected = bytearray(64 * 1024)
        expected[513:521] = b"sentinel"
        self.assertEqual(backend.read(512, 10), b"\0sentinel\0")
        counters = backend.counts()
        self.assertGreater(counters["physical_program_page_equivalents"], 0)
        self.assertEqual(counters["checkpoint_operations"], 1)
        self.assertEqual(backend.resume(), 0)
        self.assertEqual(backend.verify(bytes(expected)), 0)

    def test_wrong_payload_is_rejected(self):
        backend = self.backend()
        expected = bytearray(64 * 1024)
        expected[1] = 1
        self.assertEqual(backend.verify(bytes(expected)), 1)

    def test_early_acknowledgement_control_loses_unsynced_update(self):
        backend = self.backend()
        backend.write(1, b"X", synchronize=False)
        expected = bytearray(64 * 1024)
        expected[1] = ord("X")
        self.assertEqual(backend.resume(), 0)
        self.assertEqual(backend.verify(bytes(expected)), 1)


if __name__ == "__main__":
    unittest.main()
