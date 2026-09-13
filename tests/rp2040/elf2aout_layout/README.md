# RP2040 ELF-to-a.out layout verification

The verifier links seven Cortex-M0+ section layouts through the production ARM
linker script. It requires every non-empty ELF load segment to be RX or RW,
checks the converted OMAGIC header and payload against the ELF address space,
and covers text-only, data-only, BSS-only, optional writable sections, and zero
data or BSS boundaries.

The negative controls require a one-segment RWX link to fail, malformed load
sizes and unsupported program headers to fail before changing the output, and
an overwrite with a shorter conversion to remove the complete old tail.
Symbol-mode validation checks every translated name against the initialized
string-table extent, including ELF symbols that share one source name offset.

Build the host converter and run the gate with an explicitly selected machine
and interpreter:

```sh
bmake MACHINE=rp2040 tools
PYTHON=/usr/bin/python bmake MACHINE=rp2040 check-elf2aout
```
