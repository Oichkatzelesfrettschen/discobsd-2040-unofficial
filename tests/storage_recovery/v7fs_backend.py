"""Compile and call the production fixed-pool v7fs code under typed I/O faults."""

import ctypes
import errno
import os
import shlex
import subprocess
import sys
from pathlib import Path

from storage_trace import Address, Domain, Interruption, TransferError, checked_range

ROOT = Path(__file__).resolve().parents[2]
TRANSFER = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_int, ctypes.c_void_p,
                           ctypes.c_size_t, ctypes.c_int)
SEEK = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_int, ctypes.c_int64, ctypes.c_int)


def compile_backend(directory):
    compiler = shlex.split(os.environ.get("HOSTCC", "cc"))
    flags = ["-std=gnu17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-fPIC", "-fsanitize=undefined",
             "-fno-sanitize-recover=undefined", "-DV7FS_EI", "-DSTATIC_BUFFER", "-DNDEBUG",
             "-D_DEFAULT_SOURCE", "-include", str(ROOT / "usr.sbin/v7fs/netbsd_compat.h"),
             "-I" + str(ROOT / "usr.sbin/v7fs/compat"),
             "-I" + str(ROOT / "sys/fs/v7fs"), "-I" + str(ROOT / "sbin/newfs_v7fs")]
    sources = [ROOT / "sys/fs/v7fs" / f"v7fs_{name}.c" for name in (
        "endian", "superblock", "superblock_util", "inode", "inode_util",
        "datablock", "dirent", "io", "file", "file_util",
    )]
    sources += [ROOT / "sbin/newfs_v7fs/main.c", Path(__file__).with_name("v7fs_bridge.c"),
                ROOT / "usr.sbin/v7fs/v7fs_io_fd.c"]
    objects = []
    for number, source in enumerate(sources):
        output = directory / f"object-{number}.o"
        renames = (["-Dread=storage_test_read", "-Dwrite=storage_test_write",
                    "-Dlseek=storage_test_seek"] if source.name == "v7fs_io_fd.c" else [])
        subprocess.run([*compiler, *flags, *renames, "-c", str(source), "-o", str(output)],
                       check=True)
        objects.append(str(output))
    library = directory / "v7fs-backend.so"
    shared = "-dynamiclib" if sys.platform == "darwin" else "-shared"
    subprocess.run([*compiler, shared, "-fsanitize=undefined", *objects, "-o", str(library)],
                   check=True)
    return library


class Backend:
    def __init__(self, library, device, order="little"):
        if order not in ("little", "pdp") or len(device.export()) % 512:
            raise ValueError("backend requires a whole-sector image and supported byte order")
        self.library = ctypes.CDLL(str(library))
        self.device = device
        self.size_bytes = len(device.export())
        self.position = 0
        self.callback_errors = []
        self._transfer_callback = TRANSFER(self._transfer)
        self._seek_callback = SEEK(self._seek)
        self.library.storage_backend_callbacks.argtypes = [TRANSFER, SEEK]
        self.library.storage_backend_callbacks.restype = None
        self.library.storage_backend_close.restype = None
        self.library.storage_backend_callbacks(self._transfer_callback, self._seek_callback)
        self.library.storage_backend_open.argtypes = [ctypes.c_uint, ctypes.c_int]
        self.library.storage_backend_read.argtypes = [ctypes.c_int32, ctypes.c_void_p]
        self.library.storage_backend_write.argtypes = [ctypes.c_int32, ctypes.c_void_p]
        self.library.storage_backend_create.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.library.storage_backend_lookup.argtypes = [ctypes.c_char_p]
        self.library.storage_backend_create_at.argtypes = [ctypes.c_uint, ctypes.c_char_p,
                                                           ctypes.c_int]
        self.library.storage_backend_lookup_at.argtypes = [ctypes.c_uint, ctypes.c_char_p]
        self.library.storage_backend_reclaim.argtypes = [ctypes.c_uint]
        self.library.storage_backend_rename.argtypes = [ctypes.c_uint, ctypes.c_char_p,
                                                        ctypes.c_uint, ctypes.c_char_p]
        self.library.storage_backend_link.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.library.storage_backend_fill_parent.argtypes = [ctypes.c_uint]
        result = self.library.storage_backend_open(self.size_bytes // 512, int(order == "pdp"))
        if result:
            raise ValueError(f"backend open failed: {result}")

    def _seek(self, descriptor, offset, whence):
        try:
            if descriptor != 0 or whence != os.SEEK_SET:
                raise ValueError("unexpected descriptor or seek convention")
            checked_range(offset, 0, self.size_bytes)
            self.position = offset
            return offset
        except Exception as error:
            self.callback_errors.append(error)
            return -errno.EIO

    def _transfer(self, descriptor, pointer, length, writing):
        try:
            if descriptor != 0:
                raise ValueError("unexpected descriptor")
            address = Address(Domain.IMAGE_BYTE, self.position)
            if writing:
                amount = self.device.write(address, ctypes.string_at(pointer, length))
            else:
                data = self.device.read(address, length)
                ctypes.memmove(pointer, data, len(data))
                amount = len(data)
            self.position += amount
            return amount
        except TransferError:
            event = self.device.events[-1]
            if not writing and event.transferred:
                data = self.device.export()[event.offset:event.offset + event.transferred]
                ctypes.memmove(pointer, data, len(data))
            self.position += event.transferred
            self.device.phase = "compensation"
            return -errno.EIO
        except Interruption:
            return -errno.EIO
        except Exception as error:
            self.callback_errors.append(error)
            return -errno.EIO

    def _result(self, result):
        if self.callback_errors:
            raise RuntimeError("descriptor callback failed") from self.callback_errors[0]
        return result

    def read_sector(self, sector):
        buffer = ctypes.create_string_buffer(512)
        result = self.library.storage_backend_read(sector, buffer)
        return bool(self._result(result)), buffer.raw

    def write_sector(self, sector, data):
        if len(data) != 512:
            raise ValueError("descriptor write requires one V7 block")
        buffer = ctypes.create_string_buffer(data, 512)
        return bool(self._result(self.library.storage_backend_write(sector, buffer)))

    def mount(self):
        return self._result(self.library.storage_backend_mount())

    def format(self, inodes=64):
        return self._result(self.library.storage_backend_format(inodes))

    def create(self, name, directory=False):
        result = self.library.storage_backend_create(name.encode("ascii"), int(directory))
        return self._result(result)

    def lookup(self, name):
        return self._result(self.library.storage_backend_lookup(name.encode("ascii")))

    def create_at(self, parent, name, directory=False):
        return self._result(self.library.storage_backend_create_at(
            parent, name.encode("ascii"), int(directory)))

    def lookup_at(self, parent, name):
        return self._result(self.library.storage_backend_lookup_at(parent, name.encode("ascii")))

    def rename(self, source_parent, source, target_parent, target):
        return self._result(self.library.storage_backend_rename(
            source_parent, source.encode("ascii"), target_parent, target.encode("ascii")))

    def reclaim(self, number):
        return self._result(self.library.storage_backend_reclaim(number))

    def link(self, source, name):
        return self._result(self.library.storage_backend_link(source.encode("ascii"),
                                                             name.encode("ascii")))

    def fill_parent(self, blocks):
        return self._result(self.library.storage_backend_fill_parent(blocks))

    def sync(self):
        return self._result(self.library.storage_backend_sync())

    def close(self):
        self.library.storage_backend_close()
