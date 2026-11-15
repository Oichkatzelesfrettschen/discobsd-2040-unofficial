# Stdio EOF fixed-point qualification

The EOF counter repair passes aggregate non-hardware qualification and all
219 required execution-inventory variants at clean source
`e92025c1d1458d0c4857f5b8173fd6265c397283`, based on main
`152803ce08b544880bd1894cfe3ea88a4e158fca`. The source stays frozen throughout
the runs. The [contract](callable-character-io-contracts.md) owns the semantic
boundary, and the [resource exception](stdio-eof-fixed-point-resource-exception.md)
owns the exact owner-approved growth vector.

## Execution evidence

| Gate | Result | Immutable local invocation |
| --- | --- | --- |
| `logged-check` | Build and aggregate exit zero; lint, host, 32-bit POSIX shell, cross, QEMU, host-package and board-build tiers complete | `rp2040-check-local-20261115T175852Z-1420245-782cda6a` |
| `logged-ilp32` | 219/219 required variants pass; exit zero | `rp2040-ilp32-local-20261115T182938Z-1940968-f7419af4` |
| Clean production distribution and resource capture | Exit zero; dependency closure and static receipt validate | `rp2040-distribution-local-20261115T184653Z-2110470-5ecb6e14` |
| Native a.out archives | Both archive links pass; all 36 missing-member controls reject | `eof-e92025c1-native-archives-20261115T185111Z-2204370-9d11c01f` |
| `logged-flash` | Exit zero | `rp2040-flash-local-20261115T185016Z-2190683-4f43fc43` |
| `check-renode` | Exit zero; boot assertions and model-warning ceilings pass | `eof-e92025c1-renode-uart-20261115T185147Z-2212292-4ed9bc8f` |
| `check-renode-union` | Exit zero; union-kernel boot assertions and model-warning ceilings pass | `eof-e92025c1-renode-union-20261115T190146Z-2266575-1d017d29` |

Each local invocation retains its status receipt and full output under
`.build-logs`. The canonical-checkout retention directory is
`.cache/evidence/libc-eof-fixed-point`; it preserves that invocation tree and
the Renode results when the integration worktree retires. The aggregate
status SHA-256 is
`ec682f1a7d3a938220f5d88727d18324a3a7ad7fbbae8d983f9a7a7ede36e6ac`;
its build-log SHA-256 is
`749a2dc6a6a4d35edf3a2d40396cc195580b3f0a01913e60682b60c9d7d4fcc0`,
and its check-log SHA-256 is
`c77adf92b02a8f0ff2d444469ab7644baeea65ed629832686202ddf621bce632`.
The run checks 203 ShellCheck scripts and 392 Ruff files. Earlier lint timeout
receipts remain historical observations; the completed run supplies the
qualification verdict and leaves their cause unresolved.

The required-inventory status SHA-256 is
`94061ffc302d1ca277d30f110d58b56209b3fa28ef4cce4e7e44c12d3ed6e06c`,
and its log SHA-256 is
`c9eb755d449e5ca67ba95e14f47e6e4f2f38801ce0a12d9c0f8226eccfa72a7c`.
The UART Renode status/log SHA-256 values are
`d70de04ef4e632cc1fbf177cf6fde7a84f95ab976b09ca9401c57d9b036ec54a`
and `4e539f512ddbc339cad169e2d13543f406b47f9f091c4d952d10fa76fb13114e`.
The union Renode status/log SHA-256 values are
`39a83a5b4385378dc6d0e8a321e4c4f1eed33ead77fc4d370aac0c9287366759`
and `dc691242e7026420e13db0cd55388d0a229b67fac925ba068766e63719fa1e02`.

