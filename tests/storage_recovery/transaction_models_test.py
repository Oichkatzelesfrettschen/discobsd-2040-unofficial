"""Compare bounded policies over transactions produced by the real V7 core."""

import importlib
import json
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from storage_trace import Device, Fault, Interruption
from v7_image import verify
from v7fs_backend import Backend, compile_backend

KINDS = ("undo", "redo", "copy-on-write")


class RecoveryModelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path(__file__).with_name("transaction_models.py").exists():
            raise AssertionError("the bounded transaction policies are absent")
        cls.models = importlib.import_module("transaction_models")
        cls.coverage = Counter()
        temporary = tempfile.TemporaryDirectory(prefix="transaction-workload-")
        cls.addClassCleanup(temporary.cleanup)
        backend = Backend(compile_backend(Path(temporary.name)), Device(bytes(512 * 128)))
        cls.addClassCleanup(backend.close)
        if backend.format(32) or backend.create("seed") or backend.sync():
            raise AssertionError("initial production image failed")
        cls.before = backend.device.export()
        if backend.create("first", True) or backend.sync():
            raise AssertionError("first production transaction failed")
        cls.first = backend.device.export()
        if backend.create("second") or backend.sync():
            raise AssertionError("second production transaction failed")
        cls.second = backend.device.export()
        backend.close()
        for image in (cls.before, cls.first, cls.second):
            verify(image)

    @classmethod
    def tearDownClass(cls):
        print(json.dumps({"byte_prefix_cuts": dict(sorted(cls.coverage.items()))}, sort_keys=True))

    def recover(self, kind, persistent):
        model = self.models.open_model(kind, Device(persistent))
        recovered = model.recover()
        verify(recovered)
        self.assertEqual(model.recover(), recovered, "recovery must be idempotent")
        return recovered

    def enumerate_transaction(self, kind, initial, before, after, complete_prefixes):
        positive = self.models.open_model(kind, Device(initial))
        positive.transaction(after)
        self.assertEqual(self.recover(kind, positive.device.export()), after)
        count = 0
        for event in positive.device.events:
            if event.kind == "read":
                continue
            amounts = (range(event.length + 1) if complete_prefixes else
                       sorted({0, 1, event.length // 2, event.length - 1, event.length}))
            for amount in amounts:
                with self.subTest(kind=kind, sequence=event.sequence, transferred=amount):
                    device = Device(initial, faults=[Fault(event.sequence, "interrupt", amount)])
                    with self.assertRaises(Interruption):
                        self.models.open_model(kind, device).transaction(after)
                    device.verify_injections()
                    self.assertIn(self.recover(kind, device.export()), (before, after))
                    phase = "first" if before == self.before else "second"
                    self.coverage[kind + ":" + phase] += 1
                    count += 1
        self.assertGreater(count, 0)

    def test_every_write_prefix_preserves_whole_first_transaction(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            self.enumerate_transaction(kind, model.device.export(), self.before, self.first, True)

    def test_second_transaction_preserves_acknowledged_first_transaction(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            model.transaction(self.first)
            self.enumerate_transaction(kind, model.device.export(), self.first, self.second, True)

    def test_interruption_during_recovery_can_recover_again(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            initial = model.device.export()
            model.transaction(self.first)
            writes = [event for event in model.device.events if event.kind == "write"]
            fault = Fault(writes[-1].sequence, "interrupt", 3)
            interrupted = Device(initial, faults=[fault])
            with self.assertRaises(Interruption):
                self.models.open_model(kind, interrupted).transaction(self.first)
            interrupted.verify_injections()
            persistent = interrupted.export()
            recovery = self.models.open_model(kind, Device(persistent))
            expected = recovery.recover()
            writes = [event for event in recovery.device.events if event.kind != "read"]
            if kind == "copy-on-write":
                self.assertEqual(writes, [])
            for event in writes:
                for amount in range(event.length + 1):
                    device = Device(persistent,
                                    faults=[Fault(event.sequence, "interrupt", amount)])
                    with self.assertRaises(Interruption):
                        self.models.open_model(kind, device).recover()
                    device.verify_injections()
                    self.assertEqual(self.recover(kind, device.export()), expected)
                    self.coverage[kind + ":recovery"] += 1

    def test_stale_generation_record_is_rejected(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            model.transaction(self.first)
            persistent = bytearray(model.device.export())
            start = model.journal_offset
            size = model.slot_size
            persistent[start + size:start + 2 * size] = persistent[start:start + size]
            with self.assertRaisesRegex(self.models.RecoveryUnavailable, "generation"):
                self.models.open_model(kind, Device(persistent)).recover()

    def test_committed_corrupt_record_reports_unavailable_recovery(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            model.transaction(self.first)
            persistent = bytearray(model.device.export())
            persistent[model.journal_offset + 40] ^= 1
            with self.assertRaises(self.models.RecoveryUnavailable):
                self.models.open_model(kind, Device(persistent)).recover()

    def test_capacity_exhaustion_preserves_acknowledged_bytes(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before, capacity=2)
            model.transaction(self.first)
            model.transaction(self.second)
            persistent = model.device.export()
            with self.assertRaises(self.models.CapacityExhausted):
                model.transaction(self.second)
            self.assertEqual(model.device.export(), persistent)
            self.assertEqual(self.recover(kind, persistent), self.second)

    def test_new_transaction_after_interrupted_recovery(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before)
            initial = model.device.export()
            model.transaction(self.first)
            event = [event for event in model.device.events if event.kind == "write"][-1]
            device = Device(initial, faults=[Fault(event.sequence, "interrupt", 3)])
            with self.assertRaises(Interruption):
                self.models.open_model(kind, device).transaction(self.first)
            device.verify_injections()
            recovered = self.models.open_model(kind, Device(device.export()))
            state = recovered.recover()
            wanted = self.second if state == self.first else self.first
            recovered.transaction(wanted)
            self.assertEqual(self.recover(kind, recovered.device.export()), wanted)

    def test_wrong_policy_identity_is_rejected(self):
        model = self.models.new_model("undo", self.before)
        with self.assertRaisesRegex(self.models.RecoveryUnavailable, "identity"):
            self.models.open_model("redo", Device(model.device.export()))

    def test_early_publication_mutant_loses_recovery(self):
        for kind in KINDS:
            model = self.models.new_model(kind, self.before, early_publication=True)
            initial = model.device.export()
            model.transaction(self.first)
            publication = next(event for event in model.device.events if event.kind == "publish")
            fault = Fault(publication.sequence, "interrupt", publication.length)
            device = Device(initial, faults=[fault])
            with self.assertRaises(Interruption):
                self.models.open_model(kind, device, early_publication=True).transaction(self.first)
            device.verify_injections()
            with self.assertRaises(self.models.RecoveryUnavailable):
                self.models.open_model(kind, Device(device.export())).recover()


if __name__ == "__main__":
    unittest.main()
