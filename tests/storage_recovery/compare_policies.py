"""Compare one V7 transaction through identical synchronous Dhara replay."""

import hashlib
import json
import os
import platform
import shlex
import subprocess
import sys
import tempfile
import tracemalloc
from pathlib import Path

from dhara_backend import DharaBackend, compile_dhara
from storage_trace import Device
from transaction_models import new_model, open_model
from v7_image import verify
from v7fs_backend import Backend, compile_backend

ROOT = Path(__file__).resolve().parents[2]


class Capture(Device):
    def __init__(self, image):
        super().__init__(image)
        self.payloads = []

    def transfer(self, kind, address, length, payload=None):
        result = super().transfer(kind, address, length, payload)
        self.payloads.append((self.events[-1], bytes(payload) if payload is not None else result))
        return result


def replay_events(backend, capture):
    for event, payload in capture.payloads:
        if event.kind == "read":
            if backend.read(event.offset, event.length) != payload:
                raise RuntimeError("Dhara replay read differs from the logical trace")
        else:
            backend.write(event.offset, payload)


def replay(library, initial, capture, recovery=None):
    backend = DharaBackend(library, initial)
    try:
        replay_events(backend, capture)
        counters = backend.counts()
        if backend.resume():
            raise RuntimeError("Dhara resume failed")
        recovery_counts = None
        if recovery is not None:
            replay_events(backend, recovery)
            after_recovery = backend.counts()
            recovery_counts = {key: value - counters[key] for key, value in after_recovery.items()}
        if backend.verify((recovery if recovery is not None else capture).export()):
            raise RuntimeError("resumed Dhara payload or unrelated sentinel differs")
        return counters, recovery_counts
    finally:
        backend.close()


