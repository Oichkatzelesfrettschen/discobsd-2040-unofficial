"""Replay a swap trace through the shipped pool and swap map.

Admission follows swapram_reserve() in sys/arch/rp2040/rp2040/swapram.c:
the request is the compressed data and stack plus SR_WORST(USIZE) for the u
area, first fit in the pool, trimmed to the compressed total on commit. A
refusal is "nospace" when the grain-rounded request exceeds the free bytes
and "fragment" otherwise, as sr_probe_pool_refused() classifies it.

The flash path follows kern/vm_swap.c: swap_exchange_switch() writes a
refused outgoing image over a flash-backed incoming image's extent when the
tail fits (SWAP_IMAGE_EXCHANGE), else into a fresh extent from
malloc3_contiguous_next(); a fork copy (swap_image_reserve()) always takes
a fresh extent. An image erases every 4 KiB sector its bytes reach
(flash_swap_append() in sys/arch/rp2040/dev/flash_swap.c), which is its
span in sectors.

Variant knobs live in Config; Config() is the shipped PICO kernel.
"""

from dataclasses import dataclass, field

from .engine import ALIGN_BLOCKS, BLOCK, SECTOR, FlashSwapDriver, Nor, Pool, SwapMap, roundgrain

# FLASH_SWAP_OFFSET of sys/arch/rp2040/dev/flash.h: 2048 KiB less 384 KiB.
FLASH_SWAP_OFFSET = 0x1a0000

USIZE = 3072


def sr_worst(n):
    """SR_WORST in swapram.c: heatshrink's nine-bit literal bound."""
    return n + n // 8 + 4


def btod(n):
    return (n + BLOCK - 1) // BLOCK


@dataclass(frozen=True)
class Config:
    pool_kb: int = 16                 # SWAPRAM_KB in compile/PICO/Config
    nseg: int = 17                    # NPROC + 1 free segments (swapram.c)
    u_compressed: bool = False        # variant 1: reserve the u area at cu
    u_slack: int = 0                  #   plus this many bytes
    compaction: bool = False          # variant 2: compact, then retry
    flash_compressed: bool = False    # variant 4: refused images compressed
    zero_elision: bool = False        # variant 5: skip all-zero sectors
    flash_kb: int = 384               # variant 7: raw swap size
    flash: bool = True                # variant 8: False puts swap off NOR
    guard_blocks: int = 196           # SWAP_IMAGE_GUARD (machparam.h)


SHIPPED = Config()


@dataclass
class Counters:
    values: dict = field(default_factory=dict)

    def note(self, name, n=1):
        self.values[name] = self.values.get(name, 0) + n

    def get(self, name):
        return self.values.get(name, 0)


