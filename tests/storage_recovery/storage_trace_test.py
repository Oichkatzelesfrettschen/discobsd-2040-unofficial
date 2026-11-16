"""Calibrate typed byte ranges and persistent transfer fault effects."""

import importlib
import json
import unittest
from pathlib import Path


class TraceTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(Path(__file__).with_name("storage_trace.py").exists(),
                        "the shared storage transfer interface is absent")
        self.trace = importlib.import_module("storage_trace")

    def test_checked_range_uses_remaining_capacity(self):
        for offset, length in ((0, 0), (0, 1024), (1024, 0), (1023, 1)):
            self.trace.checked_range(offset, length, 1024)
        for offset, length in ((-1, 1), (1025, 0), (1024, 1), (1, 1024)):
            with self.assertRaises(ValueError):
                self.trace.checked_range(offset, length, 1024)

    def test_domains_require_explicit_conversion(self):
        trace = self.trace
        address = trace.Address(trace.Domain.V7_BLOCK, 3)
        self.assertEqual(trace.byte_address(address, 2048).offset, 1536)
        self.assertEqual(trace.byte_address(trace.Address(trace.Domain.DHARA_PAGE, 3),
                                            2 * 1024 * 1024, origin=128 * 1024),
                         trace.Address(trace.Domain.FLASH_BYTE, 128 * 1024 + 3072))
        unit, displacement = trace.split_address(
            trace.Address(trace.Domain.IMAGE_BYTE, 1537), trace.Domain.V7_BLOCK)
        self.assertEqual((unit, displacement), (address, 1))
        with self.assertRaises(ValueError):
            trace.byte_address(trace.Address(trace.Domain.V7_BLOCK, 5), 2048)
        with self.assertRaises(ValueError):
            trace.Device(bytes(2048)).read(address, 1)

    def test_codecs_at_every_block_alignment_residue(self):
        trace = self.trace
        for width in (1, 2, 3, 4):
            for signed in (False, True):
                lower = -(1 << (width * 8 - 1)) if signed else 0
                upper = (1 << (width * 8 - int(signed))) - 1
                for order in ("little", "pdp"):
                    for value in (lower, min(lower + 1, upper), 0, upper):
                        encoded = trace.encode(value, width, order, signed)
                        for residue in range(512):
                            container = bytes(residue) + encoded + bytes(4)
                            self.assertEqual(trace.decode(container, residue, width, order, signed),
                                             value)
                    for value in (lower - 1, upper + 1):
                        with self.assertRaises(ValueError):
                            trace.encode(value, width, order, signed)
        self.assertEqual(trace.encode(0x123456, 3, "pdp"), bytes.fromhex("125634"))
        self.assertEqual(trace.encode(0x12345678, 4, "pdp"), bytes.fromhex("34127856"))

    def test_short_transfer_changes_only_its_prefix(self):
        trace = self.trace
        fault = trace.Fault(1, "short", 2)
        device = trace.Device(b"abcdefgh", faults=[fault])
        address = trace.Address(trace.Domain.IMAGE_BYTE, 1)
        self.assertEqual(device.write(address, b"WXYZ"), 2)
        self.assertEqual(device.export(), b"aWXdefgh")
        device.verify_injections()
        event = device.events[0]
        self.assertEqual((event.sequence, event.transferred, event.offset, event.length),
                         (1, 2, 1, 4))

    def test_failure_before_and_after_transfer(self):
        trace = self.trace
        for mode, count, expected in (("before_error", 0, b"abcdefgh"),
                                      ("after_error", 3, b"XYZdefgh")):
            device = trace.Device(b"abcdefgh", faults=[trace.Fault(1, mode, count)])
            with self.assertRaises(trace.TransferError):
                device.write(trace.Address(trace.Domain.IMAGE_BYTE, 0), b"XYZW")
            self.assertEqual(device.export(), expected)
            device.verify_injections()

    def test_interruption_freezes_persistent_bytes(self):
        trace = self.trace
        device = trace.Device(b"abcdefgh", faults=[trace.Fault(1, "interrupt", 3)])
        address = trace.Address(trace.Domain.IMAGE_BYTE, 0)
        with self.assertRaises(trace.Interruption):
            device.write(address, b"XYZW")
        self.assertEqual(device.export(), b"XYZdefgh")
        with self.assertRaises(trace.Interruption):
            device.write(address, b"zzzz")
        self.assertEqual(device.export(), b"XYZdefgh")

    def test_second_failure_during_compensation_is_visible(self):
        trace = self.trace
        device = trace.Device(bytes(16), faults=[trace.Fault(1, "after_error", 4),
                                                trace.Fault(2, "before_error", 0)])
        address = trace.Address(trace.Domain.IMAGE_BYTE, 0)
        with self.assertRaises(trace.TransferError):
            device.write(address, b"data")
        device.phase = "compensation"
        with self.assertRaises(trace.TransferError):
            device.write(address, bytes(4))
        device.verify_injections()
        self.assertEqual([event.phase for event in device.events], ["operation", "compensation"])

    def test_inactive_injection_fails_calibration(self):
        trace = self.trace
        device = trace.Device(bytes(16), faults=[trace.Fault(5, "before_error", 0)])
        device.read(trace.Address(trace.Domain.IMAGE_BYTE, 0), 1)
        with self.assertRaisesRegex(ValueError, "inactive injection"):
            device.verify_injections()

    def test_event_export_uses_the_closed_schema(self):
        trace = self.trace
        device = trace.Device(bytes(16))
        device.read(trace.Address(trace.Domain.IMAGE_BYTE, 2), 4)
        records = [json.loads(line) for line in device.json_lines().splitlines()]
        self.assertEqual(set(records[0]), {"sequence", "layer", "kind", "domain", "offset",
                                           "length", "phase", "transferred", "result"})
        self.assertEqual(records[0]["domain"], "image-byte")


if __name__ == "__main__":
    unittest.main()
