"""Exercise the production fixed-pool descriptor backend through typed faults."""

import importlib
import tempfile
import unittest
from pathlib import Path

from storage_trace import Address, Device, Domain, Fault


class BackendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path(__file__).with_name("v7fs_backend.py").exists():
            raise AssertionError("the production descriptor bridge is absent")
        cls.module = importlib.import_module("v7fs_backend")
        cls.temporary = tempfile.TemporaryDirectory(prefix="v7fs-backend-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.library = cls.module.compile_backend(Path(cls.temporary.name))

    def backend(self, device):
        backend = self.module.Backend(self.library, device)
        self.addCleanup(backend.close)
        return backend

    def test_production_backend_retries_positive_short_read(self):
        image = bytes(range(256)) * 8
        device = Device(image, faults=[Fault(1, "short", 1)])
        backend = self.backend(device)
        result, data = backend.read_sector(1)
        self.assertTrue(result)
        self.assertEqual(data, image[512:1024])
        self.assertEqual([event.length for event in device.events], [512, 511])
        device.verify_injections()

    def test_production_backend_retries_positive_short_write(self):
        device = Device(bytes(2048), faults=[Fault(1, "short", 255)])
        backend = self.backend(device)
        payload = bytes(range(256)) * 2
        self.assertTrue(backend.write_sector(2, payload))
        self.assertEqual(device.export()[1024:1536], payload)
        self.assertEqual([event.offset for event in device.events], [1024, 1279])
        device.verify_injections()

    def test_production_backend_refuses_out_of_bounds_sectors(self):
        device = Device(bytes(2048))
        backend = self.backend(device)
        for sector in (-1, 4):
            self.assertFalse(backend.read_sector(sector)[0])
            self.assertFalse(backend.write_sector(sector, bytes(512)))
        self.assertEqual(device.events, [])

    def test_failure_after_prefix_is_persistent_before_reopen(self):
        device = Device(bytes(2048), faults=[Fault(1, "after_error", 3)])
        backend = self.backend(device)
        self.assertFalse(backend.write_sector(1, b"X" * 512))
        self.assertEqual(device.export()[512:1024], b"XXX" + bytes(509))
        self.assertEqual(device.events[0].transferred, 3)
        device.verify_injections()
        backend.close()
        reopened = self.backend(Device(device.export()))
        self.assertEqual(reopened.read_sector(1), (True, b"XXX" + bytes(509)))

    def test_interruption_excludes_later_compensation_writes(self):
        device = Device(bytes(2048), faults=[Fault(1, "interrupt", 257)])
        backend = self.backend(device)
        self.assertFalse(backend.write_sector(0, b"X" * 512))
        saved = device.export()
        self.assertFalse(backend.write_sector(0, bytes(512)))
        self.assertEqual(device.export(), saved)
        self.assertEqual(saved[:512], b"X" * 257 + bytes(255))
        device.verify_injections()

    def test_formatter_and_core_allocate_through_production_callbacks(self):
        backend = self.backend(Device(bytes(512 * 128)))
        self.assertEqual(backend.format(32), 0)
        self.assertEqual(backend.create("empty"), 0)
        self.assertEqual(backend.sync(), 0)
        backend.close()
        reopened = self.backend(Device(backend.device.export()))
        self.assertEqual(reopened.mount(), 0)
        self.assertGreater(reopened.lookup("empty"), 2)
        self.assertEqual(reopened.device.read(Address(Domain.IMAGE_BYTE, 0), 1), bytes(1))


if __name__ == "__main__":
    unittest.main()
