"""Python handles on the shipped allocators and the raw-swap writer.

Pool wraps swapram_pool.c over a private buffer, so every instance is
independent of swapram.c's file-scope pool. SwapMap wraps subr_rmap.c's
ring through glue.c; the kernel has one swap map, and so does the shared
object, so at most one SwapMap is live at a time. Nor is a NOR flash model
that the shipped flash_swap.c drives through cffi callbacks: it rejects a
program that would turn a 0 bit into a 1, an unaligned or partial-page
program, and an unaligned erase, and counts erases per 4 KiB sector.
"""

from . import native

GRAIN = 4
SECTOR = 4096
PAGE = 256
BLOCK = 1024
ALIGN_BLOCKS = 4


def roundgrain(n):
    return (n + GRAIN - 1) & ~(GRAIN - 1)


class Pool:
    """swapram_pool.c over its own buffer of size bytes and nseg segments."""

    def __init__(self, size, nseg=17, overrides=None):
        self.ffi, self.lib = native.load(overrides)
        self.size = size
        self.nseg = nseg
        self.mem = self.ffi.new("unsigned char[]", max(size, 1))
        self.seg = self.ffi.new("struct swapram_seg[]", max(nseg, 1))
        self.pool = self.ffi.new("struct swapram_pool *")
        self.lib.swapram_pool_init(self.pool, self.mem, size, self.seg, nseg)
        self._off = self.ffi.new("unsigned int *")

    def alloc(self, size):
        if self.lib.swapram_pool_alloc(self.pool, size, self._off) < 0:
            return None
        return self._off[0]

    def free(self, off, size):
        return self.lib.swapram_pool_free(self.pool, off, size) == 0

    def trim(self, off, old, new):
        return self.lib.swapram_pool_trim(self.pool, off, old, new) == 0

    def avail(self):
        return self.lib.swapram_pool_avail(self.pool)

    def largest(self):
        return self.lib.swapram_pool_largest(self.pool)

    def segments(self):
        return [(self.seg[i].s_off, self.seg[i].s_size)
                for i in range(self.pool.p_used)]

    def read(self, off, n):
        return bytes(self.ffi.buffer(self.mem + off, n))

    def write(self, off, data):
        self.ffi.memmove(self.mem + off, data, len(data))

    def compact(self, live):
        """Slide live allocations to the bottom of the pool, keeping their
        address order and bytes, and rebuild the free list as one run.

        live maps an owner to (offset, size); the result maps it to the new
        offset. This is the model of pool compaction, not shipped code.
        """
        order = sorted(live.items(), key=lambda item: item[1][0])
        saved = {owner: self.read(off, roundgrain(size))
                 for owner, (off, size) in order}
        self.lib.swapram_pool_init(self.pool, self.mem, self.size, self.seg,
                                   self.nseg)
        moved = {}
        for owner, (_off, size) in order:
            new = self.alloc(size)
            if new is None:
                raise RuntimeError("compaction lost an allocation")
            self.write(new, saved[owner])
            moved[owner] = new
        return moved


_MAP_OWNER = []