def compare():
    results = []
    with tempfile.TemporaryDirectory(prefix="storage-policy-comparison-") as directory:
        work = Path(directory)
        v7fs = compile_backend(work)
        dhara = compile_dhara(work)
        backend = Backend(v7fs, Device(bytes(512 * 128)))
        if backend.format(32) or backend.create("seed") or backend.sync():
            raise RuntimeError("baseline workload construction failed")
        before = backend.device.export()
        backend.close()

        capture = Capture(before)
        backend = Backend(v7fs, capture)
        if backend.mount():
            raise RuntimeError("baseline reopen failed")
        capture.events.clear()
        capture.payloads.clear()
        if backend.create("first", True) or backend.sync():
            raise RuntimeError("production creation failed")
        after = capture.export()
        backend.close()
        verify(before)
        verify(after)
        logical_changed = sum(old != new for old, new in zip(before, after))
        counters, _recovery = replay(dhara, before, capture)
        results.append({"mechanism": "compensation", "dhara": counters,
                        "logical_write_calls": sum(event.kind == "write"
                                                    for event in capture.events),
                        "logical_written_bytes": sum(event.transferred for event in capture.events
                                                      if event.kind == "write"),
                        "reserved_metadata_bytes": 0,
                        "peak_python_bytes": None,
                        "loaded_firmware_code_bytes": None})

        for kind in ("undo", "redo", "copy-on-write"):
            tracemalloc.start()
            model = new_model(kind, before)
            initial = model.device.export()
            capture = Capture(initial)
            model.device = capture
            model.transaction(after)
            _current, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            costs = model.costs()
            recovery_capture = Capture(capture.export())
            recovery_model = open_model(kind, recovery_capture)
            recovered = recovery_model.recover()
            if recovered != after:
                raise RuntimeError("policy recovery differs from production transaction")
            verify(recovered)
            counters, recovery_counts = replay(dhara, initial, capture, recovery_capture)
            results.append({"mechanism": kind, "dhara": counters,
                            "peak_python_bytes": peak, **costs,
                            "committed_recovery": {"logical": recovery_model.costs(),
                                                   "dhara": recovery_counts}})
        for result in results:
            programs = result["dhara"]["physical_program_page_equivalents"]
            erases = result["dhara"]["physical_erase_sector_equivalents"]
            result["program_amplification"] = {"programmed_bytes": programs * 256,
                                                "logical_changed_bytes": logical_changed}
            result["erase_amplification"] = {"erased_bytes": erases * 4096,
                                              "logical_changed_bytes": logical_changed}
        sources = [
            "sys/arch/rp2040/dev/flash.h", "sys/arch/rp2040/dhara/map.c",
            "sys/arch/rp2040/dhara/journal.c", "sys/arch/rp2040/dhara/error.c",
            "tests/rp2040/dhara_amplification/dhara_amplification_test.c",
            "sbin/newfs_v7fs/main.c", "usr.sbin/v7fs/v7fs_io_fd.c",
            "usr.sbin/v7fs/v7fs_io_fd.h", "usr.sbin/v7fs/netbsd_compat.h",
            "sbin/newfs_v7fs/newfs_v7fs.h",
        ]
        sources += ["sys/fs/v7fs/v7fs_" + name + ".c" for name in (
            "endian", "superblock", "superblock_util", "inode", "inode_util",
            "datablock", "dirent", "io", "file", "file_util",
        )]
        sources += [path.relative_to(ROOT).as_posix() for directory in (
            ROOT / "sys/arch/rp2040/dhara", ROOT / "sys/fs/v7fs",
            ROOT / "usr.sbin/v7fs/compat",
        ) for path in sorted(directory.rglob("*.h"))]
        sources += ["tests/storage_recovery/" + name for name in (
            "storage_trace.py", "transaction_models.py", "v7_image.py", "v7fs_backend.py",
            "dhara_backend.py", "compare_policies.py", "dhara_bridge.c", "v7fs_bridge.c",
            "v7fs_io_hooks.h", "host_build.py",
        )]
        return {
            "schema_version": 1,
            "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                     cwd=ROOT, text=True).strip(),
            "source_state": "source_inputs_sha256 binds the compiled sources, declared headers "
                            "and Python implementation in the exercised working tree",
            "source_inputs_sha256": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                                      for path in sorted(sources)},
            "host": {"system": platform.system(), "machine": platform.machine(),
                     "python": platform.python_version(),
                     "compiler": subprocess.check_output(
                         [*shlex.split(os.environ.get("HOSTCC", "cc")), "--version"],
                         text=True).splitlines()[0]},
            "configuration": {"v7fs": ["STATIC_BUFFER", "NDEBUG", "V7FS_EI", "UBSan"],
                              "model_slots": 2, "model_records_per_slot": 8,
                              "layout_alignment_bytes": 1024,
                              "dhara_gc_ratio": 4},
            "peak_python_measurement": "tracemalloc peak of new Python allocations during "
                                       "model construction and transaction; input image "
                                       "allocations and C allocations excluded",
            "workload": "Create one empty subdirectory beside one empty file "
                        "in a 128-block V7 image",
            "before_sha256": hashlib.sha256(before).hexdigest(),
            "after_sha256": hashlib.sha256(after).hexdigest(),
            "logical_changed_bytes": logical_changed,
            "adapter": "Full 989-sector map; each touched 1024-byte unit is read, modified, "
                       "written and checkpointed synchronously; final inspection reads excluded",
            "interruption_contract": "Models admit ordered persistent byte prefixes and "
                                     "an absent-or-complete publication byte. The Dhara traffic "
                                     "replay admits complete NAND operations only.",
            "hardware_coefficients": {"program_latency": None, "erase_latency": None,
                                      "read_latency": None, "cpu_latency": None},
            "production_admission": "blocked on persistent representation, publication, "
                                    "recycling, migration and production-adapter "
                                    "interruption evidence",
            "results": results,
        }


if __name__ == "__main__":
    json.dump(compare(), sys.stdout, indent=2, sort_keys=True)
    print()
