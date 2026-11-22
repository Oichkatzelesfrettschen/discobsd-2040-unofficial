# RP2040 ELF-to-a.out layout verification

The verifier links seven Cortex-M0+ section layouts through the production ARM
linker script and converts the multicall BSS overlay through the same ARM
path. It requires every non-empty ARM ELF load segment to be RX or RW,
checks the converted OMAGIC header and payload against the ELF address space,
and covers text-only, data-only, BSS-only, optional writable sections, and zero
data or BSS boundaries.

The negative controls require a one-segment RWX link to fail, malformed load
sizes and unsupported program headers to fail before changing the output, and
an overwrite with a shorter conversion to remove the complete old tail.
Symbol-mode validation checks every translated name against the initialized
string-table extent, including ELF symbols that share one source name offset.
The overlay gate checks shared applet VMAs, maximum private BSS extent,
separation from shared BSS, initialized-data retention, and the exact a.out
header and payload.

The cross tier runs this gate as one of `CROSS_CONTRACT_GATES`. Naming it on
its own takes an explicitly selected machine and interpreter, and reaches the
same result on a bare checkout: the fixtures are assembled here rather than
read out of a built tree, so `tools` and the cross toolchain are the whole
prerequisite.

```sh
PYTHON="${PYTHON}" bmake MACHINE=rp2040 check-elf2aout
```

The shared fixture links a file-only program header of type 0x64787271.
It contains the 56-byte XREQ record. Its read-only flags, zero virtual and
physical addresses, and zero memory extent keep it outside both process
segments. The converter places the requirement immediately after the
flagged OMAGIC header. The gate compares all record and payload bytes. A
loaded requirement, an orphaned requirement section and a misplaced BSS
reservation each reject the input before replacement of an existing output.
