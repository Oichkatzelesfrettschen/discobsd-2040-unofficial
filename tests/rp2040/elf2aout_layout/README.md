# RP2040 ELF-to-a.out layout verification

The verifier links seven Cortex-M0+ section layouts through the production ARM
linker script. It also links MIPS32r2 objects and the multicall BSS overlay
through the production MIPS linker script before converting that image. It
requires every non-empty ARM ELF load segment to be RX or RW,
checks the converted OMAGIC header and payload against the ELF address space,
and covers text-only, data-only, BSS-only, optional writable sections, and zero
data or BSS boundaries.

The negative controls require a one-segment RWX link to fail, malformed load
sizes and unsupported program headers to fail before changing the output, and
an overwrite with a shorter conversion to remove the complete old tail.
Symbol-mode validation checks every translated name against the initialized
string-table extent, including ELF symbols that share one source name offset.
The MIPS gate forces a small-data `.sbss` input and checks its absorption into
the private overlay, shared applet VMAs, maximum private BSS extent, separation
from shared BSS, initialized-data retention, and the exact a.out header and
payload. The shared `MIPS_GCCPREFIX` uses `mipsel-elf` on Linux and the
platform-specific `mips-elf` paths on BSD hosts. An environment or make
argument can name another GNU little-endian MIPS cross-tool prefix explicitly.

Build the host converter and run the gate with an explicitly selected machine
and interpreter:

```sh
bmake MACHINE=rp2040 tools
PYTHON="${PYTHON}" bmake MACHINE=rp2040 check-elf2aout
```
