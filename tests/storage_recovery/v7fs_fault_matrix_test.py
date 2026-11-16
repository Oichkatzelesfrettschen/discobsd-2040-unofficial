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

    def test_rename_write_prefix_failures_restore_persistent_invariants(self):
        interruptions = Counter()
        for order in ("little", "pdp"):
            with self.subTest(order=order):
                backend = Backend(self.library, Device(bytes(512 * 2048)), order)
                self.assertEqual(backend.format(64), 0)
                source = backend.create_at(2, "source", True)
                target = backend.create_at(2, "target", True)
                self.assertGreater(source, 2)
                self.assertGreater(target, source)
                self.assertEqual(backend.sync(), 0)
                initial = backend.device.export()
                backend.close()

                def operation(backend, target=target):
                    return backend.rename(2, "source", target, "moved")

                result, successful, events = self.run_operation(initial, order, operation)
                self.assertEqual(result, 0)
                new = verify(successful, order)
                self.assertEqual(new.namespace["/target/moved"], source)
                old = verify(initial, order)
                writes = [event for event in events if event.kind == "write"]
                self.assertGreater(len(writes), 0)
                for event in writes:
                    for amount in range(event.length + 1):
                        with self.subTest(sequence=event.sequence, transferred=amount):
                            result, persistent, _events = self.run_operation(
                                initial, order, operation,
                                Fault(event.sequence, "after_error", amount))
                            self.assertEqual(result, errno.EIO)
                            report = verify(persistent, order)
                            self.assertEqual(report.namespace, old.namespace)
                            self.assertEqual(report.link_counts, old.link_counts)
                            self.assertEqual(report.sizes, old.sizes)
                            self.assertEqual(report.inode_metadata, old.inode_metadata)
                            self.assertEqual(report.free_blocks, old.free_blocks)
                            self.assertEqual(report.free_inodes, old.free_inodes)
                            self.check_reopen_and_continue(persistent, order)

                            device = Device(initial, faults=[
                                Fault(event.sequence, "interrupt", amount)])
                            backend = Backend(self.library, device, order)
                            try:
                                self.assertEqual(backend.mount(), 0)
                                self.assertEqual(operation(backend), errno.EIO)
                            finally:
                                backend.close()
                            device.verify_injections()
                            self.assertTrue(device.frozen)
                            self.assertEqual(len(device.events), event.sequence)
                            persistent = device.export()
                            try:
                                report = verify(persistent, order)
                                category = ("old" if report == old else
                                            "new" if report == new else "other-valid")
                            except ValueError:
                                category = "invalid"
                            interruptions[category] += 1
                            for _reopen in range(2):
                                reopened = Backend(self.library, Device(persistent), order)
                                try:
                                    self.assertEqual(reopened.mount(), 0)
                                    self.assertEqual(reopened.device.export(), persistent)
                                finally:
                                    reopened.close()
        self.assertGreater(interruptions["invalid"], 0)
        print(json.dumps({"rename_complete_prefix_interruptions": dict(interruptions)},
                         sort_keys=True))

    def test_replacement_rename_caller_reclaims_displaced_inode(self):
        for order in ("little", "pdp"):
            backend = Backend(self.library, Device(bytes(512 * 2048)), order)
            self.assertEqual(backend.format(64), 0)
            source = backend.create_at(2, "source", True)
            parent = backend.create_at(2, "parent", True)
            displaced = backend.create_at(parent, "target", True)
            self.assertGreater(displaced, parent)
            self.assertEqual(backend.reclaim(source), errno.EINVAL)
            self.assertEqual(backend.sync(), 0)
            initial = backend.device.export()
            backend.close()
            old = verify(initial, order)

            def operation(backend, parent=parent):
                return backend.rename(2, "source", parent, "target")

            result, intermediate, events = self.run_operation(initial, order, operation)
            self.assertEqual(result, 0)
            with self.assertRaisesRegex(ValueError, "unreachable inode"):
                verify(intermediate, order)
            backend = Backend(self.library, Device(intermediate), order)
            try:
                self.assertEqual(backend.mount(), 0)
                self.assertEqual(backend.reclaim(displaced), 0)
                self.assertEqual(backend.reclaim(displaced), errno.EINVAL)
                self.assertEqual(backend.sync(), 0)
                completed = backend.device.export()
            finally:
                backend.close()
            report = verify(completed, order)
            self.assertEqual(report.namespace["/parent/target"], source)
            self.assertNotIn("/source", report.namespace)
            self.assertNotIn(displaced, report.inode_metadata)
            self.assertEqual(report.free_inodes, old.free_inodes + 1)
            self.assertEqual(report.free_blocks, old.free_blocks + 1)
            self.check_reopen_and_continue(completed, order)

            for event in (event for event in events if event.kind == "write"):
                for amount in (0, event.length):
                    with self.subTest(order=order, sequence=event.sequence, amount=amount):
                        result, persistent, _events = self.run_operation(
                            initial, order, operation,
                            Fault(event.sequence, "after_error", amount))
                        self.assertEqual(result, errno.EIO)
                        report = verify(persistent, order)
                        self.assertEqual(report.namespace, old.namespace)
                        self.assertEqual(report.inode_metadata, old.inode_metadata)
                        self.assertEqual(report.free_blocks, old.free_blocks)
                        self.assertEqual(report.free_inodes, old.free_inodes)
                        self.check_reopen_and_continue(persistent, order)

    def test_rename_second_failure_reports_inspection_boundary(self):
        dispositions = Counter()
        for order in ("little", "pdp"):
            backend = Backend(self.library, Device(bytes(512 * 2048)), order)
            self.assertEqual(backend.format(64), 0)
            self.assertGreater(backend.create_at(2, "source", True), 2)
            target = backend.create_at(2, "target", True)
            self.assertGreater(target, 2)
            self.assertEqual(backend.sync(), 0)
            initial = backend.device.export()
            backend.close()

            def operation(backend, target=target):
                return backend.rename(2, "source", target, "moved")

            result, _successful, events = self.run_operation(initial, order, operation)
            self.assertEqual(result, 0)
            for first in (event for event in events if event.kind == "write"):
                _result, _persistent, cleanup = self.run_operation(
                    initial, order, operation,
                    Fault(first.sequence, "after_error", first.length))
                compensation = [event for event in cleanup if event.kind == "write"
                                and event.phase == "compensation"]
                self.assertGreater(len(compensation), 0)
                for second in compensation:
                    with self.subTest(order=order, first=first.sequence, second=second.sequence):
                        device = Device(initial, faults=[
                            Fault(first.sequence, "after_error", first.length),
                            Fault(second.sequence, "before_error", 0),
                        ])
                        backend = Backend(self.library, device, order)
                        try:
                            self.assertEqual(backend.mount(), 0)
                            self.assertEqual(operation(backend), errno.EIO)
                            self.assertEqual(backend.sync(), 0)
                        finally:
                            backend.close()
                        device.verify_injections()
                        self.assertEqual(device.events[second.sequence - 1].phase,
                                         "compensation")
                        try:
                            verify(device.export(), order)
                            dispositions["reader-valid inspection state"] += 1
                        except ValueError:
                            dispositions["reader-rejected inspection state"] += 1
        self.assertGreater(dispositions["reader-rejected inspection state"], 0)
        print(json.dumps({"rename_second_failure_dispositions": dict(dispositions)},
                         sort_keys=True))


if __name__ == "__main__":
    unittest.main()
