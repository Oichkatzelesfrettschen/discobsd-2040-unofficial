# Kernel checksum transformation provenance

`kernsum` changes the linked RP2040 kernel's length and CRC words before image
conversion. `SYSTEM_LD_TAIL` invokes the stamp outside the compiler/linker
wrapper, so a clean captured distribution retains the pre-stamp ELF while the
shipped-closure collector reads the stamped ELF. The collector rejects the
final kernel with `missing producer for sys/arch/rp2040/compile/PICO/unix`.
The failed matched string-header distributions provide the reproducer; their
successful build receipts establish compilation, while their closure verdicts
remain failures.

The repair classifies `kernsum unix` as an in-place transformation and
`kernsum -c unix` as a read-only query. The existing recorder retains input
bytes before execution and output bytes only after success. The kernel
Makefile template and all four generated configuration Makefiles place the
capture prefix before the stamp. Default builds retain the same stamp command
when capture is disabled. Kernel identity words and CRC computation retain
their existing implementations.

The recorder calibration checks operation shape and evaluated production
recipes. The closure calibration builds an ARM ELF with zero identity words,
records the real checksum transformation, verifies its result through `-c`,
and resolves the final artifact back to assembly and linker-script sources.
Withholding only the transformation receipt must reproduce the missing-producer
failure. The control changes the evidence graph while retaining the artifact,
so a checksum-valid image alone cannot satisfy source attribution.

The existing `check-c17-tool-capture`, `check-config-generated-sync` and
`check-build-failure` targets own regression coverage. Paired clean distributions
with fresh source snapshots and capture directories establish repaired complete
attribution. Static resource comparison still requires matching collector and
toolchain identities; the adapter supplies provenance rather than a resource
saving or a board-execution claim.
