"""The model's admission rule against the shipped swapram.c.

model._ram_out() admits an image when the pool has one run of
roundgrain(cdata + cstack + SR_WORST(USIZE)) bytes and keeps
cdata + cstack + cu after the commit. Here swapram.c itself, compiled
unmodified, admits real images: an exec spool shapes its pool so the
largest run is exactly the rule's request, then four bytes less, and the
pool's own allocator, read through the spool entry points, measures what
the commit kept. Every image is restored and compared byte for byte, so
the file-scope pool is empty again after each example.
"""

import hypothesis.strategies as st
from hypothesis import given, settings

from swap_tier_model import native
from swap_tier_model.engine import roundgrain
from swap_tier_model.model import sr_worst

USIZE = 3072
POOL = 16 * 1024


def image(seed, n, fill):
    out = bytearray(n)
    x = seed * 2654435761 & 0xFFFFFFFF
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out[i] = (x >> 16) & 0xFF if (x >> 8) % 100 >= fill else (i >> 5) & 7
    return bytes(out)


@settings(max_examples=12)
@given(
    seed=st.integers(0, 2**16),
    dlen=st.integers(0, 9000),
    slen=st.integers(0, 3000),
    fill=st.integers(40, 99),
)
def test_admission_rule_matches_swapram(seed, dlen, slen, fill):
    ffi, lib = native.load()
    d, s, u = image(seed, dlen, fill), image(seed + 1, slen, fill), image(seed + 2, USIZE, fill)
    dn = lib.glue_hs_count(d, dlen) if dlen else 0
    sn = lib.glue_hs_count(s, slen) if slen else 0
    need = roundgrain(dn + sn + sr_worst(USIZE))
    assert lib.glue_swapram_largest() == POOL
    if need > POOL:
        return
    dbuf, sbuf, ubuf = (ffi.new("unsigned char[]", max(len(b), 1)) for b in (d, s, u))
    ffi.memmove(dbuf, d, len(d))
    ffi.memmove(sbuf, s, len(s))
    ffi.memmove(ubuf, u, len(u))
    for hold, admit in ((POOL - need + 4, False), (POOL - need, True)):
        spool = lib.glue_swapram_spool(hold) if hold else None
        assert spool != -1
        got = lib.glue_swapram_admit(0, dbuf, dlen, sbuf, slen, ubuf)
        assert got == (1 if admit else 0), (hold, need)
        if spool is not None:
            assert lib.glue_swapram_spool_free(spool, hold) == 0
        if not admit:
            continue
        out = [ffi.new("unsigned char[]", max(len(b), 1)) for b in (d, s, u)]
        assert lib.glue_swapram_restore(0, *out) == 0
        assert ffi.buffer(out[0], len(d))[:] == d
        assert ffi.buffer(out[1], len(s))[:] == s
        assert ffi.buffer(out[2], len(u))[:] == u
        assert lib.glue_swapram_largest() == POOL


@settings(max_examples=8)
@given(seed=st.integers(0, 2**16), dlen=st.integers(1, 9000), fill=st.integers(40, 99))
def test_commit_keeps_compressed_total(seed, dlen, fill):
    ffi, lib = native.load()
    d, u = image(seed, dlen, fill), image(seed + 2, USIZE, fill)
    dn = lib.glue_hs_count(d, dlen)
    un = lib.glue_hs_count(u, USIZE)
    if roundgrain(dn + sr_worst(USIZE)) > POOL:
        return
    dbuf = ffi.new("unsigned char[]", d)
    ubuf = ffi.new("unsigned char[]", u)
    assert lib.glue_swapram_admit(1, dbuf, dlen, ffi.NULL, 0, ubuf) == 1
    # An empty pool takes the image at offset 0; what remains above it is
    # the pool less the kept bytes, rounded to the grain.
    assert lib.glue_swapram_largest() == POOL - roundgrain(dn + un)
    out_d = ffi.new("unsigned char[]", dlen)
    out_u = ffi.new("unsigned char[]", USIZE)
    assert lib.glue_swapram_restore(1, out_d, ffi.NULL, out_u) == 0
    assert ffi.buffer(out_d, dlen)[:] == d
    assert ffi.buffer(out_u, USIZE)[:] == u
    assert lib.glue_swapram_largest() == POOL