Native, ILP32, ASan/UBSan and Cortex-M0 QEMU executions each perform 3,963
character operations and reject six compiled mutations. The EOF mutation
removes only the counter normalization and exits with the required status 13.
The Smaller C stdio archive control rejects a damaged `filbuf.o` and accepts
the restored archive. The required inventory also passes its registered
character-I/O execution variants. An optional i386-time32 archive-listing
case beyond January 2038 explicitly skips inside its broader wrapper; every
required inventory variant supplies a passing receipt.

The [hosted run](https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/actions/runs/43744412267)
passes for `e92025c1`. It executes the warning-free build, headroom and flash
checks, lint, changed-comment/text checks and Renode UART boot. Its host,
cross, QEMU, dedicated ILP32, macOS and union-Renode lanes skip. Local host,
cross, QEMU, required-inventory and union-Renode execution records retain
separate authority. Local macOS execution remains unrun on the Linux host.

The local tools are Python 3.14.9, bmake 20260912, ARM GCC 16.2.0, GNU ld
2.47.20260726, host GCC 16.2.1 20260810, qemu-arm 11.1.3, ShellCheck 0.11.0
and Ruff 0.14.11. Renode identifies build
`1.17.0+20260907gitf1dd1b4af`; its RP2040 models are pinned at
`205a5e4b25440582008a4292074bb07f80a72328`.

## Resource and source correspondence

The owner approves the exact resource vector at evidence commit `55b79ded`.
The pushed archive ref `archive/libc-eof-resource-evidence` preserves that
commit and the measured candidate `8f9c4d91` against main `cc71239c`. The
subsequent main integration changes research documentation only. The tracked
production source and build inputs retain their measured identities; the
qualification source adds the explicit approval and narrows unmeasured
alternative-cost claims.

Merge `89cc029a` integrates main `46a63981`, whose intervening changes also
contain research documentation only. The non-documentation tree compares
byte-identically with qualified source `e92025c1`. The evidence documentation
receives separate parent review and changed-text/comment checks; the complete
execution claims remain bound to their recorded source and artifacts.

The [qualified resource receipt](stdio-eof-fixed-point-qualified-resources.json)
binds the restored production distribution to `e92025c1`. Its SHA-256 is
`3a7230c7d45cb582ce96c3d8f5296623656444eb1d278ef4a31dff4ec4bd20f5`.
Comparison against the approved parent produces the byte-identical retained
delta JSON, SHA-256
`517a0d30d867d18fd4e715be8e93b6a9bd5c4d5b2c9118f9eae1426f4de98810`.
Native libc remains 51,198 bytes; the root retains 752 data blocks, 227 free
blocks and 29 free inodes. The installed archive's SHA-256 still matches the
qualified receipt after the native link controls complete.

The resulting flash binary SHA-256 is
`a1d8f452a71ee16fdad6a40bed43149369aa4a59a50f10fa2226c555ae8d9f3e`,
and its UF2 SHA-256 is
`65e1e08c103fb0bf62562481e69e231f22573e3a292284f72c4a855bdfc73559`.
The root-image SHA-256 is
`f37acf345e6a7e0225e790c0212f940ad80ddd59aab68b6b14fc6ad12732851a`.
Runtime peak stack, heap, timing and swap traffic remain unmeasured. Optional
unshipped executable costs require their own measurements before installation.

## Review and remaining boundaries

The parent source review checks `_filbuf`, its callable and macro consumers,
pushback, positioning, the fixture and its mutation, and the approved resource
arithmetic. No actionable defect survives that inspection. The state transition
returns the count to zero on every established-EOF return; the next `getc`
decrement therefore reaches the representable value minus one. Existing
`ungetc` and `fseek` paths clear EOF before resumed input.

The [bounded external source review](stdio-eof-fixed-point-review.md) reports
no actionable findings for the exact `e92025c1` branch and `152803ce` base,
and the parent's source and arithmetic checks accept that conclusion. The
fixture uses controlled byte dependencies and the QEMU instruction model.
Renode exercises peripheral models. Physical-board execution remains unrun,
and the partial POSIX error, timestamp, signal and
concurrency facets retain their existing ledger states.
