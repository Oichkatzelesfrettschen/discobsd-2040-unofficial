"""Check ordinary compensation against fresh persistent-image state."""

import errno
import json
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from storage_trace import Device, Fault
from v7_image import verify
from v7fs_backend import Backend, compile_backend


class CompensationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix="v7-compensation-")
        cls.addClassCleanup(temporary.cleanup)
        cls.library = compile_backend(Path(temporary.name))
        cls.images = {}
        for order in ("little", "pdp"):
            for blocks in (0, 1, 10, 138):
                backend = Backend(cls.library, Device(bytes(512 * 2048)), order)
                if backend.format(64):
                    raise AssertionError("formatter failed")
                result = backend.fill_parent(blocks) if blocks else backend.create("seed")
                if result or backend.sync():
                    raise AssertionError("parent fixture failed")
                backend.close()
                image = backend.device.export()
                verify(image, order)
                cls.images[order, blocks] = image

    def run_operation(self, image, order, operation, fault=None):
        device = Device(image, faults=[fault] if fault else [])
        backend = Backend(self.library, device, order)
        self.assertEqual(backend.mount(), 0)
        result = operation(backend)
        operation_events = list(device.events)
        self.assertEqual(backend.sync(), 0, "compensation state must persist after one fault")
        backend.close()
        device.verify_injections()
        return result, device.export(), operation_events

    def check_reopen_and_continue(self, image, order):
        backend = Backend(self.library, Device(image), order)
        try:
            self.assertEqual(backend.mount(), 0)
            self.assertEqual(backend.create("continued"), 0)
            self.assertEqual(backend.sync(), 0)
            self.assertIn("/continued", verify(backend.device.export(), order).namespace)
        finally:
            backend.close()

    def test_creation_write_failures_restore_persistent_invariants(self):
        for order in ("little", "pdp"):
            for blocks in (1, 10, 138):
                image = self.images[order, blocks]
                old = verify(image, order)
                for directory in (False, True):
                    def operation(backend, directory=directory):
                        return backend.create("new", directory)
                    result, successful, events = self.run_operation(image, order, operation)
                    self.assertEqual(result, 0)
                    self.assertIn("/new", verify(successful, order).namespace)
                    for event in events:
                        if event.kind != "write":
                            continue
                        for mode, amount in (("before_error", 0), ("after_error", event.length)):
                            with self.subTest(order=order, parent_blocks=blocks,
                                              directory=directory,
                                              sequence=event.sequence, mode=mode):
                                fault = Fault(event.sequence, mode, amount)
                                result, persistent, _events = self.run_operation(
                                    image, order, operation, fault)
                                self.assertEqual(result, errno.EIO, "injected error must propagate")
                                self.assertEqual(verify(persistent, order), old)
                                self.check_reopen_and_continue(persistent, order)

    def test_hardlink_write_failures_report_and_compensate(self):
        for order in ("little", "pdp"):
            image = self.images[order, 0]
            old = verify(image, order)
            def operation(backend):
                return backend.link("seed", "alias")
            result, successful, events = self.run_operation(image, order, operation)
            self.assertEqual(result, 0)
            self.assertIn("/alias", verify(successful, order).namespace)
            for event in events:
                if event.kind != "write":
                    continue
                for mode, amount in (("before_error", 0), ("after_error", event.length)):
                    with self.subTest(order=order, sequence=event.sequence, mode=mode):
                        result, persistent, _events = self.run_operation(
                            image, order, operation, Fault(event.sequence, mode, amount))
                        self.assertEqual(result, errno.EIO, "hard-link write error was hidden")
                        self.assertEqual(verify(persistent, order), old)

    def test_second_compensation_failure_reports_inspection_boundary(self):
        dispositions = Counter()
        for order in ("little", "pdp"):
            image = self.images[order, 1]
            for directory in (False, True):
                def operation(backend, directory=directory):
                    return backend.create("new", directory)
                _result, _successful, events = self.run_operation(image, order, operation)
                first = [event for event in events if event.kind == "write"][-1]
                _result, _persistent, cleanup = self.run_operation(
                    image, order, operation, Fault(first.sequence, "after_error", first.length))
                compensation = [event for event in cleanup if event.kind == "write" and
                                event.phase == "compensation"]
                self.assertGreater(len(compensation), 0)
                for event in compensation:
                    device = Device(image, faults=[
                        Fault(first.sequence, "after_error", first.length),
                        Fault(event.sequence, "before_error", 0),
                    ])
                    backend = Backend(self.library, device, order)
                    self.assertEqual(backend.mount(), 0)
                    result = operation(backend)
                    self.assertEqual(result, errno.EIO)
                    self.assertEqual(backend.sync(), 0)
                    backend.close()
                    device.verify_injections()
                    self.assertEqual(device.events[event.sequence - 1].phase, "compensation")
                    try:
                        verify(device.export(), order)
                        dispositions["reader-valid inspection state"] += 1
                    except ValueError:
                        dispositions["reader-rejected inspection state"] += 1
        self.assertGreater(dispositions["reader-rejected inspection state"], 0)
        print(json.dumps({"second_failure_dispositions": dict(dispositions)}, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
