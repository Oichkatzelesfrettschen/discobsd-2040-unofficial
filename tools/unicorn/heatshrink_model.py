# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""A whole-stream reference for heatshrink streams with a 9-bit window.

The model follows the stream layout that machine/hsx_decoder.h states and
the vendored heatshrink encoder emits (sys/arch/rp2040/heatshrink): bits
are read most significant first; a tag bit 1 is followed by eight literal
bits, and a tag bit 0 by a window-bits index (distance - 1) and a
lookahead-bits count (length - 1). A back-reference copies length bytes
one at a time from distance bytes back, so a copy may overlap its own
output, and a byte before the first output byte reads as zero, the value
the vendored decoder's zeroed window holds. Decoding stops at the first
token whose bits are not all present; the bits after the last whole token
never contribute output.

The model shares no code with the kernel's bit-buffer loop
(hsx_decoder.c): it indexes the stream bit by bit.
"""

from __future__ import annotations

WINDOW_BITS = 9


def decode(stream: bytes, lookahead: int, window_bits: int = WINDOW_BITS) -> bytes:
    total = 8 * len(stream)
    position = 0

    def take(count: int) -> int:
        nonlocal position
        value = 0
        for _ in range(count):
            byte = stream[position >> 3]
            value = (value << 1) | ((byte >> (7 - (position & 7))) & 1)
            position += 1
        return value

    out = bytearray()
    while position < total:
        tag = (stream[position >> 3] >> (7 - (position & 7))) & 1
        if tag:
            if position + 9 > total:
                break
            take(1)
            out.append(take(8))
            continue
        if position + 1 + window_bits + lookahead > total:
            break
        take(1)
        distance = take(window_bits) + 1
        length = take(lookahead) + 1
        for _ in range(length):
            source = len(out) - distance
            out.append(out[source] if source >= 0 else 0)
    return bytes(out)


def encode_tokens(
    tokens: list[tuple], lookahead: int, pad: int = 0, window_bits: int = WINDOW_BITS
) -> bytes:
    """Serialize ("lit", byte) and ("ref", distance, length) tokens, then
    fill the last byte with the low bits of pad (fewer than eight bits,
    too few for a whole token)."""
    bits = []

    def put(value: int, count: int) -> None:
        bits.extend((value >> (count - 1 - i)) & 1 for i in range(count))

    for token in tokens:
        if token[0] == "lit":
            put(1, 1)
            put(token[1], 8)
        else:
            put(0, 1)
            put(token[1] - 1, window_bits)
            put(token[2] - 1, lookahead)
    spare = -len(bits) % 8
    put(pad & ((1 << spare) - 1), spare)
    return bytes(int("".join(map(str, bits[i : i + 8])), 2) for i in range(0, len(bits), 8))


def expand_tokens(tokens: list[tuple]) -> bytes:
    """The bytes a token list stands for, computed from the tokens alone."""
    out = bytearray()
    for token in tokens:
        if token[0] == "lit":
            out.append(token[1])
            continue
        _, distance, length = token
        for _ in range(length):
            out.append(out[-distance] if len(out) >= distance else 0)
    return bytes(out)
