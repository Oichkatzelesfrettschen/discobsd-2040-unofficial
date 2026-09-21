# Non-ARM source archive

The main source tree supports ARM machines only. This directory retains the
former MIPS/PIC32 sources and the Smaller C x86 backend for provenance and
historical study. The repository does not claim that the archived sources
compile, link, boot, or pass their former tests.

`tools/architecture-isolation/legacy-path-map.tsv` maps all 1,008 proven
non-ARM paths to their archive paths and records the source Git blob and
SHA-256. The archive verifier requires every immutable mapped blob to remain
byte-identical.

The explicit option validates the archive without admitting it to a maintained
build:

```sh
bmake MACHINE=rp2040 BUILD_LEGACY_NON_ARM=yes legacy-non-arm-verify
```

Restoring a supported non-ARM port requires a separate repository or worktree,
a declared machine/CPU/ABI tuple, an owned toolchain, warning-clean source,
tests, and hardware evidence. The option above supplies none of those claims.
