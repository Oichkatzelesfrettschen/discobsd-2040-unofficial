"""Replay logical traffic with synchronous per-unit Dhara persistence."""

import ctypes
import os
import shlex
import subprocess
import sys
from pathlib import Path

from host_build import sanitizer_link_flags

ROOT = Path(__file__).resolve().parents[2]


def compile_dhara(directory):
    compiler = shlex.split(os.environ.get("HOSTCC", "cc"))
    shared = "-dynamiclib" if sys.platform == "darwin" else "-shared"
    dhara = ROOT / "sys/arch/rp2040/dhara"
    output = directory / "dhara-traffic.so"
    subprocess.run(
        [*compiler, "-std=c17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-Wstrict-prototypes", "-Wold-style-definition", "-fPIC", shared,
         *sanitizer_link_flags(compiler), "-fno-sanitize-recover=undefined",
         "-I" + str(ROOT / "sys/arch"), "-I" + str(dhara),
         str(Path(__file__).with_name("dhara_bridge.c")), str(dhara / "error.c"),
         str(dhara / "journal.c"), str(dhara / "map.c"), "-o", str(output)],
        check=True,
    )
    return output


class DharaBackend:
    def __init__(self, library, initial):
        self.library = ctypes.CDLL(str(library))
        self.library.storage_dhara_setup.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.library.storage_dhara_transfer.argtypes = [ctypes.c_size_t, ctypes.c_void_p,
                                                       ctypes.c_size_t, ctypes.c_int, ctypes.c_int]
        self.library.storage_dhara_verify.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.library.storage_dhara_counts.argtypes = [ctypes.c_void_p]
        self.library.storage_dhara_counts.restype = None
        self.library.storage_dhara_close.restype = None
        data = ctypes.create_string_buffer(initial, len(initial))
        if self.library.storage_dhara_setup(data, len(initial)):
            raise RuntimeError("Dhara full-map initialization failed")

    def write(self, offset, data, synchronize=True):
        buffer = ctypes.create_string_buffer(data, len(data))
        if self.library.storage_dhara_transfer(offset, buffer, len(data), 1, int(synchronize)):
            raise RuntimeError("Dhara traffic write failed")

    def read(self, offset, length):
        buffer = ctypes.create_string_buffer(length)
        if self.library.storage_dhara_transfer(offset, buffer, length, 0, 0):
            raise RuntimeError("Dhara traffic read failed")
        return buffer.raw

    def resume(self):
        return self.library.storage_dhara_resume()

    def verify(self, expected):
        buffer = ctypes.create_string_buffer(expected, len(expected))
        return self.library.storage_dhara_verify(buffer, len(expected))

    def counts(self):
        buffer = (ctypes.c_ulonglong * 6)()
        self.library.storage_dhara_counts(buffer)
        return dict(zip(("physical_program_page_equivalents", "physical_erase_sector_equivalents",
                         "checkpoint_operations", "nand_read_calls", "nand_read_bytes",
                         "nand_copied_bytes"), buffer))

    def close(self):
        self.library.storage_dhara_close()
