# Packed-stream history-ring call bounds

`hsx_expand` limits each discard-mode decoder call to the contiguous suffix of
the 512-byte history ring. `hsx_dec_run` writes a contiguous run and the caller
passes that same run to `crc32_update`. With ring capacity W, produced position
p, requested room r and produced length n, the contract is

```text
0 <= n <= r <= W - (p mod W).
```

`sys/arch/rp2040/include/hsx_decoder.h` states the caller's obligation.
`hsx_stream.c` enforces it before calling the decoder. Production code retains
that guard; the change strengthens its test oracle.

## Output equivalence leaves the caller boundary unproven

Hosted macOS run `43347072435`, job `131628887234`, rejects 34 decoder mutations
but admits the mutation that raises the ring-clamp threshold by eight bytes.
The unmodified differential corpus passes 285,963 cases and 1,526,435 checks.
The failed calibration prevents a passing cross-tier verdict.

A deterministic 513-literal stream explains the gap. The mutated run writes
past the ring into already-consumed staging bytes, and the CRC reads those same
bytes. Its output and independently computed CRC still agree. GCC and Clang
both accept that output-only probe; a probe checking the caller's contiguous
range rejects the mutant with status 13 and accepts production with status 0.
The evidence identifies a test-oracle gap, not a production decoder overrun.

## Deterministic caller-contract witness

The differential fixture routes only the stream loop's decoder call through
`hsx_checked_run`, a test adapter that checks the ring base and contiguous room
before delegating to the production decoder. Invalid room reports a named
failure and returns before a decoder write. The production source remains
unchanged; a compile-time symbol rename binds the test adapter in both the
Makefile and mutation runner.

Eight literal streams have lengths W+1 through W+8 and execute before the
distribution-dependent corpus. Their contents and order use no pseudo-random
state or directory enumeration. The ring mutant must exit 1 with the diagnostic
`history ring run crosses end`; another fault or a timeout cannot satisfy that
particular rejection. The other 34 mutations retain their existing verdicts.

```sh
export PYTHON=/usr/bin/python
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-hsx-decoder
"$PYTHON" tests/hsaout/hsx_decoder_mutants.py --cc clang --topsrc .
```

The gate establishes the exercised host call boundary and differential decoder
behavior. It supplies no silicon timing, MPU, shared-libc runtime or physical
board evidence. Shipped source and resource layouts remain unchanged.
