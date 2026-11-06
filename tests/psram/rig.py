"""Binds the compiled psram_xfer.c to the simulator: every register access
the driver makes is a ctypes callback into pio_sim.Pio, which advances the
state machine by the access cost before it answers."""

import ctypes

import pio_sim


class Rig:
    def __init__(
        self, lib_path, device=None, out_ns=3.0, in_ns=3.0, access_ticks=6
    ):
        self.dev = device or pio_sim.Device()
        self.pio = pio_sim.Pio(
            self.dev, out_ns=out_ns, in_ns=in_ns, access_ticks=access_ticks
        )
        self.pins = []
        self.unresets = 0
        self.lib = ctypes.CDLL(lib_path)
        rd = ctypes.CFUNCTYPE(ctypes.c_uint, ctypes.c_uint)
        wr = ctypes.CFUNCTYPE(None, ctypes.c_uint, ctypes.c_uint)
        nul = ctypes.CFUNCTYPE(None)
        pin = ctypes.CFUNCTYPE(None, ctypes.c_uint, ctypes.c_uint)
        dly = ctypes.CFUNCTYPE(None, ctypes.c_uint)
        sav = ctypes.CFUNCTYPE(ctypes.c_int)
        rst = ctypes.CFUNCTYPE(None, ctypes.c_int)
        self.error = None
        self._cb = (
            rd(self._guard(self.pio.read, 0)),
            wr(self._guard(self.pio.write, None)),
            nul(self._guard(self._unreset, None)),
            pin(self._guard(self._pin, None)),
            dly(self._guard(self.pio.delay_us, None)),
            sav(self._guard(self.pio.irq_save, 0)),
            rst(self._guard(self.pio.irq_restore, None)),
        )
        self.lib.psram_host_set_hooks(*self._cb)
        self.lib.psram_chunk_len.restype = ctypes.c_uint
        self.lib.psram_chunk_len.argtypes = [ctypes.c_uint, ctypes.c_uint]
        self.lib.psram_device_init.restype = ctypes.c_int
        self.lib.psram_read.restype = ctypes.c_int
        self.lib.psram_write.restype = ctypes.c_int
        self.lib.psram_read.argtypes = [
            ctypes.c_uint,
            ctypes.c_char_p,
            ctypes.c_uint,
        ]
        self.lib.psram_write.argtypes = [
            ctypes.c_uint,
            ctypes.c_char_p,
            ctypes.c_uint,
        ]

    def _guard(self, fn, default):
        """A ctypes callback swallows exceptions; keep the first one and
        raise it when the driver call returns."""

        def call(*a):
            if self.error is not None:
                return default
            try:
                return fn(*a)
            except Exception as e:  # noqa: BLE001
                self.error = e
                return default

        return call

    def _check(self, value):
        if self.error is not None:
            e, self.error = self.error, None
            raise e
        return value

    def _unreset(self):
        self.unresets += 1

    def _pin(self, n, pad):
        self.pins.append((n, pad))

    def hw_init(self):
        self._check(self.lib.psram_hw_init())

    def device_init(self):
        return self._check(self.lib.psram_device_init())

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        err = self._check(self.lib.psram_read(addr, buf, n))
        return err, bytes(buf.raw[:n])

    def write(self, addr, data):
        return self._check(self.lib.psram_write(addr, bytes(data), len(data)))

    def ids(self):
        return (
            ctypes.c_ubyte.in_dll(self.lib, "psram_mfid").value,
            ctypes.c_ubyte.in_dll(self.lib, "psram_kgd").value,
        )