class Model:
    def __init__(self, config=None, cursor_sector=0, nor=False):
        config = config or SHIPPED
        self.config = config
        self.pool = Pool(config.pool_kb * 1024, config.nseg) \
            if config.pool_kb else None
        self.nswap = config.flash_kb * 1024 // BLOCK
        self.map = SwapMap(self.nswap, config.guard_blocks,
                           ALIGN_BLOCKS + cursor_sector * ALIGN_BLOCKS)
        self.sectors = self.nswap // ALIGN_BLOCKS
        self.erases = [0] * self.sectors
        self.where = {}               # pid -> ("ram", off, res, sizes)
        self.counters = Counters()    # | ("flash", base, span) | ("lost",)
        self.peak_blocks = 0
        self.phase = "prefix"
        self.snapshots = []
        self.driver = None
        if nor:
            # Write every flash image through flash_swap_append() into a
            # NOR model of the swap region, so the erase counts come from
            # the shipped writer rather than from the span arithmetic.
            self.nor = Nor(FLASH_SWAP_OFFSET, self.nswap * BLOCK)
            self.driver = FlashSwapDriver(self.nor)

    # -- counters -----------------------------------------------------
    def note(self, name, n=1):
        self.counters.note(name, n)

    def snapshot(self):
        self.snapshots.append((dict(self.counters.values), list(self.erases)))

    # -- flash --------------------------------------------------------
    def _fold(self, block):
        while block >= self.nswap:
            block -= self.nswap - ALIGN_BLOCKS
        return block

    def _write(self, base, shape):
        """Append each nonempty segment at its block, folding at the ring
        end the way swap_with_buf_result() folds a block past nswap."""
        block = base
        for blocks in shape:
            left, at = blocks, block
            while left:
                at = self._fold(at)
                n = min(left, self.nswap - at)
                payload = bytes([0x5a]) * (n * BLOCK)
                if self.driver.append(FLASH_SWAP_OFFSET + at * BLOCK,
                                      payload) != 0:
                    raise RuntimeError(f"flash_swap_append refused: "
                                       f"{self.nor.violations[-1:]}")
                at += n
                left -= n
            block += blocks

    def _erase(self, base, blocks, skip=0, shape=None):
        if self.driver is not None and shape is not None:
            self._write(base, shape)
        sectors = (blocks + ALIGN_BLOCKS - 1) // ALIGN_BLOCKS
        for i in range(sectors - skip):
            sector = self._fold(base + i * ALIGN_BLOCKS) // ALIGN_BLOCKS
            self.erases[sector] += 1
        self.note("swap_erase_sectors", sectors - skip)

    def _in_use(self):
        used = self.nswap - ALIGN_BLOCKS - self.map.free_blocks()
        self.peak_blocks = max(self.peak_blocks, used)

    def _flash_shape(self, r):
        if self.config.flash_compressed:
            return [btod(r["cdata"] + r["cstack"] + r["cu"]), 0, 0]
        return [btod(r["data"]), btod(r["stack"]), btod(r["u"])]

    def _span(self, shape):
        blocks = sum(shape)
        return (blocks + ALIGN_BLOCKS - 1) & ~(ALIGN_BLOCKS - 1)

    def _flash_out(self, r):
        shape = self._flash_shape(r)
        span = self._span(shape)
        skip = r["zero_sectors"] if self.config.zero_elision else 0
        if not self.config.flash:
            self.where[r["pid"]] = ("offchip",)
            self.note("swapram_flash_swapouts")
            return
        incoming = r.get("incoming")
        if incoming is not None and \
                self.where.get(incoming, (None,))[0] == "flash":
            _kind, base, in_span = self.where[incoming]
            if self.map.take_tail(base, in_span, span):
                self._erase(base, sum(shape), skip, shape)
                if in_span > span:
                    self.map.free(in_span - span, base + span)
                self.where[incoming] = ("exchanged",)
                self.where[r["pid"]] = ("flash", base, span)
                self.note("swapram_exchange_swapouts")
                self.note("swap_write_bytes", r["data"] + r["stack"] + r["u"])
                self._in_use()
                return
        got = self.map.alloc(*shape)
        if got is None:
            self.note("flash_refusals")
            self.where[r["pid"]] = ("lost",)
            return
        base, span = got
        self._erase(base, sum(shape), skip, shape)
        self.where[r["pid"]] = ("flash", base, span)
        self.note("swapram_flash_swapouts")
        self.note("swap_write_bytes", r["data"] + r["stack"] + r["u"])
        self._in_use()

    # -- pool ---------------------------------------------------------
    def _live_ram(self):
        return {pid: (w[1], w[2]) for pid, w in self.where.items()
                if w[0] == "ram"}

    def _ram_out(self, r):
        if self.pool is None:
            self.note("swapram_refused_closed")
            return False
        ds = r["cdata"] + r["cstack"]
        if self.config.u_compressed:
            want = ds + r["cu"] + self.config.u_slack
        else:
            want = ds + sr_worst(r["u"])
        off = self.pool.alloc(want)
        need = roundgrain(want)
        if off is None and self.config.compaction and \
                need <= self.pool.avail():
            moved = self.pool.compact(self._live_ram())
            for pid, new in moved.items():
                _k, _o, res, sizes = self.where[pid]
                self.where[pid] = ("ram", new, res, sizes)
            self.note("compactions")
            off = self.pool.alloc(want)
        if off is None:
            avail, largest = self.pool.avail(), self.pool.largest()
            shortfall = need - largest if need > largest else 0
            bin_ = min((shortfall - 1) >> 10, 7) if shortfall else 0
            names = ["1k", "2k", "3k", "4k", "5k", "6k", "7k", "more"]
            self.note("swapram_short_" + names[bin_])
            self.note("swapram_refused_want_bytes", want)
            self.note("swapram_refused_free_bytes", avail)
            self.note("swapram_refused_largest_bytes", largest)
            if ds <= largest:
                self.note("swapram_refused_data_fits")
            self.note("swapram_refused_nospace" if need > avail
                      else "swapram_refused_fragment")
            self.note("swapram_refuse_scan_bytes", r["data"] + r["stack"])
            return False
        used = ds + r["cu"]
        if not self.pool.trim(off, want, used):
            raise RuntimeError("pool trim refused")
        self.where[r["pid"]] = ("ram", off, used, r)
        self.note("swapram_admissions")
        self.note("swapram_admitted_data_bytes", ds)
        self.note("swapram_admitted_u_bytes", r["cu"])
        self.note("swapram_admit_scan_bytes", r["data"] + r["stack"])
        return True

    # -- trace --------------------------------------------------------
    def release(self, pid):
        w = self.where.pop(pid, None)
        if w is None:
            return
        if w[0] == "ram":
            if not self.pool.free(w[1], w[2]):
                raise RuntimeError(f"pool free refused for pid {pid}")
        elif w[0] == "flash":
            self.map.free(w[2], w[1])

    def out(self, r):
        if r["pid"] in self.where:
            raise RuntimeError(f"pid {r['pid']} swapped out twice")
        self.note("swapram_attempts")
        span = self._span([btod(r["data"]), btod(r["stack"]),
                           btod(r["u"])]) // ALIGN_BLOCKS
        tier = "ram" if self._ram_out(r) else "flash"
        if tier == "flash":
            self._flash_out(r)
        self.note(f"swapram_{tier}_span_{min(max(span, 1), 12):02d}")
        if r.get("incoming") is not None:
            self.release(r["incoming"])

    def replay(self, records):
        for r in records:
            op = r["op"]
            if op == "out":
                self.out(r)
            elif op in ("in", "exit"):
                self.release(r["pid"])
            elif op == "mark":
                self.snapshot()
        return self

    def delta(self):
        """Counter and per-sector erase deltas between the two snapshots."""
        (c0, e0), (c1, e1) = self.snapshots[0], self.snapshots[-1]
        names = set(c0) | set(c1)
        counters = {n: c1.get(n, 0) - c0.get(n, 0) for n in names}
        erases = [b - a for a, b in zip(e0, e1)]
        return counters, erases


def run(records, config=None, cursor_sector=0, nor=False):
    return Model(config, cursor_sector, nor).replay(records)


__all__ = ["Config", "Model", "run", "sr_worst", "SECTOR"]
