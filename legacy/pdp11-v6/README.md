# PDP-11 and Sixth Edition UNIX legacy option

The PDP-11 emulator, its Sixth Edition UNIX guest pack, and the independent
SIMH/V7 reference oracle form a provenance quarantine. The ARM build excludes
all three by default.

Enable the RP2040 emulator and guest pack explicitly:

```sh
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes build
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v6
```

Run the SIMH/V7 runner tests without an external image:

```sh
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes \
    check-legacy-pdp11-v7-runner
```

Run the external SIMH/V7 reference oracle separately:

```sh
PDP11_V7_IMAGE=/path/to/v7.dsk \
    bmake MACHINE=rp2040 BUILD_PDP11_V6=yes \
        check-legacy-pdp11-v7-reference
```

The oracle does not test the project emulator. It validates an externally
supplied image against its pinned SIMH profile and transcript.

The retained guest and license identities are:

| Artifact | SHA-256 |
| --- | --- |
| `usr.bin/pdp11/v6.rk.gz` | `61678c2e916120922a5fc4ffaa24afd47af68097c176b0ab294291905c40e9bf` |
| decompressed V6 pack | `300443c727acda3c7aa55e23ac5339390b00cd5479436450f4d9e3ac0cebe5c4` |
| `usr.bin/pdp11/Caldera-license.pdf` | `16514a4d9ea6426b85a9c4baea883dce2d71e5342eea0ebb6a32a40efc7035e3` |
| `usr.bin/pdp11/COPYING` | `7637386b5f81e8a719ca336233149005e5fa28b5e6054ea7b67de49355b0ad40` |

The checked-in gzip stream remains byte-for-byte historical evidence. The
archive records its upstream recipe but does not claim deterministic
regeneration of the gzip metadata.