class SwapMap:
    """The kernel swap map: a ring over blocks [4, nswap) through subr_rmap.c."""

    def __init__(self, nswap, guard=196, cursor=None, overrides=None):
        if _MAP_OWNER and _MAP_OWNER[0] is not None:
            _MAP_OWNER[0].closed = True
        self.ffi, self.lib = native.load(overrides)
        self.nswap = nswap
        self.closed = False
        if cursor is None:
            cursor = ALIGN_BLOCKS
        if self.lib.glue_map_reset(nswap, guard, cursor) != 0:
            raise RuntimeError(self._panic())
        _MAP_OWNER[:] = [self]
        self._out = self.ffi.new("size_t[3]")

    def _panic(self):
        return self.ffi.string(self.lib.glue_panic_message()).decode()

    def _live(self):
        if self.closed:
            raise RuntimeError("a newer SwapMap replaced this one")

    def alloc(self, dblocks, sblocks, ublocks):
        """Return (base, span) or None when the map refuses."""
        self._live()
        got = self.lib.glue_map_alloc(dblocks, sblocks, ublocks, self._out)
        if got == native.GLUE_PANIC:
            raise RuntimeError(self._panic())
        if got == 0:
            return None
        return self._out[0], got

    def free(self, span, base):
        self._live()
        if self.lib.glue_map_free(span, base) != 0:
            raise RuntimeError(self._panic())

    def cursor(self):
        """swapnext, the next-fit cursor in blocks."""
        self._live()
        return self.lib.glue_map_cursor()

    def runs(self):
        self._live()
        addr = self.ffi.new("size_t[64]")
        size = self.ffi.new("size_t[64]")
        n = self.lib.glue_map_runs(addr, size, 64)
        return [(addr[i], size[i]) for i in range(n)]

    def free_blocks(self):
        return sum(size for _addr, size in self.runs())

    def take_tail(self, base, incoming_span, outgoing_span):
        """swap_exchange_tail_reserve() of kern/vm_swap.c, mirrored in
        glue.c: claim the outgoing span's excess past the incoming extent at
        base. True when the exchange can proceed."""
        self._live()
        got = self.lib.glue_map_take_tail(base, incoming_span, outgoing_span)
        if got == native.GLUE_PANIC:
            raise RuntimeError(self._panic())
        return got == 1


class Nor:
    """NOR flash over [base, base + size): erase sets 0xff, program ANDs."""

    def __init__(self, base, size):
        self.base = base
        self.size = size
        self.data = bytearray(b"\xff" * size)
        self.erases = [0] * (size // SECTOR)
        self.programs = 0
        self.violations = []

    def _range(self, offset, length):
        rel = offset - self.base
        return 0 <= rel and length <= self.size and rel <= self.size - length

    def read(self, offset, length):
        if not self._range(offset, length):
            self.violations.append(("read", offset, length))
            return None
        rel = offset - self.base
        return bytes(self.data[rel:rel + length])

    def erase(self, offset, length):
        if (not self._range(offset, length) or length != SECTOR or
                offset % SECTOR):
            self.violations.append(("erase", offset, length))
            return -1
        rel = offset - self.base
        self.data[rel:rel + length] = b"\xff" * length
        self.erases[rel // SECTOR] += 1
        return 0

    def program(self, offset, payload):
        length = len(payload)
        if (not self._range(offset, length) or length != PAGE or
                offset % PAGE):
            self.violations.append(("program", offset, length))
            return -1
        rel = offset - self.base
        new = int.from_bytes(payload, "big")
        have = int.from_bytes(self.data[rel:rel + length], "big")
        if new & ~have:
            self.violations.append(("zero-to-one", offset))
            return -1
        self.data[rel:rel + length] = (new & have).to_bytes(length, "big")
        self.programs += 1
        return 0


class FlashSwapDriver:
    """flash_swap.c's append and rewrite over a Nor, through cffi callbacks."""

    def __init__(self, nor, overrides=None):
        self.ffi, self.lib = native.load(overrides)
        self.nor = nor
        ffi = self.ffi

        @ffi.callback("int(unsigned int, unsigned char *, unsigned int)")
        def read(offset, out, length):
            got = nor.read(offset, length)
            if got is None:
                return -1
            ffi.memmove(out, got, length)
            return 0

        @ffi.callback("int(unsigned int, unsigned int)")
        def erase(offset, length):
            return nor.erase(offset, length)

        @ffi.callback("int(unsigned int, const unsigned char *, unsigned int)")
        def program(offset, data, length):
            return nor.program(offset, bytes(ffi.buffer(data, length)))

        self._callbacks = (read, erase, program)
        self.ops = ffi.new("struct flash_swap_ops *",
                           {"read": read, "erase": erase, "program": program})
        self.scratch = ffi.new("unsigned char[]", PAGE)

    def append(self, offset, payload):
        buf = self.ffi.new("unsigned char[]", payload) if payload else \
            self.ffi.NULL
        return self.lib.flash_swap_append(self.ops, offset, buf, len(payload),
                                          self.scratch)

    def rewrite(self, offset, payload, spare):
        buf = self.ffi.new("unsigned char[]", payload) if payload else \
            self.ffi.NULL
        return self.lib.flash_swap_rewrite(self.ops, offset, buf,
                                           len(payload), spare, self.scratch)
