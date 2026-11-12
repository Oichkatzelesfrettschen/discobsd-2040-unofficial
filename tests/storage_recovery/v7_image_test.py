"""Calibrate persistent-image invariants with corruptions of production images."""

import importlib
import tempfile
import unittest
from pathlib import Path

from storage_trace import Device, decode, encode
from v7fs_backend import Backend, compile_backend


class ImageOracleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path(__file__).with_name("v7_image.py").exists():
            raise AssertionError("the independent persistent-image oracle is absent")
        cls.oracle = importlib.import_module("v7_image")
        temporary = tempfile.TemporaryDirectory(prefix="v7-image-")
        cls.addClassCleanup(temporary.cleanup)
        library = compile_backend(Path(temporary.name))
        cls.images = {}
        for order in ("little", "pdp"):
            backend = Backend(library, Device(bytes(512 * 256)), order)
            if backend.format(64) or backend.create("file") or backend.create("sub", True):
                raise AssertionError("production fixture construction failed")
            if backend.sync():
                raise AssertionError("production fixture writeback failed")
            backend.close()
            cls.images[order] = backend.device.export()

    def test_freshly_reopened_production_images(self):
        for order, image in self.images.items():
            report = self.oracle.verify(image, order)
            self.assertEqual(set(report.namespace), {"/", "/file", "/sub"})
            self.assertEqual(report.allocated_inodes, 4)

    def mutation(self, modify, reason):
        for order, original in self.images.items():
            image = bytearray(original)
            modify(image, order)
            with self.assertRaisesRegex(ValueError, reason):
                self.oracle.verify(bytes(image), order)

    def test_duplicate_data_ownership(self):
        def modify(image, order):
            file_inode = 2 * 512 + 2 * 64
            root_inode = 2 * 512 + 64
            image[file_inode + 8:file_inode + 12] = encode(512, 4, order)
            image[file_inode + 12:file_inode + 15] = image[root_inode + 12:root_inode + 15]
        self.mutation(modify, "multiply owned")

    def test_leaked_block_with_adjusted_free_count(self):
        def modify(image, order):
            count = decode(image, 512 + 6, 2, order)
            if count < 2:
                raise AssertionError("fixture needs multiple cached free blocks")
            image[512 + 6:512 + 8] = encode(count - 1, 2, order)
            free = decode(image, 512 + 418, 4, order)
            image[512 + 418:512 + 422] = encode(free - 1, 4, order)
        self.mutation(modify, "unowned data block")

    def test_link_count_differs_from_directory_references(self):
        def modify(image, order):
            offset = 2 * 512 + 2 * 64 + 2
            image[offset:offset + 2] = encode(2, 2, order)
        self.mutation(modify, "link count")

    def test_orphaned_inode(self):
        def modify(image, order):
            root_block = decode(image, 2 * 512 + 64 + 12, 3, order)
            image[root_block * 512 + 32:root_block * 512 + 34] = bytes(2)
        self.mutation(modify, "unreachable inode")

    def test_allocated_inode_in_free_inode_cache(self):
        def modify(image, order):
            image[512 + 208:512 + 210] = encode(1, 2, order)
            image[512 + 210:512 + 212] = encode(2, 2, order)
        self.mutation(modify, "free inode cache")

    def test_block_pointer_outside_image(self):
        def modify(image, order):
            offset = 2 * 512 + 64 + 12
            image[offset:offset + 3] = encode(256, 3, order)
        self.mutation(modify, "block bounds")

    def test_directory_entry_reaches_unallocated_inode(self):
        def modify(image, order):
            root_block = decode(image, 2 * 512 + 64 + 12, 3, order)
            image[root_block * 512 + 32:root_block * 512 + 34] = encode(64, 2, order)
        self.mutation(modify, "unallocated inode")


if __name__ == "__main__":
    unittest.main()
